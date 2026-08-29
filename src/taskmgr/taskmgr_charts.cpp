#include "taskmgr/charts.hpp"
#include "taskmgr/taskmgr_internal.hpp"
#include "sys/system.hpp"

#include <algorithm>
#include <cmath>

// 界面语言翻译帮助（由 sys/system.hpp 提供，跟随系统语言）
using ::sys::tr;

// 图表绘制：核心方框 / 逻辑核网格 / 面积+折线图 / 参考线网格 / GPU / 内存大方框。
// Chart drawing: core box, logical-core grid, area-line charts, reference grid,
// GPU box and memory box.

// 单个核心方框：深色背景 + 淡网格底纹 + 彩色边框 + 内部该核占用曲线（面积 + 折线）
// One core box: dark background, faint grid, colored border and a per-core area-line chart
void Charts::draw_core_box(Renderer& r, Rect box, int core_index, Color c) {
    r.draw_rounded_rect(box, 4, kBoxBg);
    // 历史索引越界（如 GPU 规模框）则只画边框
    if (core_index < 0 || core_index >= (int)hist.core_hist.size()) {
        r.draw_rounded_outline(box, 4, c, 1);
        return;
    }
    // 每个核心方框内：滚动竖线网格（随刷新左移） + 该核占用曲线
    draw_reflines(r, box, (int)hist.core_hist[core_index].size());
    draw_area_line(r, box, hist.core_hist[core_index], c);
    r.draw_rounded_outline(box, 4, c, 1);
}

// 统一逻辑核网格：不同级别（S / P / E）各成一组，组内每行对齐，组与组之间加
// 垂直间隔以视觉区分。S 核紫色、P 核蓝色、E 核绿色。每个方框内部画该核的
// 占用曲线（Win11 风格的小方块）。
// Unified logical-core grid; each level (S/P/E) forms its own group separated by a
// vertical gap. S cores purple, P cores blue, E cores green.
void Charts::draw_core_grid(Renderer& r, Rect area, int total_cores, int s_cores, int p_cores) {
    if (total_cores <= 0) return;
    const int e_cores = total_cores - s_cores - p_cores;
    const float gap = 4;                 // 组内核心间距
    const float level_gap = 12;          // 不同级别之间的垂直间隔
    const float min_box = 12, max_box = 120;
    const float avail_w = area.w;
    const float avail_h = area.h;

    // 迭代尝试不同的每行核数，求能在区域内排下的最大方框尺寸（尽量填满区域）
    // Try different columns-per-row and pick the largest box that fills the area
    float box = min_box;
    for (int per_row = 1; per_row <= total_cores; ++per_row) {
        const int rows = (total_cores + per_row - 1) / per_row;
        const float bw = (avail_w - (float)(per_row - 1) * gap) / (float)per_row;
        const float bh = (avail_h - (float)(rows - 1) * gap) / (float)rows;
        float b = std::min(bw, bh);
        if (b > max_box) b = max_box;
        if (b < min_box) break;            // 列数太多，已无法放下最小方框
        if (b > box) box = b;
    }
    // 由最终方框大小反推每行数量
    const int per_row = std::max(1, (int)((avail_w + gap) / (box + gap)));

    // 按级别分组（只保留存在的组）：S 在前、P 居中、E 在后
    struct Grp { int count; Color c; };
    Grp grp[3]; int ng = 0;
    if (s_cores > 0) grp[ng++] = {s_cores, kSColor};
    if (p_cores > 0) grp[ng++] = {p_cores, kAccent};
    if (e_cores > 0) grp[ng++] = {e_cores, kGreen};

    // 逐组绘制：组内每行完全对齐，组之间加垂直间隔
    float by = area.y;
    int idx = 0;
    for (int g = 0; g < ng; ++g) {
        int remain = grp[g].count;
        const Color c = grp[g].c;
        while (remain > 0) {
            const int n_this_row = std::min(per_row, remain);
            const float row_w = (float)per_row * box + (float)(per_row - 1) * gap;
            float bx = area.x + (avail_w - row_w) * 0.5f;
            for (int i = 0; i < n_this_row; ++i, ++idx) {
                draw_core_box(r, Rect{bx, by, box, box}, idx, c);
                bx += box + gap;
            }
            remain -= n_this_row;
            by += box + gap;
        }
        if (g < ng - 1) by += level_gap;   // 组之间上下隔开一点
    }
}

// 面积 + 折线图：折线用实色，线下方用同色低透明版本做面积填充。
// 采用“右对齐滚动”：最新点贴右边缘，采样点未满窗口时曲线只占右侧、左侧留空。
// Area + line chart with right-aligned scrolling: the newest sample sits at the right
// edge; while the buffer is not full the curve occupies only the right part.
void Charts::draw_area_line(Renderer& r, Rect box, const std::vector<float>& data, Color line) {
    const int n = (int)data.size();
    if (n <= 0) return;
    // 图元内边距（命名常量）
    const float x0 = box.x + kChartPadX, w = box.w - kChartPadX * 2;
    const float bottom = box.y + box.h - kChartPadY, max_h = box.h - kChartPadY * 2;
    const int cols = std::max(1, (int)w);
    // 右对齐：按历史占满窗口的比例计算实际占用的列数，左侧留空
    // Right-aligned: only the right portion is used until the history fills the window
    const int used_cols = std::max(1, (int)std::ceil((float)(n - 1) / (float)(History::kHist - 1) * (float)cols));
    const int left_gap = std::max(0, cols - used_cols);
    const Color fill{line.r, line.g, line.b, 0.22f};
    for (int px = left_gap; px < cols; ++px) {
        // 把像素列映射到历史索引，线性插值当前值
        const float fx = (float)(px - left_gap) / (float)std::max(1, used_cols - 1) * (float)(n - 1);
        const int i0 = (int)fx;
        const int i1 = std::min(i0 + 1, n - 1);
        const float t = fx - (float)i0;
        const float v = data[i0] + (data[i1] - data[i0]) * t;
        const float y = bottom - v * 0.01f * max_h;
        // 面积列：从线所在高度填充到底部（淡色）
        const float hh = bottom - y;
        if (hh > 0) r.draw_rect(Rect{x0 + (float)px, y, 1, hh}, fill);
        // 线点：每列一个 1px 像素点，纵向相邻形成连续折线
        r.draw_rect(Rect{x0 + (float)px, y, 1, 1}, line);
    }
}

// 完整网格：水平网格线（静态，25%/50%/75%）+ 滚动竖线（随刷新左移）。
// 铺满整个图表区域；竖线随每次采样向左推进形成流动背景，横线固定用于读值。
// Full grid: static horizontal lines (25/50/75%) plus scrolling vertical lines that
// advance left per refresh. Horizontal lines are fixed reference, vertical ones move.
void Charts::draw_reflines(Renderer& r, Rect box, int /*hist_size*/) {
    // 图元内边距（命名常量）
    const float x0 = box.x + kChartPadX, line_w = box.w - kChartPadX * 2;
    if (line_w < 1.0f) return;
    const float y0 = box.y + kChartPadY, hh = box.h - kChartPadY * 2;

    // 水平网格线（静态参考线，25% / 50% / 75%）；颜色随深浅色主题
    // Static horizontal reference lines; color follows the light/dark theme
    const float bottom = box.y + box.h - kChartPadY, max_h = box.h - kChartPadY * 2;
    const Color hgrid = kRefLine;
    for (int p = 25; p <= 75; p += 25) {
        const float y = bottom - (float)p * 0.01f * max_h;
        r.draw_rect(Rect{x0, y, line_w, 1}, hgrid);
    }

    // 每刷新推进一个采样点宽度（约为线宽 / 历史长度），用于驱动竖线左移
    // Advance one sample width per refresh to drive the vertical lines left
    const float pw = line_w / (float)std::max(1, History::kHist - 1);
    // 竖线间隔自适应：小框（如每个核心方框）用更密间隔，让网格流动更明显
    // Adaptive spacing: small boxes (per-core) use a denser grid so the flow is visible
    const float spacing = line_w < 64.0f ? 10.0f : 30.0f;
    const float scroll = std::fmod((float)hist.sample_total * pw, spacing);
    const Color vgrid = kRefLine;
    // 铺满整个区域画竖线，随采样向左推进（循环往复）
    for (float gx = x0 + line_w - scroll; gx > x0 - spacing; gx -= spacing) {
        if (gx < x0) continue;
        r.draw_rect(Rect{gx, y0, 1, hh}, vgrid);
    }
}

// GPU 总体占用大方框：深色背景 + 紫色边框 + 内部 GPU 使用率曲线 + 中央当前值
// Large GPU utilization box: dark background, purple border, inner usage curve and current value
void Charts::draw_gpu_box(Renderer& r, Rect box) {
    r.draw_rounded_rect(box, 10, kBoxBg);
    r.draw_rounded_outline(box, 10, kPurple, 1);
    draw_text_in_rect(r, Rect{box.x, box.y - 22, box.w, 18}, tr("GPU 总体占用", "GPU Overall Usage"), 12, kDimText, 0);
    // 百分比参考线 + 内部使用率曲线（面积 + 折线）
    draw_reflines(r, box, (int)hist.gpu_hist.size());
    draw_area_line(r, box, hist.gpu_hist, kPurple);
    // 中央显示当前使用率
    draw_text_in_rect(r, box, fmt_percent(hist.gpu_util), 24, kText, 1);
}

// 内存卡片内容：左侧“内存压力”填充图（按使用率绿/黄/红），右侧分类数值列表
// （App 内存 / 联动内存 / 被压缩 / 已缓存文件 / 已使用内存 / 物理内存），
// 布局与 macOS 活动监视器的内存面板一致。
// Memory card: a memory-pressure fill chart on the left and category values on the
// right, matching the macOS Activity Monitor memory panel.
void Charts::draw_mem_box(Renderer& r, Rect box) {
    r.draw_rounded_rect(box, 10, kBoxBg);
    r.draw_rounded_outline(box, 10, kAccent, 1);

    // 左侧：内存压力填充图（宽约 36%），颜色随占用率在绿/黄/红间变化
    const float chart_w = box.w * 0.36f;
    const Rect chart{box.x + 8, box.y + 10, chart_w, box.h - 20};
    draw_reflines(r, chart, (int)hist.mem_hist.size());
    draw_area_line(r, chart, hist.mem_hist, usage_color(ov.mem_percent / 100.0));
    draw_text_in_rect(r, Rect{chart.x, chart.y - 18, chart.w, 16}, tr("内存压力", "Memory pressure"), 11, kSubText, 0);
    draw_text_in_rect(r, Rect{chart.x, chart.y + chart.h - 22, chart.w, 18}, fmt_percent(ov.mem_percent), 14, kText, 1);

    // 右侧：分类数值列表（活动监视器口径）
    const float lx = box.x + 8 + chart_w + 12;
    const float lw = box.w - (chart_w + 20);
    struct Row { const char* label; double value; };
    const Row rows[] = {
        {tr("App 内存", "App memory"),   ov.mem_app},
        {tr("联动内存", "Wired memory"),   ov.mem_wired},
        {tr("被压缩", "Compressed"),     ov.mem_compressed},
        {tr("已缓存文件", "Cached files"), ov.mem_cached},
        {tr("已使用内存", "In use"), ov.mem_used},
        {tr("物理内存", "Physical memory"),   ov.mem_total},
    };
    const float row_h = (box.h - 16) / 6.0f;
    for (int i = 0; i < 6; ++i) {
        const float y = box.y + 8 + (float)i * row_h;
        draw_text_in_rect(r, Rect{lx, y, lw, row_h}, rows[i].label, 12, kSubText, 0);
        draw_text_in_rect(r, Rect{lx, y, lw, row_h}, fmt_bytes(rows[i].value), 12, kText, 2);
    }
}
