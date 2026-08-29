#include "taskmgr/ui_layout.hpp"
#include "taskmgr/layout.hpp"           // fl 弹性布局引擎
#include "taskmgr/taskmgr_internal.hpp" // 布局常量 kTopNavH / kNavItemW

// 布局：顶部横向导航栏 + 下方内容区；侧边栏内排列两个导航项，
// 性能页在内容区再分设备列表（左）与详情（右）。
// 全部由弹性容器（fl::Flex）推导，不写死坐标偏移。
// Layout: top navbar + content below; the top bar holds two nav items, and the
// performance page splits the content into a device list and a detail area.
// All rects are derived by flex containers instead of hardcoded offsets.
void UiLayout::compute(float w, float h) {
    sidebar = Rect{0, 0, w, kTopNavH};
    content = Rect{0, kTopNavH, w, h - kTopNavH};

    // 顶部栏：自左到右 红绿灯留白 / 标题 / 搜索框 / 结束进程 / 导航项(剩余区域居中)
    {
        fl::Flex f;
        f.dir = fl::Dir::Row;
        f.pad = {0, 4, 8, 4};
        f.gap = 10;
        f.items = {{.size = 72}, {.size = 120}, {.size = 110}, {.flex = 1}, {.size = 220}};
        auto rc = f.layout(Rect{0, 0, w, kTopNavH});
        const Rect nav_box = rc[3];
        // nav_box 内：只放两个导航项（性能/进程），居中
        fl::Flex nf;
        nf.dir = fl::Dir::Row;
        nf.justify = fl::Justify::Center;
        nf.gap = 4;
        nf.items = {{.size = kNavItemW}, {.size = kNavItemW}};
        auto nrc = nf.layout(nav_box);
        nav_perf = nrc[0];
        nav_proc = nrc[1];
    }

    // 性能页内容：Row 容器（设备列表固定宽 + 详情弹性填满，无标题栏）
    {
        fl::Flex f;
        f.dir = fl::Dir::Row;
        f.pad = {12, 8, 20, 20};
        f.gap = 16;
        f.items = {{.size = 224}, {.flex = 1}};
        auto rc = f.layout(content);
        dev_list = rc[0];
        dev_detail = rc[1];
    }

    // 设备列表项（CPU / 内存 / GPU）：Column 容器，三个固定高的项
    {
        fl::Flex f;
        f.dir = fl::Dir::Column;
        f.pad = {8, 8, 8, 8};
        f.gap = 6;
        f.items = {{.size = 72}, {.size = 72}, {.size = 72}, {.size = 72}};
        dev_items = f.layout(dev_list);
    }
}
