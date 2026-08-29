#pragma once

#include "render/renderer.hpp"
#include "sys/system.hpp"
#include "taskmgr/history.hpp"
#include "types/color.hpp"
#include "types/rect.hpp"
#include <vector>

// 图表绘制模块：核心方框 / 逻辑核网格 / 面积+折线图 / 参考线网格 / GPU / 内存大方框。
// 持有历史数据与系统总体数据的引用，只负责图形绘制，不依赖页面/输入。
// Chart drawing module: core box, logical-core grid, area-line charts, reference
// grid, GPU box and memory box. Holds refs to the history and overview data and
// only draws, independent of pages / input.
struct Charts {
    History& hist;                    // 历史曲线（核/CPU/内存/GPU 采样）
    const sys::Overview& ov;          // 系统总体数据

    Charts(History& h, const sys::Overview& o) : hist(h), ov(o) {}

    // 单个核心方框：深色背景 + 边框 + 内部占用曲线（面积 + 折线）
    void draw_core_box(Renderer& r, Rect box, int core_index, Color c);
    // 统一逻辑核网格：S / P / E 分组，组间加垂直间隔
    void draw_core_grid(Renderer& r, Rect area, int total_cores, int s_cores, int p_cores);
    // 面积 + 折线图：实线 + 线下方低透明填充（右对齐滚动）
    void draw_area_line(Renderer& r, Rect box, const std::vector<float>& data, Color line);
    // 速率面积图：按数据最大值归一化高度（用于磁盘读/写速率等非百分比数据）
    void draw_rate_line(Renderer& r, Rect box, const std::vector<float>& data, Color line);
    // 在大方框内画横向百分比参考线（25/50/75%）+ 滚动竖线
    void draw_reflines(Renderer& r, Rect box, int hist_size);
    // GPU 总体占用大方框
    void draw_gpu_box(Renderer& r, Rect box);
    // 内存卡片：左侧内存压力图 + 右侧分类数值列表
    void draw_mem_box(Renderer& r, Rect box);
};
