#include "taskmgr/taskmgr.hpp"
#include "taskmgr/taskmgr_internal.hpp"

#include <cstdio>
#include <string>

// —— 当前配色定义与系统深浅色同步 ——
// Active palette definitions, refreshed from the macOS system appearance
Color kNavBg, kCardBg, kBorder, kShadow, kRowBg, kRowAltBg, kHeadBg, kSelBg,
      kText, kSubText, kDimText, kAccent, kHoverBg, kTrack, kBlue, kGreen,
      kSColor, kYellow, kRed, kPurple, kBoxBg, kDivider, kRefLine;

namespace theme {
void update() {
    const bool dark = system_dark_mode();   // 实现在 theme.cpp（隔离 CoreFoundation）
    if (dark) {
        kNavBg = kNavBgDark;     kCardBg = kCardBgDark;   kBorder = kBorderDark;
        kShadow = kShadowDark;   kRowBg = kRowBgDark;     kRowAltBg = kRowAltBgDark;
        kHeadBg = kHeadBgDark;   kSelBg = kSelBgDark;     kText = kTextDark;
        kSubText = kSubTextDark; kDimText = kDimTextDark; kAccent = kAccentDark;
        kHoverBg = kHoverBgDark; kTrack = kTrackDark;     kBlue = kBlueDark;
        kGreen = kGreenDark;     kSColor = kSColorDark;   kYellow = kYellowDark;
        kRed = kRedDark;         kPurple = kPurpleDark;   kBoxBg = kBoxBgDark;
        kDivider = kDividerDark; kRefLine = kRefLineDark;
    } else {
        kNavBg = kNavBgLight;    kCardBg = kCardBgLight;  kBorder = kBorderLight;
        kShadow = kShadowLight;  kRowBg = kRowBgLight;    kRowAltBg = kRowAltBgLight;
        kHeadBg = kHeadBgLight;  kSelBg = kSelBgLight;    kText = kTextLight;
        kSubText = kSubTextLight;kDimText = kDimTextLight;kAccent = kAccentLight;
        kHoverBg = kHoverBgLight;kTrack = kTrackLight;    kBlue = kBlueLight;
        kGreen = kGreenLight;    kSColor = kSColorLight;  kYellow = kYellowLight;
        kRed = kRedLight;        kPurple = kPurpleLight;  kBoxBg = kBoxBgLight;
        kDivider = kDividerLight; kRefLine = kRefLineLight;
    }
}
} // namespace theme

// 通用工具：矩形内文本、进度条、命中测试、字节/百分比格式化。
// Generic helpers: text in rect, progress bar, hit test, byte/percent formatting.

// 在矩形内绘制文本，支持左/中/右对齐并垂直居中
// Draw text inside a rect with horizontal alignment and vertical centering
void draw_text_in_rect(Renderer& r, Rect rc, const std::string& s, float size, Color c, int align) {
    if (s.empty() || rc.w <= 0) return;
    const float tw = r.font().font_text_width(s.c_str(), size);
    float top = 0, bottom = 0;
    r.font().font_text_ink_extent(s.c_str(), size, top, bottom);
    float tx = rc.x;
    if (align == 1) tx = rc.x + (rc.w - tw) * 0.5f;
    else if (align == 2) tx = rc.x + rc.w - tw;
    const float ty = rc.y + rc.h * 0.5f - (top + bottom) * 0.5f;
    r.font().font_draw(tx, ty, s.c_str(), c, size);
}

// 进度条：轨道 + 按比例填充
// Progress bar: track plus proportional fill
void draw_bar(Renderer& r, Rect rect, float t, Color fill, Color track) {
    r.draw_rounded_rect(rect, rect.h * 0.5f, track);
    const float fw = rect.w * (t < 0 ? 0 : t > 1 ? 1 : t);
    if (fw > 2) r.draw_rounded_rect(Rect{rect.x, rect.y, fw, rect.h}, rect.h * 0.5f, fill);
}

// 命中测试：点是否落在矩形内
// Hit test: is the point inside the rect
bool hit(const Rect& r, float mx, float my) {
    return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
}

// 字节数格式化：B / KB / MB / GB / TB
// Format bytes into a human readable string
std::string fmt_bytes(double bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = bytes;
    int i = 0;
    while (v >= 1024 && i < 4) { v /= 1024; ++i; }
    char buf[32];
    if (i == 0) std::snprintf(buf, sizeof buf, "%.0f %s", v, units[i]);
    else std::snprintf(buf, sizeof buf, "%.1f %s", v, units[i]);
    return buf;
}

// 百分比格式化：保留一位小数并加 %
// Format a percentage with one decimal and a percent sign
std::string fmt_percent(double v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%.1f%%", v);
    return buf;
}
