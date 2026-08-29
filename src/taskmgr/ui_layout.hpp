#pragma once

#include "types/rect.hpp"
#include <vector>

// 布局几何层：集中管理所有由弹性容器推导出的区域矩形。
// 只根据窗口点尺寸计算分区，不依赖绘制 / 输入 / 系统数据。
// Layout geometry: owns every region rect derived by the flex containers.
// It depends only on the window point size, not on drawing / input / system data.
struct UiLayout {
    Rect sidebar;                 // 左侧导航侧边栏
    Rect content;                 // 侧边栏右侧内容区
    Rect nav_perf;                // 顶部导航项（性能）
    Rect nav_proc;                // 顶部导航项（进程）
    Rect dev_list;                // 性能页设备列表区
    Rect dev_detail;              // 性能页设备详情区
    std::vector<Rect> dev_items;  // 设备列表项矩形（用于点击/悬停）

    // 根据窗口点尺寸计算所有分区
    // Compute all regions from the window point size
    void compute(float w, float h);
};
