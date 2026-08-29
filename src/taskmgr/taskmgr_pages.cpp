#include "taskmgr/taskmgr.hpp"
#include "taskmgr/taskmgr_internal.hpp"
#include "sys/system.hpp"

#include <algorithm>
#include <string>

#if defined(__APPLE__)
// 避免引入 GLFW/Vulkan 头，仅前置声明（实现在 frontmost.mm）
void platform_set_ime_rect(float x, float y, float w, float h);
#endif

// 界面语言翻译帮助（由 sys/system.hpp 提供，跟随系统语言）
using ::sys::tr;

// 页面绘制：侧边栏导航 / 性能页（设备列表 + 详情）/ 进程页（分组列表）。
// Page drawing: sidebar nav, overview page (device list + detail), processes page.

void TaskManager::draw(Renderer& r) {
    // 同步系统深浅色模式，刷新当前配色（运行中切换也实时跟随）
    theme::update();
    // 先铺满内容区背景，再画左侧侧边栏与当前页面
    r.draw_rect(Rect{0, 0, w_, h_}, kNavBg);
    draw_sidebar(r);
    if (page_ == Page::Overview) overview_.draw(r);
    else processes_.draw(r);
}

// 左侧侧边栏导航（Win11 风格）：顶部 Logo 区 + 图标/文字导航项，选中项高亮。
// 导航项内部由 Row 容器排布（图标方块 + 文字），无硬编码偏移。
// Left sidebar navigation (Win11 style): a logo header plus icon/text nav items;
// each nav item's inside is laid out by a Row container (icon block + label).
void TaskManager::draw_sidebar(Renderer& r) {
    // 顶部导航栏（横向，自绘标题栏）
    layout_.sidebar = Rect{0, 0, w_, kTopNavH};
    r.draw_rect(layout_.sidebar, kNavBg);
    r.draw_rect(Rect{0, kTopNavH - 1, w_, 1}, kDivider);

    // 顶部栏元素矩形（与 layout 一致）
    fl::Flex f;
    f.dir = fl::Dir::Row;
    f.pad = {0, 4, 8, 4};
    f.gap = 10;
    f.items = {{.size = 72}, {.size = 120}, {.size = 110}, {.flex = 1}, {.size = 220}};
    auto rc = f.layout(layout_.sidebar);
    const Rect title_rc = rc[1], endbtn_rc = rc[2], search_rc = rc[4];
    const Rect nav_box = rc[3];
    // nav_box 内：只放两个导航项（性能/进程），居中
    fl::Flex nf;
    nf.dir = fl::Dir::Row;
    nf.justify = fl::Justify::Center;
    nf.gap = 4;
    nf.items = {{.size = kNavItemW}, {.size = kNavItemW}};
    auto nrc = nf.layout(nav_box);
    layout_.nav_perf = nrc[0];
    layout_.nav_proc = nrc[1];

    // sidebar 大框：只包住两个导航项（性能/进程），上下各留 2px
    const float sb_w = kNavItemW + nf.gap + kNavItemW;
    const Rect sb_box{nrc[0].x, nav_box.y - 2, sb_w, nav_box.h + 4};
    r.draw_rounded_rect(sb_box, 12, kCardBg);
    r.draw_rounded_outline(sb_box, 12, kBorder, 1);

    // 红绿灯右侧的"任务管理器"标题
    draw_text_in_rect(r, title_rc, tr("猫头鹰监看器", "Owl Monitor"), 15, kText, 0);
    // 搜索输入框与结束进程按钮：仅进程页显示，性能页隐藏
    if (page_ == Page::Processes) {
        // 搜索输入框（最右，圆角16、高32）；记录矩形并支持输入
        search_rect_ = search_rc;
        r.draw_rounded_rect(search_rc, 16, search_focus_ ? kSelBg : kHoverBg);
        const std::string shown = search_text_.empty() ? std::string(tr("搜索名称、发布者或 PID", "Search name, publisher or PID")) : search_text_;
        draw_text_in_rect(r, Rect{search_rc.x + 12, search_rc.y, search_rc.w - 12, search_rc.h},
                          shown, 12, search_text_.empty() ? kDimText : kText, 0);
        if (search_focus_) {
            const float tw = r.font().font_text_width(shown.c_str(), 12);
            r.draw_rect(Rect{search_rc.x + 12 + tw, search_rc.y + 8, 1, search_rc.h - 16}, kText);
#if defined(__APPLE__)
            // 让输入法候选框定位到搜索框光标处（窗口内点坐标）；锚点放在搜索框底部，
            // 使 macOS 候选框出现在输入框下方。
            platform_set_ime_rect(search_rc.x + 12 + tw, search_rc.y + search_rc.h, 2, 1);
#endif
        }
        // 结束进程按钮（顶部栏最右，圆角16、高32）；未选中进程时置灰，选中后高亮可点
        endbtn_rect_ = endbtn_rc;
        const bool has_sel = (sel_pid_ >= 0);
        r.draw_rounded_rect(endbtn_rc, 16, has_sel ? (hover_endbtn_ ? kSelBg : kHoverBg) : kCardBg);
        draw_text_in_rect(r, endbtn_rc, tr("结束进程", "End process"), 13, has_sel ? kText : kDimText, 1);
    }

    // 白色选中矩形：随 nav_anim_ 在两个导航项之间平滑移动
    const float sel_x = layout_.nav_perf.x + (layout_.nav_proc.x - layout_.nav_perf.x) * nav_anim_;
    const float sel_h = kTopNavH - 8;
    const float sel_y = (kTopNavH - sel_h) * 0.5f;
    r.draw_rounded_rect(Rect{sel_x, sel_y, layout_.nav_perf.w, sel_h}, 16, Color{1.0f, 1.0f, 1.0f, 0.92f});

    // 导航项：仅文字，水平居中；选中项文字用深色（白底可读）
    auto draw_nav = [&](Rect rc, const std::string& label, bool active, bool hover) {
        (void)hover;
        fl::Flex nf;
        nf.dir = fl::Dir::Row;
        nf.pad = {12, 0, 12, 0};
        nf.align = fl::Align::Center;
        nf.justify = fl::Justify::Center;
        nf.items = {{.flex = 1, .self = fl::Align::Center}};
        auto in = nf.layout(rc);
        const Color txt = active ? Color{0.12f, 0.12f, 0.13f, 1.0f} : kSubText;
        draw_text_in_rect(r, in[0], label, 14, txt, 1);
    };
    draw_nav(layout_.nav_perf, tr("性能", "Performance"), page_ == Page::Overview, hover_nav_perf_);
    draw_nav(layout_.nav_proc, tr("进程", "Processes"), page_ == Page::Processes, hover_nav_proc_);
}




