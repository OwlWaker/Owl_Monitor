#pragma once

#include "taskmgr/layout.hpp"
#include "taskmgr/taskmgr.hpp"
#include "types/color.hpp"
#include <vector>

// 拆分 taskmgr 后多个 .cpp 共用的内部常量与工具（不进入公共 API）。
// 原 taskmgr.cpp 匿名命名空间内容，改为头文件内联定义以便各拆分单元共享。

// —— 图元内边距 / 尺寸常量（单元格内部绘制，替代散落的硬编码偏移）——
// Per-primitive padding / size constants used inside a single cell
constexpr float kChartPadX = 2;    // 折线/网格框水平内边距
constexpr float kChartPadY = 2;    // 折线/网格框垂直内边距
constexpr float kMiniPadX  = 3;    // 设备列表缩略图曲线水平内边距
constexpr float kMiniPadY  = 4;    // 设备列表缩略图曲线垂直内边距
constexpr float kStatLabelH = 20;  // 统计块标签行高
constexpr float kStatValueH = 26;  // 统计块数值行高
constexpr float kStatGap    = 2;   // 统计块标签与数值间距
constexpr float kStatRowGap = 8;   // CPU 统计区两行间距（56 - 行高 48）
constexpr float kMemRowH    = 56;  // 内存/GPU 统计区行高

// —— 任务管理器配色：深色 / 浅色两套主题（macOS 系统深浅色）——
// Dark / light palettes; the active one is selected by theme::update().
// 深色主题（Windows 11 任务管理器深色风格）
constexpr Color kNavBgDark    = {0.121f, 0.121f, 0.129f, 1.0f};
constexpr Color kCardBgDark   = {0.165f, 0.165f, 0.176f, 1.0f};
constexpr Color kBorderDark   = {1.0f, 1.0f, 1.0f, 0.07f};
constexpr Color kShadowDark   = {0.0f, 0.0f, 0.0f, 0.30f};
constexpr Color kRowBgDark    = {0.145f, 0.145f, 0.153f, 1.0f};
constexpr Color kRowAltBgDark = {0.129f, 0.129f, 0.137f, 1.0f};
constexpr Color kHeadBgDark   = {0.110f, 0.110f, 0.117f, 1.0f};
constexpr Color kSelBgDark    = {0.212f, 0.212f, 0.224f, 1.0f};
constexpr Color kTextDark     = {0.945f, 0.945f, 0.957f, 1.0f};
constexpr Color kSubTextDark  = {0.620f, 0.620f, 0.650f, 1.0f};
constexpr Color kDimTextDark  = {0.450f, 0.450f, 0.480f, 1.0f};
constexpr Color kAccentDark   = {0.255f, 0.608f, 0.980f, 1.0f};
constexpr Color kHoverBgDark  = {0.200f, 0.200f, 0.212f, 1.0f};
constexpr Color kTrackDark    = {0.220f, 0.220f, 0.235f, 1.0f};
constexpr Color kBlueDark     = {0.255f, 0.608f, 0.980f, 1.0f};
constexpr Color kGreenDark    = {0.200f, 0.740f, 0.420f, 1.0f};
constexpr Color kSColorDark   = {0.620f, 0.470f, 0.940f, 1.0f};
constexpr Color kYellowDark   = {0.980f, 0.720f, 0.220f, 1.0f};
constexpr Color kRedDark      = {0.910f, 0.360f, 0.360f, 1.0f};
constexpr Color kPurpleDark   = {0.620f, 0.470f, 0.940f, 1.0f};
constexpr Color kBoxBgDark    = {0.129f, 0.129f, 0.137f, 1.0f};
constexpr Color kDividerDark  = {1.0f, 1.0f, 1.0f, 0.06f};
constexpr Color kRefLineDark  = {1.0f, 1.0f, 1.0f, 0.10f};  // 图表参考线（深色：白）
// 浅色主题（macOS 原生浅色）
constexpr Color kNavBgLight    = {0.957f, 0.957f, 0.961f, 1.0f};
constexpr Color kCardBgLight   = {0.980f, 0.980f, 0.984f, 1.0f};
constexpr Color kBorderLight   = {0.0f, 0.0f, 0.0f, 0.08f};
constexpr Color kShadowLight   = {0.0f, 0.0f, 0.0f, 0.12f};
constexpr Color kRowBgLight    = {1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color kRowAltBgLight = {0.961f, 0.961f, 0.965f, 1.0f};
constexpr Color kHeadBgLight   = {0.933f, 0.933f, 0.941f, 1.0f};
constexpr Color kSelBgLight    = {0.878f, 0.925f, 0.984f, 1.0f};
constexpr Color kTextLight     = {0.114f, 0.114f, 0.122f, 1.0f};
constexpr Color kSubTextLight  = {0.431f, 0.431f, 0.451f, 1.0f};
constexpr Color kDimTextLight  = {0.525f, 0.525f, 0.545f, 1.0f};
constexpr Color kAccentLight   = {0.0f, 0.478f, 1.0f, 1.0f};
constexpr Color kHoverBgLight  = {0.922f, 0.937f, 0.957f, 1.0f};
constexpr Color kTrackLight    = {0.851f, 0.851f, 0.863f, 1.0f};
constexpr Color kBlueLight     = {0.0f, 0.478f, 1.0f, 1.0f};
constexpr Color kGreenLight    = {0.204f, 0.780f, 0.349f, 1.0f};
constexpr Color kSColorLight   = {0.686f, 0.322f, 0.871f, 1.0f};
constexpr Color kYellowLight   = {1.0f, 0.624f, 0.039f, 1.0f};
constexpr Color kRedLight      = {1.0f, 0.231f, 0.188f, 1.0f};
constexpr Color kPurpleLight   = {0.686f, 0.322f, 0.871f, 1.0f};
constexpr Color kBoxBgLight    = {0.961f, 0.961f, 0.965f, 1.0f};
constexpr Color kDividerLight  = {0.0f, 0.0f, 0.0f, 0.12f};
constexpr Color kRefLineLight = {0.0f, 0.0f, 0.0f, 0.14f};  // 图表参考线（浅色：黑）

// 当前激活配色（由 theme::update() 按系统模式填充；绘制函数直接使用这些名字）
// Active palette, refreshed by theme::update() from the system appearance
extern Color kNavBg, kCardBg, kBorder, kShadow, kRowBg, kRowAltBg, kHeadBg, kSelBg,
             kText, kSubText, kDimText, kAccent, kHoverBg, kTrack, kBlue, kGreen,
             kSColor, kYellow, kRed, kPurple, kBoxBg, kDivider, kRefLine;

// 根据系统深浅色模式刷新当前配色；system_dark_mode 由 theme.cpp 实现
// （隔离 CoreFoundation，避免其 Point/Rect 与项目类型冲突）
// Refresh the active palette from the system appearance; system_dark_mode is
// implemented in theme.cpp to isolate CoreFoundation from our Point/Rect types.
namespace theme {
    bool system_dark_mode();
    void update();
}

// 侧边栏宽度与内容区顶部标题栏高度
// Sidebar width and the height of the content-area page title bar
constexpr float kSidebarW = 172.0f;
constexpr float kPageTitleH = 72.0f;
constexpr float kTopNavH = 32.0f;    // 顶部导航栏高度
constexpr float kNavItemW = 132.0f;  // 顶部导航项宽度
constexpr float kToolH = 46.0f;      // 进程页自绘工具行高度

// 按使用率返回颜色（<50% 绿，<85% 黄，否则红）
// Color by usage: green under 50%, yellow under 85%, red above
inline Color usage_color(double t) {
    if (t < 0.50) return kGreen;
    if (t < 0.85) return kYellow;
    return kRed;
}

// —— 通用绘制 / 格式化工具（图表与面板共用，自由函数）——
// Generic drawing / formatting helpers shared by charts and panels
void draw_text_in_rect(Renderer& r, Rect rc, const std::string& s, float size, Color c, int align = 0);
void draw_bar(Renderer& r, Rect rect, float t, Color fill, Color track);
bool hit(const Rect& r, float mx, float my);
std::string fmt_bytes(double bytes);
std::string fmt_percent(double v);


