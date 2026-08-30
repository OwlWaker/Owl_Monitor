#include "taskmgr/taskmgr.hpp"
#include "taskmgr/taskmgr_internal.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <signal.h>

// 生命周期 / 布局 / 输入：初始化、窗口尺寸变化重排、每帧更新（鼠标/滚轮/低频刷新）。
// Lifecycle, layout and input handling.

bool TaskManager::init() {
    const bool ok = sampler_.init();
    // 一次性读取 CPU / GPU 硬件信息（核心、缓存、架构等）
    // Read the CPU / GPU hardware info once (cores, cache, architecture)
    sys::sample_cpu_info(cpu_);
    sys::sample_gpu_info(gpu_);
    // 立即采样一次，让首屏就有进程列表与总体数据（不必等 1 秒刷新）
    // Sample once so the first frame already has process data and overview stats
    sampler_.sample_overview(ov_);
    sampler_.sample_procs(procs_);
    apply_sort();
    return ok;
}

void TaskManager::apply_sort() {
    std::sort(procs_.begin(), procs_.end(), [&](const sys::ProcInfo& a, const sys::ProcInfo& b) {
        int cmp = 0;
        switch (sort_col_) {
            case 0: case 2: cmp = a.name.compare(b.name); break;
            case 1: cmp = (a.pid < b.pid) ? -1 : (a.pid > b.pid ? 1 : 0); break;
            case 3: cmp = (a.cpu_percent < b.cpu_percent) ? -1 : (a.cpu_percent > b.cpu_percent ? 1 : 0); break;
            case 4: cmp = (a.mem_bytes < b.mem_bytes) ? -1 : (a.mem_bytes > b.mem_bytes ? 1 : 0); break;
            case 5: { const double da = a.disk_read_bs + a.disk_write_bs, db = b.disk_read_bs + b.disk_write_bs; cmp = (da < db) ? -1 : (da > db ? 1 : 0); break; }
        }
        return sort_asc_ ? (cmp < 0) : (cmp > 0);
    });
    // 排序后按选中 PID 重新定位行索引，保证高亮与结束目标保持一致
    // Re-locate the selected row index by PID after sorting, so the highlight
    // and the end-process target stay consistent.
    if (sel_pid_ >= 0) {
        sel_proc_row_ = -1;
        for (size_t i = 0; i < procs_.size(); ++i)
            if (procs_[i].pid == sel_pid_) { sel_proc_row_ = (int)i; break; }
    }
}

// 原生搜索输入框文本变化回调入口：更新搜索关键字并标记需要重绘。
// Search-text change entry from the native field: update the keyword and request a repaint.
void TaskManager::set_search_text(const char* text) {
    const std::string s = text ? text : "";
    if (s != search_text_) {
        search_text_ = s;
        search_changed_ = true;
    }
}

void TaskManager::resize(float width, float height) {
    w_ = width; h_ = height;
    layout();
}

// 布局：由 UiLayout 计算各区域矩形（顶部导航栏 + 内容区 + 性能页分区）
// Layout: delegate the region rect computation to UiLayout
void TaskManager::layout() {
    layout_.compute(w_, h_);
}

bool TaskManager::update(float mx, float my, bool mouse_down, bool mouse_pressed, double scroll_delta, float dt) {
    (void)mouse_down;
    bool repaint = false;

    // 原生搜索框文本变化时立即触发一次重绘，反映最新过滤结果
    // Repaint as soon as the native search text changed, to reflect the new filter.
    if (search_changed_) {
        search_changed_ = false;
        repaint = true;
    }

    // 结束进程按钮矩形：进程页顶部栏右侧（draw_sidebar 中每帧赋值；此处兜底保证首帧可命中）
    // Ensure the end-process button rect is valid even before the first draw
    if (endbtn_rect_.w <= 0 && w_ > 0) {
        fl::Flex f;
        f.dir = fl::Dir::Row;
        f.pad = {0, 4, 8, 4};
        f.gap = 10;
        f.items = {{.size = 72}, {.size = 120}, {.size = 110}, {.flex = 1}, {.size = 220}};
        auto rc = f.layout(Rect{0, 0, w_, kTopNavH});
        endbtn_rect_ = rc[4];
    }

    // 结束进程：轮询已发 SIGTERM 的进程是否已退出；超时仍存活则弹出"强制结束"确认 sheet
    // Poll whether a SIGTERM'd process has exited; if still alive after the timeout, ask to force-kill it.
    if (pending_pid_ >= 0 && end_stage_ == EndStage::None) {
        pending_timer_ += dt;
        if (pending_timer_ >= 2.0f) {
            if (sys::process_exists(pending_pid_)) {
                end_pid_ = pending_pid_;
                end_msg_ = std::string(sys::tr("进程未退出，是否强制结束？", "The process did not exit. Force end?")) + " (PID " + std::to_string(end_pid_) + ")?";
                end_stage_ = EndStage::ForceConfirm;
                platform_confirm_sheet(
                    sys::tr("强制结束", "Force end"), end_msg_.c_str(),
                    sys::tr("强制结束", "Force end"), sys::tr("取消", "Cancel"),
                    &TaskManager::end_confirm_done, this);
                pending_pid_ = -1;
            } else {
                sel_pid_ = -1; sel_proc_row_ = -1;
                pending_pid_ = -1;
            }
        }
    }

    // 结束进程流程：确认 sheet 由平台层以原生 NSAlert 异步弹出（非阻塞），无需在此拦截输入；
    // 结果通过 end_confirm_done 回调（主线程）继续，sheet 本身为模态会屏蔽窗口内交互。
    // 顶部导航选中白色矩形滑动动画：指数平滑逼近目标索引
    // Smoothly move the selection rectangle toward the target nav index
    {
        const float target = (page_ == Page::Overview) ? 0.0f : 1.0f;
        constexpr float kNavSpeed = 14.0f;
        nav_anim_ += (target - nav_anim_) * std::min(1.0f, kNavSpeed * dt);
        if (std::fabs(nav_anim_ - target) < 0.001f) nav_anim_ = target;
        else repaint = true;   // 导航滑块动画未结束，需要重绘
    }

    // 记录按下点；在顶部导航栏按住并移动超过阈值时触发窗口拖动
    // Record the press point; drag the window when holding in the top bar and moving
    if (mouse_pressed) { press_mx_ = mx; press_my_ = my; dragging_win_ = false; }
    if (mouse_down && !dragging_win_ && hit(layout_.sidebar, mx, my) &&
        (std::fabs(mx - press_mx_) > 3.0f || std::fabs(my - press_my_) > 3.0f)) {
        dragging_win_ = true;
        platform_drag_window(mx, my);
    }

    // 顶部导航项悬停与点击
    hover_nav_perf_ = hit(layout_.nav_perf, mx, my);
    hover_nav_proc_ = hit(layout_.nav_proc, mx, my);
    // 性能页设备列表项悬停
    hover_dev_ = -1;
    for (int i = 0; i < (int)layout_.dev_items.size(); ++i)
        if (hit(layout_.dev_items[i], mx, my)) { hover_dev_ = i; break; }

    if (mouse_pressed) {
        // 切换页面
        if (hit(layout_.nav_perf, mx, my)) page_ = Page::Overview;
        else if (hit(layout_.nav_proc, mx, my)) page_ = Page::Processes;
        // 性能页切换设备
        else if (page_ == Page::Overview && hover_dev_ >= 0) dev_sel_ = hover_dev_;
    }

    // 进程页：分组折叠 + 进程行悬停/选中
    if (page_ == Page::Processes) {
        // 点击表头列：首次点击从大到小（降序），再次点击切换升/降
        if (mouse_pressed) {
            for (int c = 0; c < (int)head_cols_.size(); ++c)
                if (hit(head_cols_[c], mx, my)) {
                    if (sort_col_ == c) sort_asc_ = !sort_asc_;
                    else { sort_col_ = c; sort_asc_ = false; }
                    apply_sort();
                }
        }
        // 悬停：分组标题 / 进程行
        hover_group_front_ = hit(group_front_hit_, mx, my);
        hover_group_bg_    = hit(group_bg_hit_, mx, my);
        hover_proc_row_ = -1;
        if (mouse_pressed) {
            if (hover_group_front_) group_front_open_ = !group_front_open_;
            else if (hover_group_bg_) group_bg_open_ = !group_bg_open_;
            else {
                for (auto& pr : proc_row_hit_)
                    if (hit(pr.first, mx, my)) {
                        sel_proc_row_ = pr.second;
                        sel_pid_ = procs_[pr.second].pid;
                        break;
                    }
            }
        } else {
            for (auto& pr : proc_row_hit_)
                if (hit(pr.first, mx, my)) { hover_proc_row_ = pr.second; break; }
        }
    }

    // 进程页滚轮滚动，按行数钳制范围
    if (page_ == Page::Processes && scroll_delta != 0) {
        scroll_ -= (float)scroll_delta * 50.0f;
        // 列表顶 = 表头；含总计行 + 两个分组标题行各 32px
        const float list_top = layout_.content.y + row_h_ + 2;
        const float list_h = layout_.content.h - (list_top - layout_.content.y) - 8;
        // 按折叠状态 + 搜索过滤计算实际显示行数，避免折叠/过滤后滚出空白
        std::string q = search_text_;
        for (char& ch : q) ch = (char)std::tolower((unsigned char)ch);
        int front_count = 0, bg_count = 0;
        for (auto& p : procs_) {
            if (!search_text_.empty()) {
                std::string nm = p.name;
                for (char& ch : nm) ch = (char)std::tolower((unsigned char)ch);
                if (nm.find(q) == std::string::npos && std::to_string(p.pid).find(search_text_) == std::string::npos)
                    continue;
            }
            (p.is_app ? front_count : bg_count)++;
        }
        const float content_h = row_h_ * (1.0f +
            (group_front_open_ ? (float)front_count : 0.0f) +
            (group_bg_open_   ? (float)bg_count   : 0.0f)) + 2 * 32.0f;
        // 列表顶起始有 +2 偏移，max_scroll 需一并计入，否则滚到底部内容会超出裁剪区 2px
        const float max_scroll = std::max(0.0f, content_h + 2 - list_h);
        if (scroll_ < 0) scroll_ = 0;
        if (scroll_ > max_scroll) scroll_ = max_scroll;
    }

    // 低频刷新：性能页/总体/历史与进程列表每 2 秒
    // Low-frequency refresh: overview/history and process list every 2 s
    refresh_timer_ += dt;
    if (refresh_timer_ >= 2.0f) {
        refresh_timer_ = 0;
        repaint = true;   // 数据刷新，界面需更新
        sampler_.sample_overview(ov_);
        // 每核使用率推入历史（平滑；逻辑核数变化时重建缓冲）
        std::vector<double> cores;
        if (sampler_.sample_cores(cores)) {
            if (hist_.core_hist.size() != cores.size()) {
                hist_.core_hist.clear();
                hist_.core_hist.resize(cores.size());
            }
            // 总体 CPU 使用率 = 所有逻辑核平均（推入单独历史用于大曲线）
            double cpu_sum = 0;
            for (size_t i = 0; i < cores.size(); ++i) { History::push_smooth(hist_.core_hist[i], (float)cores[i]); cpu_sum += cores[i]; }
            History::push_smooth(hist_.cpu_hist, (float)(cores.empty() ? 0.0 : cpu_sum / (double)cores.size()));
        }
        // GPU 总体使用率推入历史（获取失败则沿用上次值）
        const double gu = sys::sample_gpu_utilization();
        if (gu >= 0) hist_.gpu_util = (float)gu;
        History::push_smooth(hist_.gpu_hist, hist_.gpu_util);
        // 磁盘读/写速率推入历史（字节/秒，绘制时按最大值归一化）
        History::push_smooth(hist_.disk_read_hist, (float)ov_.disk_read_bs);
        History::push_smooth(hist_.disk_write_hist, (float)ov_.disk_write_bs);
        // 内存占用率与细分（活跃/非活跃/固定/压缩）同样平滑记录
        History::push_smooth(hist_.mem_hist, (float)ov_.mem_percent);
        const float mt = (float)std::max(1.0, ov_.mem_total);
        History::push_smooth(hist_.mem_app_hist,        (float)(ov_.mem_app / mt * 100.0));
        History::push_smooth(hist_.mem_wired_hist,      (float)(ov_.mem_wired / mt * 100.0));
        History::push_smooth(hist_.mem_compressed_hist, (float)(ov_.mem_compressed / mt * 100.0));
        History::push_smooth(hist_.mem_cached_hist,     (float)(ov_.mem_cached / mt * 100.0));
        // 累计采样次数 +1：竖线网格随每次采样往左推进一个数据点宽度
        ++hist_.sample_total;
    }
    proc_timer_ += dt;
    if (proc_timer_ >= 2.0f) {
        proc_timer_ = 0;
        repaint = true;   // 进程列表更新
        sampler_.sample_procs(procs_);
        apply_sort();
        // 选中的进程若已退出/被结束则清除选中（用 PID 判断，避免索引错位）
        if (sel_pid_ >= 0) {
            bool found = false;
            for (auto& p : procs_) if (p.pid == sel_pid_) { found = true; break; }
            if (!found) { sel_pid_ = -1; sel_proc_row_ = -1; }
        }
    }
    return repaint;
}

// 原生确认 sheet 回调（主线程）：根据用户选择继续结束进程流程。
// Native confirm sheet callback (main thread): continue the end-process flow.
void TaskManager::end_confirm_done(int choice, void* user) {
    TaskManager* tm = static_cast<TaskManager*>(user);
    if (!tm) return;
    if (tm->end_stage_ == EndStage::Confirm) {
        if (choice == 1) {
            int err = 0;
            sys::terminate_process(tm->end_pid_, SIGTERM, &err);
            if (err == 0) { tm->pending_pid_ = tm->end_pid_; tm->pending_timer_ = 0; }
            else { tm->sel_pid_ = -1; tm->sel_proc_row_ = -1; }
        }
        tm->end_pid_ = -1;
        tm->end_stage_ = EndStage::None;
    } else if (tm->end_stage_ == EndStage::ForceConfirm) {
        if (choice == 1) sys::terminate_process(tm->end_pid_, SIGKILL);
        tm->end_pid_ = -1;
        tm->pending_pid_ = -1;
        tm->sel_pid_ = -1; tm->sel_proc_row_ = -1;
        tm->end_stage_ = EndStage::None;
    }
}

// 原生结束进程按钮点击：进程页且已选中进程、无进行中流程时，弹出原生确认框。
// Native end-process button click: on the processes page with a selected process and no
// in-flight flow, show the native confirmation sheet.
void TaskManager::on_end_button_clicked() {
    if (page_ != Page::Processes) return;
    if (end_stage_ != EndStage::None) return;
    if (sel_pid_ < 0) return;
    std::string nm;
    for (auto& p : procs_) if (p.pid == sel_pid_) { nm = p.name; break; }
    end_pid_ = sel_pid_;
    end_msg_ = std::string(sys::tr("确定要结束进程", "End process")) + " \"" + nm + "\" (" + std::to_string(sel_pid_) + ")?";
    end_stage_ = EndStage::Confirm;
    platform_confirm_sheet(
        sys::tr("结束进程", "End process"), end_msg_.c_str(),
        sys::tr("结束", "End"), sys::tr("取消", "Cancel"),
        &TaskManager::end_confirm_done, this);
}
