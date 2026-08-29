#pragma once

#include "render/renderer.hpp"
#include "types/color.hpp"
#include "types/rect.hpp"

struct TaskManager;

// 性能页面板：设备列表 + 选中设备详情绘制。
// Overview panel: draws the device list and the selected device detail.
struct OverviewPanel {
    TaskManager& owner_;
    explicit OverviewPanel(TaskManager& o) : owner_(o) {}
    void draw(Renderer& r);
private:
    void draw_device_list(Renderer& r);
    void draw_device_detail(Renderer& r, Rect rc);
};

// 进程页面板：表头 + 分组列表 + 滚动条绘制。
// Processes panel: draws the header, grouped list and scrollbar.
struct ProcessesPanel {
    TaskManager& owner_;
    explicit ProcessesPanel(TaskManager& o) : owner_(o) {}
    void draw(Renderer& r);
};
