#pragma once

#include "render/renderer.hpp"
#include "sys/system.hpp"
#include "taskmgr/charts.hpp"
#include "taskmgr/history.hpp"
#include "taskmgr/panels.hpp"
#include "taskmgr/ui_layout.hpp"
#include "types/color.hpp"
#include "types/rect.hpp"
#include <string>
#include <vector>

// 任务管理器界面：顶部横向导航栏 + 两个页面。
//  - 概览页：下方以卡片形式展示 CPU / 内存 / 进程数等总体信息；
//  - 进程页：下方以列表形式展示进程（名称、PID、CPU、内存）。
// 直接使用 Renderer 的绘制 API，不依赖旧的节点树 UI 系统。
// Task manager UI: a top horizontal navbar plus two pages (overview cards / process list),
// drawn directly with the Renderer API.
class TaskManager {
    friend struct OverviewPanel;
    friend struct ProcessesPanel;
public:
    // 初始化系统采样器并预热快照
    // Initialize the system sampler and warm up its snapshot
    bool init();

    // 窗口尺寸变化时重新布局
    // Relayout when the window size changes
    void resize(float width, float height);

    // 每帧更新：处理鼠标/滚轮输入，并按需刷新系统数据。
    // 返回本帧是否需要重绘（数据刷新或动画进行中）。
    // mx/my 光标坐标；mouse_down 当前是否按住；mouse_pressed 本帧是否刚按下；
    // scroll_delta 本帧滚轮增量；dt 帧间隔（秒）。
    // Per-frame update: handle mouse/scroll input and refresh system data when needed.
    // Returns whether a repaint is required (data refreshed or animation running).
    bool update(float mx, float my, bool mouse_down, bool mouse_pressed, double scroll_delta, float dt);
    // 原生搜索输入框文本变化入口：更新关键字并标记需要重绘。
    // Native search-field text-change entry: update the keyword and request a repaint.
    void set_search_text(const char* text);
    // 原生结束进程按钮点击入口：触发结束进程确认流程。
    // Native end-process button click entry: start the end-process confirmation flow.
    void on_end_button_clicked();

    // 绘制当前页面
    // Draw the current page
    void draw(Renderer& renderer);

private:
    // 页面类型
    enum class Page { Overview, Processes };

    // 重新计算各区域矩形
    // Recompute the layout rects
    void layout();

    // 各部件绘制
    // 左侧侧边栏导航（Win11 风格：图标 + 文字）
    void draw_sidebar(Renderer& r);

    // 窗口尺寸与布局（Win11：左侧侧边栏 + 内容区）
    float w_ = 0, h_ = 0;
    UiLayout layout_;          // 布局几何（各区域矩形）
    // 侧边栏导航项悬停状态
    bool hover_nav_perf_ = false, hover_nav_proc_ = false;
    float nav_anim_ = 0;  // 顶部导航选中指示位置（0=性能，1=进程），用于白色矩形滑动动画
    float press_mx_ = 0, press_my_ = 0;  // 最近按下点（用于顶部栏拖动判定）
    bool dragging_win_ = false;          // 本帧已触发窗口拖动
    // 性能页：选中设备索引 0=CPU 1=内存 2=磁盘 3=GPU
    int dev_sel_ = 0;
    int hover_dev_ = -1;            // 悬停的设备项索引

    // 页面与系统数据
    Page page_ = Page::Overview;
    sys::Sampler sampler_;
    sys::Overview ov_;
    std::vector<sys::ProcInfo> procs_;

    // 硬件信息（一次性读取）
    sys::CpuInfo cpu_;
    sys::GpuInfo gpu_;

    // 历史曲线数据层：各系列使用率采样 + 累计采样数（驱动竖线网格滚动）
    // Usage-history data layer
    History hist_;
    // 图表绘制模块（引用历史与总体数据）
    // Chart drawing module (references history & overview data)
    Charts charts_{hist_, ov_};
    // 页面绘制面板（性能页 / 进程页）
    // Page drawing panels (overview / processes)
    OverviewPanel overview_{*this};
    ProcessesPanel processes_{*this};

    // 结束进程：顶部栏按钮矩形、选中 PID 与确认框流程图状态
    // End process: top-bar button rect, selected PID and the confirm-dialog state machine
    enum class EndStage { None, Confirm, ForceConfirm };  // None=无 Confirm=确认结束 ForceConfirm=确认强制结束
    Rect endbtn_rect_;              // 结束进程按钮矩形（进程页顶部栏右侧，用于定位原生按钮）
    int  sel_pid_ = -1;             // 当前选中进程 PID（用 PID 而非索引，避免刷新后错位）
    EndStage end_stage_ = EndStage::None;  // 当前结束流程状态（None=无 sheet；Confirm/ForceConfirm=sheet 等待回调）
    std::string end_msg_;           // 原生确认 sheet 的消息文本
    int end_pid_ = -1;              // 当前 sheet 对应的进程 PID
    int pending_pid_ = -1;          // 已发 SIGTERM、等待退出的进程 PID
    float pending_timer_ = 0;       // 等待计时（秒）
    static void end_confirm_done(int choice, void* user);  // 原生确认 sheet 回调（主线程）

    // 进程页状态
    float scroll_ = 0;       // 列表滚动偏移（像素）
    float row_h_ = 32;       // 进程行高
    float refresh_timer_ = 0; // 距离上次性能页/历史刷新的时间（秒）
    float proc_timer_ = 0;    // 距离上次进程列表刷新的时间（秒）
    // 进程页分组（前台进程 / 后台进程）展开状态与点击区域（仿 Win11 树形分组）
    bool group_front_open_ = true;
    bool group_bg_open_ = true;
    Rect group_front_hit_;
    Rect group_bg_hit_;
    bool hover_group_front_ = false, hover_group_bg_ = false;
    int hover_proc_row_ = -1;   // 悬停的进程行索引
    int sel_proc_row_ = -1;     // 选中（高亮）的进程行索引
    std::vector<std::pair<Rect,int>> proc_row_hit_;  // 每帧重建：屏幕矩形 -> procs_ 索引，用于点击命中
    // 进程列表排序：点击表头列切换升/降序（0名称 1PID 2进程名 3CPU 4内存 5磁盘）
    int sort_col_ = 3;      // 默认按 CPU 列
    bool sort_asc_ = false; // 默认降序（CPU 占用从高到低）
    std::vector<Rect> head_cols_;   // 表头各列命中矩形
    void apply_sort();              // 按当前排序列/方向排序 procs_
    // 搜索框：输入过滤
    Rect search_rect_;              // 搜索框矩形（用于定位原生输入框）
    std::string search_text_;       // 搜索关键字
    bool search_changed_ = false;   // 搜索关键字是否变化（标记需重绘）
};
