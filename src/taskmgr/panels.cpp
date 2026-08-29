#include "taskmgr/panels.hpp"
#include "taskmgr/taskmgr.hpp"
#include "taskmgr/taskmgr_internal.hpp"
#include "sys/system.hpp"

#include <algorithm>
#include <string>

// 界面语言翻译帮助（由 sys/system.hpp 提供，跟随系统语言）
using ::sys::tr;

// 性能页面板：设备列表 + 选中设备详情。
// Overview panel: device list plus the selected device detail.

void OverviewPanel::draw(Renderer& r) {
    auto& dev_sel_ = owner_.dev_sel_;
    auto& cpu_ = owner_.cpu_;
    auto& ov_ = owner_.ov_;
    auto& gpu_ = owner_.gpu_;
    auto& layout_ = owner_.layout_;

    // 无标题栏：设备列表直接绘制；详情区顶部自绘设备名小标题
    draw_device_list(r);
    std::string dev_title =
        dev_sel_ == 0 ? cpu_.model :
        dev_sel_ == 1 ? fmt_bytes(ov_.mem_total) :
        dev_sel_ == 2 ? tr("磁盘", "Disk") :
        gpu_.name;
    draw_text_in_rect(r, Rect{layout_.dev_detail.x, layout_.dev_detail.y, layout_.dev_detail.w, 30}, dev_title, 18, kText, 0);
    draw_device_detail(r, Rect{layout_.dev_detail.x, layout_.dev_detail.y + 36, layout_.dev_detail.w, layout_.dev_detail.h - 36});
}

void OverviewPanel::draw_device_list(Renderer& r) {
    auto& hist_ = owner_.hist_;
    auto& ov_ = owner_.ov_;
    auto& layout_ = owner_.layout_;
    auto& dev_sel_ = owner_.dev_sel_;
    auto& charts_ = owner_.charts_;

    const char* names[4] = {"CPU", tr("内存", "Memory"), tr("磁盘", "Disk"), "GPU"};
    // CPU 当前值：取所有逻辑核平均；无数据则显示 0
    double cpu_t = 0;
    if (!hist_.core_hist.empty()) {
        double sum = 0; int n = 0;
        for (auto& c : hist_.core_hist) { if (!c.empty()) { sum += c.back(); ++n; } }
        if (n) cpu_t = sum / n;
    }
    const double mem_t = ov_.mem_percent;
    const double disk_t = ov_.disk_read_bs + ov_.disk_write_bs;  // 磁盘总速率（B/s）
    const double gpu_t = hist_.gpu_util;
    const double cur[4] = {cpu_t, mem_t, disk_t, gpu_t};

    for (int i = 0; i < 4; ++i) {
        const Rect it = layout_.dev_items[i];
        const bool sel = (i == dev_sel_);
        if (sel) r.draw_rounded_rect(it, 8, kSelBg);
        if (sel) r.draw_rounded_rect_ex(Rect{it.x, it.y + 8, 3, it.h - 16}, 1.5f, 1.5f, 1.5f, 1.5f, kAccent);

        // 项内布局：Row 容器（左侧缩略图 + 右侧文字列），无硬编码偏移
        fl::Flex f;
        f.dir = fl::Dir::Row;
        f.pad = {12, 9, 12, 9};
        f.gap = 12;
        f.align = fl::Align::Start;
        f.items = {{.size = 88, .cross = 52, .self = fl::Align::Start}, {.flex = 1}};
        auto in = f.layout(it);
        const Rect thumb = in[0], text_col = in[1];
        r.draw_rounded_rect(thumb, 6, kBoxBg);
        r.draw_rounded_outline(thumb, 6, kBorder, 1);
        // 缩略图内迷你曲线：图元内边距（命名常量）
        const Rect mini{thumb.x + kMiniPadX, thumb.y + kMiniPadY,
                        thumb.w - kMiniPadX * 2, thumb.h - kMiniPadY * 2};
        if (i == 0) {
            // CPU 迷你曲线：整合所有逻辑核历史为总体
            std::vector<float> agg;
            if (!hist_.core_hist.empty()) {
                const size_t n = hist_.core_hist[0].size();
                agg.resize(n, 0);
                int cnt = 0;
                for (auto& ch : hist_.core_hist) { if ((int)ch.size() != (int)n) continue; for (size_t k = 0; k < n; ++k) agg[k] += ch[k]; ++cnt; }
                if (cnt > 1) for (auto& v : agg) v /= (float)cnt;
            }
            charts_.draw_area_line(r, mini, agg, kBlue);
        } else if (i == 1) {
            charts_.draw_area_line(r, mini, hist_.mem_hist, kPurple);
        } else if (i == 2) {
            // 磁盘迷你曲线：读 + 写速率合并
            std::vector<float> dsum;
            const size_t n = std::max(hist_.disk_read_hist.size(), hist_.disk_write_hist.size());
            dsum.resize(n, 0);
            for (size_t k = 0; k < hist_.disk_read_hist.size(); ++k) dsum[k] += hist_.disk_read_hist[k];
            for (size_t k = 0; k < hist_.disk_write_hist.size(); ++k) dsum[k] += hist_.disk_write_hist[k];
            charts_.draw_rate_line(r, mini, dsum, kGreen);
        } else {
            charts_.draw_area_line(r, mini, hist_.gpu_hist, kOrange);
        }

        // 右侧文字列：Column 容器（设备名 + 当前值）
        fl::Flex tc;
        tc.dir = fl::Dir::Column;
        tc.items = {{.size = 22}, {.size = 20, .mt = 3}};
        auto t = tc.layout(text_col);
        draw_text_in_rect(r, t[0], names[i], 15, sel ? kText : kSubText, 0);
        // 磁盘项显示速率，其余显示百分比
        draw_text_in_rect(r, t[1], i == 2 ? fmt_bytes(cur[i]) + "/s" : fmt_percent(cur[i]), 14, kText, 0);
    }
}

void OverviewPanel::draw_device_detail(Renderer& r, Rect rc) {
    auto& dev_sel_ = owner_.dev_sel_;
    auto& cpu_ = owner_.cpu_;
    auto& ov_ = owner_.ov_;
    auto& gpu_ = owner_.gpu_;
    auto& hist_ = owner_.hist_;
    auto& charts_ = owner_.charts_;

    if (dev_sel_ == 0) {
        // ===== CPU 详情：标签 + 核心网格（每个核带滚动网格）+ 底部统计 =====
        const float stat_cell_h = kStatLabelH + kStatGap + kStatValueH;
        fl::Flex col;
        col.dir = fl::Dir::Column;
        col.pad = {4, 6, 4, 0};
        col.items = {{.size = 20, .mb = 4}, {.flex = 1, .mb = 20},
                     {.size = 2 * stat_cell_h + kStatRowGap}};
        auto c = col.layout(rc);
        const Rect label_row = c[0], grid = c[1], stats = c[2];

        fl::Flex lr;
        lr.dir = fl::Dir::Row;
        lr.justify = fl::Justify::SpaceBetween;
        lr.items = {{.size = 320}, {.size = 26}};
        auto l = lr.layout(label_row);
        draw_text_in_rect(r, l[0], tr("% 过去 60 秒的使用率", "% utilization over the past 60 s"), 13, kDimText, 0);
        draw_text_in_rect(r, l[1], "...", 16, kDimText, 2);

        const int total = cpu_.s_cores + cpu_.p_cores + cpu_.e_cores;
        charts_.draw_core_grid(r, grid, total, cpu_.s_cores, cpu_.p_cores);

        fl::Flex srow;
        srow.dir = fl::Dir::Row;
        srow.items = {{.flex = 1}, {.flex = 1}, {.flex = 1}, {.flex = 1}};
        auto stat = [&](const Rect& cell, const std::string& label, const std::string& val) {
            draw_text_in_rect(r, Rect{cell.x, cell.y, cell.w, kStatLabelH}, label, 13, kDimText, 0);
            draw_text_in_rect(r, Rect{cell.x, cell.y + kStatLabelH + kStatGap, cell.w, kStatValueH}, val, 20, kText, 0);
        };
        auto r1 = srow.layout(Rect{stats.x, stats.y, stats.w, stat_cell_h});
        stat(r1[0], tr("利用率", "Utilization"), fmt_percent(hist_.cpu_hist.empty() ? 0 : hist_.cpu_hist.back()));
        stat(r1[1], tr("核心", "Cores"),   std::to_string(cpu_.physical));
        stat(r1[2], tr("逻辑处理器", "Logical processors"), std::to_string(cpu_.logical));
        stat(r1[3], tr("进程", "Processes"), std::to_string(ov_.proc_count));
        std::string core_label, core_count;
        auto append_core = [&](int n, const char* tag) {
            if (n <= 0) return;
            if (!core_label.empty()) { core_label += " / "; core_count += " / "; }
            core_label += tag; core_count += std::to_string(n);
        };
        append_core(cpu_.s_cores, "S");
        append_core(cpu_.p_cores, "P");
        append_core(cpu_.e_cores, "E");
        core_label += tr(" 核", " cores");
        auto r2 = srow.layout(Rect{stats.x, stats.y + kStatRowGap + stat_cell_h, stats.w, stat_cell_h});
        auto stat2 = [&](const Rect& cell, const std::string& label, const std::string& val) {
            draw_text_in_rect(r, Rect{cell.x, cell.y, cell.w, kStatLabelH}, label, 13, kDimText, 0);
            draw_text_in_rect(r, Rect{cell.x, cell.y + kStatLabelH + kStatGap, cell.w, kStatValueH}, val, 20, kText, 0);
        };
        stat2(r2[0], tr("L1 缓存", "L1 cache"), fmt_bytes(cpu_.cache.l1d));
        stat2(r2[1], tr("L2 缓存", "L2 cache"), cpu_.cache.l2 > 0 ? fmt_bytes(cpu_.cache.l2) : "—");
        stat2(r2[2], tr("L3 缓存", "L3 cache"), cpu_.cache.l3 > 0 ? fmt_bytes(cpu_.cache.l3) : "—");
        stat2(r2[3], core_label, core_count);
    } else if (dev_sel_ == 1) {
        // ===== 内存详情 =====
        fl::Flex col;
        col.dir = fl::Dir::Column;
        col.pad = {4, 6, 4, 0};
        col.items = {{.size = 20, .mb = 4},       // "内存使用"
                     {.flex = 1, .mb = 26},       // 使用大图
                     {.size = 18, .mb = 4},       // "内存构成"
                     {.size = 28, .mb = 18},      // 构成条
                     {.size = 3 * kMemRowH}};     // 统计区（三行）
        auto c = col.layout(rc);
        const Rect usage_label = c[0], usage = c[1], comp_label = c[2], comp = c[3], stats = c[4];
        draw_text_in_rect(r, usage_label, tr("内存使用", "Memory usage"), 13, kDimText, 0);

        r.draw_rounded_rect(usage, 2, kBoxBg);
        charts_.draw_reflines(r, usage, (int)hist_.mem_hist.size());
        charts_.draw_area_line(r, usage, hist_.mem_hist, kPurple);
        draw_text_in_rect(r, Rect{usage.x + usage.w - 80, usage.y - 4, 76, 18}, fmt_bytes(ov_.mem_total), 12, kText, 2);
        draw_text_in_rect(r, Rect{usage.x, usage.y + usage.h + 2, 100, 16}, tr("60 秒", "60 s"), 11, kDimText, 0);
        draw_text_in_rect(r, Rect{usage.x + usage.w - 30, usage.y + usage.h + 2, 30, 16}, "0", 11, kDimText, 2);

        draw_text_in_rect(r, comp_label, tr("内存构成", "Memory composition"), 13, kDimText, 0);
        const float mt = (float)std::max(1.0, ov_.mem_total);
        const float fApp = (float)(ov_.mem_app / mt), fWired = (float)(ov_.mem_wired / mt);
        const float fComp = (float)(ov_.mem_compressed / mt), fCached = (float)(ov_.mem_cached / mt);
        r.draw_rounded_rect(comp, 3, kTrack);
        fl::Flex segf;
        segf.dir = fl::Dir::Row;
        segf.items = {{.flex = fApp}, {.flex = fWired}, {.flex = fComp}, {.flex = fCached}};
        auto segs = segf.layout(comp);
        const Color seg_cols[4] = {kAccent, kBlue, kPurple, kGreen};
        for (int s = 0; s < 4; ++s)
            if (segs[s].w > 1) r.draw_rect(segs[s], seg_cols[s]);
        r.draw_rounded_outline(comp, 3, kBorder, 1);

        fl::Flex srow;
        srow.dir = fl::Dir::Row;
        srow.items = {{.flex = 1}, {.flex = 1}};
        const float stat_cell_h = kStatLabelH + kStatGap + kStatValueH;
        auto memstat = [&](const Rect& cell, const std::string& label, const std::string& val) {
            draw_text_in_rect(r, Rect{cell.x, cell.y, cell.w, kStatLabelH}, label, 13, kDimText, 0);
            draw_text_in_rect(r, Rect{cell.x, cell.y + kStatLabelH + kStatGap, cell.w, kStatValueH}, val, 20, kText, 0);
        };
        auto mem_row = [&](int row, const std::string& a, const std::string& b, const std::string& va, const std::string& vb) {
            auto rr = srow.layout(Rect{stats.x, stats.y + row * kMemRowH, stats.w, stat_cell_h});
            memstat(rr[0], a, va);
            memstat(rr[1], b, vb);
        };
        mem_row(0, tr("已使用 (已压缩)", "In use (compressed)"), tr("可用", "Available"),
                fmt_bytes(ov_.mem_used) + " (" + fmt_bytes(ov_.mem_compressed) + ")",
                fmt_bytes(std::max(0.0, ov_.mem_total - ov_.mem_used)));
        mem_row(1, tr("已提交", "Committed"), tr("已缓存文件", "Cached files"),
                fmt_bytes(ov_.mem_used + ov_.mem_cached) + " / " + fmt_bytes(ov_.mem_total),
                fmt_bytes(ov_.mem_cached));
        mem_row(2, tr("App 内存", "App memory"), tr("联动内存", "Wired memory"), fmt_bytes(ov_.mem_app), fmt_bytes(ov_.mem_wired));
    } else if (dev_sel_ == 2) {
        // ===== 磁盘详情 =====
        fl::Flex col;
        col.dir = fl::Dir::Column;
        col.pad = {4, 6, 4, 0};
        col.items = {{.size = 20, .mb = 4},       // "磁盘使用"
                     {.flex = 1, .mb = 26},       // 读/写速率图
                     {.size = 18, .mb = 4},       // "读 / 写速率"
                     {.size = 28, .mb = 18},      // 读/写占比条
                     {.size = 3 * kMemRowH}};     // 统计区（三行）
        auto c = col.layout(rc);
        const Rect usage_label = c[0], usage = c[1], comp_label = c[2], comp = c[3], stats = c[4];
        draw_text_in_rect(r, usage_label, tr("磁盘使用", "Disk usage"), 13, kDimText, 0);

        r.draw_rounded_rect(usage, 2, kBoxBg);
        charts_.draw_reflines(r, usage, (int)hist_.disk_read_hist.size());
        charts_.draw_rate_line(r, usage, hist_.disk_read_hist, kGreen);
        charts_.draw_rate_line(r, usage, hist_.disk_write_hist, kYellow);
        draw_text_in_rect(r, Rect{usage.x + usage.w - 100, usage.y - 4, 96, 18}, fmt_bytes(ov_.disk_read_bs + ov_.disk_write_bs) + "/s", 12, kText, 2);
        draw_text_in_rect(r, Rect{usage.x, usage.y + usage.h + 2, 100, 16}, tr("60 秒", "60 s"), 11, kDimText, 0);

        draw_text_in_rect(r, comp_label, tr("读 / 写速率", "Read / Write rate"), 13, kDimText, 0);
        const float rd = (float)ov_.disk_read_bs, wv = (float)ov_.disk_write_bs;
        const float tot = rd + wv;
        r.draw_rounded_rect(comp, 3, kTrack);
        if (tot > 0.01f) {
            const float fr = rd / tot;
            r.draw_rect(Rect{comp.x, comp.y, comp.w * fr, comp.h}, kGreen);
            r.draw_rect(Rect{comp.x + comp.w * fr, comp.y, comp.w * (1.0f - fr), comp.h}, kYellow);
        }
        r.draw_rounded_outline(comp, 3, kBorder, 1);

        fl::Flex srow;
        srow.dir = fl::Dir::Row;
        srow.items = {{.flex = 1}, {.flex = 1}};
        const float stat_cell_h = kStatLabelH + kStatGap + kStatValueH;
        auto diskstat = [&](const Rect& cell, const std::string& label, const std::string& val) {
            draw_text_in_rect(r, Rect{cell.x, cell.y, cell.w, kStatLabelH}, label, 13, kDimText, 0);
            draw_text_in_rect(r, Rect{cell.x, cell.y + kStatLabelH + kStatGap, cell.w, kStatValueH}, val, 20, kText, 0);
        };
        auto disk_row = [&](int row, const std::string& a, const std::string& b, const std::string& va, const std::string& vb) {
            auto rr = srow.layout(Rect{stats.x, stats.y + row * kMemRowH, stats.w, stat_cell_h});
            diskstat(rr[0], a, va);
            diskstat(rr[1], b, vb);
        };
        disk_row(0, tr("读速率", "Read"), tr("写速率", "Write"),
                 fmt_bytes(ov_.disk_read_bs) + "/s", fmt_bytes(ov_.disk_write_bs) + "/s");
        disk_row(1, tr("总速率", "Total"), tr("…", "…"),
                 fmt_bytes(ov_.disk_read_bs + ov_.disk_write_bs) + "/s", "");
    } else {
        // ===== GPU 详情 =====
        fl::Flex col;
        col.dir = fl::Dir::Column;
        col.pad = {4, 6, 4, 0};
        col.items = {{.size = 20, .mb = 4},       // "GPU 使用"
                     {.flex = 1, .mb = 26},       // 使用大图
                     {.size = 18, .mb = 4},       // 架构信息
                     {.size = 28, .mb = 18},      // 构成条
                     {.size = 3 * kMemRowH}};     // 统计区（三行）
        auto c = col.layout(rc);
        const Rect info_label = c[0], usage = c[1], hw_label = c[2], comp = c[3], stats = c[4];
        draw_text_in_rect(r, info_label, tr("GPU 使用", "GPU usage"), 13, kDimText, 0);

        r.draw_rounded_rect(usage, 2, kBoxBg);
        charts_.draw_reflines(r, usage, (int)hist_.gpu_hist.size());
        charts_.draw_area_line(r, usage, hist_.gpu_hist, kOrange);
        draw_text_in_rect(r, Rect{usage.x + usage.w - 80, usage.y - 4, 76, 18}, fmt_percent(hist_.gpu_util), 12, kText, 2);
        draw_text_in_rect(r, Rect{usage.x, usage.y + usage.h + 2, 100, 16}, tr("60 秒", "60 s"), 11, kDimText, 0);
        draw_text_in_rect(r, Rect{usage.x + usage.w - 30, usage.y + usage.h + 2, 30, 16}, "0", 11, kDimText, 2);

        draw_text_in_rect(r, hw_label,
                          std::string(tr("架构 · ", "Architecture · ")) + gpu_.arch +
                          (gpu_.cores > 0 ? std::string(tr(" · 核心 × ", " · Cores × ")) + std::to_string(gpu_.cores) : ""),
                          13, kDimText, 0);

        r.draw_rounded_rect(comp, 3, kTrack);
        r.draw_rounded_outline(comp, 3, kBorder, 1);
        const double gpu_mem = gpu_.mem_bytes;
        if (gpu_mem > 0) {
            const double frac = std::min(1.0, gpu_mem / std::max(1.0, ov_.mem_total));
            r.draw_rect(Rect{comp.x, comp.y, comp.w * (float)frac, comp.h}, kOrange);
        }

        fl::Flex srow;
        srow.dir = fl::Dir::Row;
        srow.items = {{.flex = 1}, {.flex = 1}};
        const float stat_cell_h = kStatLabelH + kStatGap + kStatValueH;
        auto gpustat = [&](const Rect& cell, const std::string& label, const std::string& val) {
            draw_text_in_rect(r, Rect{cell.x, cell.y, cell.w, kStatLabelH}, label, 13, kDimText, 0);
            draw_text_in_rect(r, Rect{cell.x, cell.y + kStatLabelH + kStatGap, cell.w, kStatValueH}, val, 20, kText, 0);
        };
        auto gpu_row = [&](int row, const std::string& a, const std::string& b, const std::string& va, const std::string& vb) {
            auto rr = srow.layout(Rect{stats.x, stats.y + row * kMemRowH, stats.w, stat_cell_h});
            gpustat(rr[0], a, va);
            gpustat(rr[1], b, vb);
        };
        gpu_row(0, tr("利用率", "Utilization"), tr("核心", "Cores"), fmt_percent(hist_.gpu_util),
                gpu_.cores > 0 ? std::to_string(gpu_.cores) : "—");
        gpu_row(1, tr("架构", "Architecture"), tr("显存 / 统一内存", "VRAM / Unified memory"), gpu_.arch, fmt_bytes(gpu_.mem_bytes));
        gpu_row(2, tr("名称", "Name"), tr("图形接口", "Graphics API"), gpu_.name, "Metal / Vulkan");
    }
}

// 进程页面板：表头 + 分组列表 + 滚动条。
// Processes panel: header, grouped list and scrollbar.

void ProcessesPanel::draw(Renderer& r) {
    auto& layout_ = owner_.layout_;
    auto& procs_ = owner_.procs_;
    auto& hist_ = owner_.hist_;
    auto& ov_ = owner_.ov_;
    auto& sort_col_ = owner_.sort_col_;
    auto& sort_asc_ = owner_.sort_asc_;
    auto& head_cols_ = owner_.head_cols_;
    auto& search_text_ = owner_.search_text_;
    auto& scroll_ = owner_.scroll_;
    auto& row_h_ = owner_.row_h_;
    auto& group_front_open_ = owner_.group_front_open_;
    auto& group_bg_open_ = owner_.group_bg_open_;
    auto& group_front_hit_ = owner_.group_front_hit_;
    auto& group_bg_hit_ = owner_.group_bg_hit_;
    auto& sel_proc_row_ = owner_.sel_proc_row_;
    auto& proc_row_hit_ = owner_.proc_row_hit_;

    const float x0 = layout_.content.x, w = layout_.content.w;

    auto col_rects = [&](float y) {
        fl::Flex cols;
        cols.dir = fl::Dir::Row;
        cols.pad = {24, 0, 24, 0};
        cols.items = {{.flex = 1}, {.size = 60}, {.size = 180}, {.size = 90},
                      {.size = 110}, {.size = 80}};
        return cols.layout(Rect{x0, y, w, row_h_});
    };

    double cpu_avg = 0;
    if (!hist_.core_hist.empty()) {
        double sum = 0; int n = 0;
        for (auto& c : hist_.core_hist) { if (!c.empty()) { sum += c.back(); ++n; } }
        if (n) cpu_avg = sum / n;
    }
    double total_disk = 0;
    for (auto& p : procs_) total_disk += p.disk_read_bs + p.disk_write_bs;

    const float head_top = layout_.content.y;
    const Rect head{x0, head_top, w, row_h_};
    r.draw_rect(head, kHeadBg);
    auto hd = col_rects(head.y);
    const char* col_names[6] = {tr("名称", "Name"), "PID", tr("进程名", "Process name"), "CPU", tr("内存", "Memory"), tr("磁盘", "Disk")};
    for (int c = 0; c < 6; ++c) {
        std::string label = col_names[c];
        if (c == sort_col_) label += sort_asc_ ? " ▲" : " ▼";
        draw_text_in_rect(r, hd[c], label, 14, kSubText, c == 0 ? 0 : 1);
    }
    head_cols_.assign(hd.begin(), hd.end());
    r.draw_rect(Rect{x0, head.y + row_h_, w, 1}, kDivider);

    double max_cpu = 0, max_mem = 0;
    for (auto& p : procs_) {
        if (p.cpu_percent > max_cpu) max_cpu = p.cpu_percent;
        if (p.mem_bytes > max_mem) max_mem = p.mem_bytes;
    }

    std::string q = search_text_;
    for (char& ch : q) ch = (char)std::tolower((unsigned char)ch);
    std::vector<int> front_idx, bg_idx;
    for (int i = 0; i < (int)procs_.size(); ++i) {
        if (!search_text_.empty()) {
            std::string nm = procs_[i].name;
            for (char& ch : nm) ch = (char)std::tolower((unsigned char)ch);
            if (nm.find(q) == std::string::npos &&
                std::to_string(procs_[i].pid).find(search_text_) == std::string::npos)
                continue;
        }
        (procs_[i].is_app ? front_idx : bg_idx).push_back(i);
    }

    const float list_top = head.y + row_h_ + 2;
    const float group_h = 32;
    const Rect list_rc{x0, list_top, w, layout_.content.h - (list_top - layout_.content.y) - 8};
    r.set_scissor(list_rc);
    r.draw_rect(list_rc, kRowBg);  // 整块列表背景（浅色），其余行只画深色交替，减少矩形数

    proc_row_hit_.clear();
    group_front_hit_ = Rect{}; group_bg_hit_ = Rect{};

    auto draw_group_header = [&](const Rect& rc, const std::string& label, bool open) {
        r.draw_rect(rc, kHeadBg);
        fl::Flex gf;
        gf.dir = fl::Dir::Row;
        gf.pad = {10, 0, 10, 0};
        gf.items = {{.size = 20, .mr = 2}, {.flex = 1}};
        auto g = gf.layout(rc);
        const char* arrow = open ? "▼" : "▶";
        draw_text_in_rect(r, g[0], arrow, 12, kSubText, 1);
        draw_text_in_rect(r, g[1], label, 14, kText, 0);
    };

    auto draw_proc_row = [&](const sys::ProcInfo& p, int gi, float ry, int row_no) {
        const Rect row{x0, ry, w, row_h_};
        proc_row_hit_.push_back({row, gi});
        const bool sel = (gi == sel_proc_row_);
        if (sel) r.draw_rect(row, kSelBg);
        else if (row_no & 1) r.draw_rect(row, kRowAltBg);  // 偶数行沿用整块背景，只画深色行
        auto cl = col_rects(ry);
        fl::Flex nf;
        nf.dir = fl::Dir::Row;
        nf.pad = {12, 0, 0, 0};
        nf.align = fl::Align::Center;
        nf.items = {{.size = 20, .mr = 4},
                    {.size = 16, .cross = 16, .self = fl::Align::Center, .mr = 8},
                    {.flex = 1}};
        auto nm = nf.layout(cl[0]);
        draw_text_in_rect(r, nm[0], "▶", 11, kDimText, 1);
        const uint32_t* icon_px = nullptr; int iw = 0, ih = 0;
        if (sys::app_icon_rgba((int)p.pid, icon_px, iw, ih))
            r.draw_bitmap_rgba(nm[1].x, nm[1].y, icon_px, iw, ih);
        else
            r.draw_rounded_rect(nm[1], 4, p.is_app ? kAccent : kSubText);
        if (p.frontmost) {
            const float dot = 7.0f;
            r.draw_rounded_rect_ex(Rect{nm[1].x + nm[1].w - dot + 2, nm[1].y - 3, dot, dot},
                                   3.5f, 3.5f, 3.5f, 3.5f, kGreen);
        }
        const bool missing = p.name.empty();
        const char* na = "N/A";
        draw_text_in_rect(r, nm[2], missing ? na : p.name, 14, kText, 0);
        draw_text_in_rect(r, cl[1], std::to_string(p.pid), 13, kSubText, 1);
        draw_text_in_rect(r, cl[2], missing ? na : p.name, 13, kDimText, 1);
        {
            const float t = max_cpu > 0 ? (float)std::min(1.0, p.cpu_percent / max_cpu) : 0.0f;
            if (!missing && t > 0.01f) {
                Color fc = usage_color(std::min(1.0, p.cpu_percent / 100.0));
                r.draw_rect(cl[3], Color{fc.r, fc.g, fc.b, t * 0.75f});
            }
            draw_text_in_rect(r, cl[3], missing ? na : fmt_percent(p.cpu_percent), 13, kText, 1);
        }
        {
            const float t = max_mem > 0 ? (float)std::min(1.0, p.mem_bytes / max_mem) : 0.0f;
            if (!missing && t > 0.01f)
                r.draw_rect(cl[4], Color{0.255f, 0.608f, 0.980f, t * 0.75f});
            draw_text_in_rect(r, cl[4], missing ? na : fmt_bytes(p.mem_bytes), 13, kSubText, 1);
        }
        draw_text_in_rect(r, cl[5], missing ? na : fmt_bytes(p.disk_read_bs + p.disk_write_bs) + "/s", 13, kDimText, 1);
    };

    float yy = list_rc.y - scroll_ + 2;
    int row_no = 0;  // 列表行的显示序号（用于行背景交替，不随进程排序索引变化）
    {
        const Rect row{x0, yy, w, row_h_};
        r.draw_rect(row, kRowAltBg);
        auto cl = col_rects(yy);
        draw_text_in_rect(r, cl[0], tr("总计", "Total"), 14, kText, 0);
        draw_text_in_rect(r, cl[3], fmt_percent(cpu_avg), 14, kText, 1);
        draw_text_in_rect(r, cl[4], fmt_percent(ov_.mem_percent), 14, kText, 1);
        draw_text_in_rect(r, cl[5], fmt_bytes(total_disk) + "/s", 14, kText, 1);
        yy += row_h_;
    }
    {
        const Rect grp{x0, yy, w, group_h};
        group_front_hit_ = grp;
        draw_group_header(grp, std::string(tr("应用", "Apps")) + " (" + std::to_string(front_idx.size()) + ")", group_front_open_);
        yy += group_h;
        if (group_front_open_)
            for (int gi : front_idx) {
                if (yy + row_h_ > list_rc.y && yy < list_rc.y + list_rc.h)
                    draw_proc_row(procs_[gi], gi, yy, row_no);
                ++row_no;
                yy += row_h_;
            }
    }
    {
        const Rect grp{x0, yy, w, group_h};
        group_bg_hit_ = grp;
        draw_group_header(grp, std::string(tr("后台进程", "Background processes")) + " (" + std::to_string(bg_idx.size()) + ")", group_bg_open_);
        yy += group_h;
        if (group_bg_open_)
            for (int gi : bg_idx) {
                if (yy + row_h_ > list_rc.y && yy < list_rc.y + list_rc.h)
                    draw_proc_row(procs_[gi], gi, yy, row_no);
                ++row_no;
                yy += row_h_;
            }
    }

    {
        const float list_h = list_rc.h;
        const float content_h = row_h_ * (1.0f +
            (group_front_open_ ? (float)front_idx.size() : 0.0f) +
            (group_bg_open_   ? (float)bg_idx.size()   : 0.0f)) + 2 * 32.0f;
        const float max_scroll = std::max(0.0f, content_h + 2 - list_h);
        if (max_scroll > 0.5f) {
            const float track_w = 4.0f;
            const float track_x = x0 + w - 10.0f;
            r.draw_rounded_rect(Rect{track_x, list_rc.y, track_w, list_h}, 2, kTrack);
            const float thumb_h = std::max(24.0f, list_h * (list_h / content_h));
            const float t = std::min(1.0f, scroll_ / max_scroll);
            const float thumb_y = list_rc.y + (list_h - thumb_h) * t;
            r.draw_rounded_rect(Rect{track_x, thumb_y, track_w, thumb_h}, 2, kSubText);
        }
    }
    r.clear_scissor();
}
