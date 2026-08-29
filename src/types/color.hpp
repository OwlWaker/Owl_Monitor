#pragma once

// 颜色结构体：RGBA 各分量以浮点 [0,1] 表示，1.0 为全值/不透明。
// 全项目统一使用该类型传递颜色；渲染层会把分量直接写入 GPU 常量。
// RGBA color with float components in [0,1]
struct Color {
    // 红 / 绿 / 蓝通道与透明度分量（0 透明，1 不透明）
    // red, green, blue channels and alpha (0 transparent, 1 opaque)
    float r, g, b, a;
};

// 颜色分量逐位相加（常用于叠加调色）
// Component wise color addition
inline constexpr Color operator+(Color a, Color b) { return {a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a}; }
// 颜色分量逐位相减
// Component wise color subtraction
inline constexpr Color operator-(Color a, Color b) { return {a.r - b.r, a.g - b.g, a.b - b.b, a.a - b.a}; }
// 颜色整体乘以标量（缩放各通道与透明度）
// Scale every color component by a scalar
inline constexpr Color operator*(Color c, float s) { return {c.r * s, c.g * s, c.b * s, c.a * s}; }

// 用 RGBA 分量构造颜色，参数取值范围均为 [0,1]
// Build a color from RGBA components
inline constexpr Color make_color(float r, float g, float b, float a) { return {r, g, b, a}; }
// 由 24 位十六进制值构造不透明颜色（如 0xRRGGBB），alpha 固定为 1
// Build an opaque color from a 24 bit hex value
inline constexpr Color make_color_hex(unsigned int hex) {
    return {(float)((hex>>16)&0xFF)/255.0f, (float)((hex>>8)&0xFF)/255.0f, (float)(hex&0xFF)/255.0f, 1.0f};
}

// 向白色方向提亮：a 为提亮强度（0 不变，1 变为纯白），alpha 保持不变
// Lighten toward white by amount a (0 keeps, 1 becomes white); alpha unchanged
inline constexpr Color color_lighten(Color c, float a) { return {c.r+(1.0f-c.r)*a, c.g+(1.0f-c.g)*a, c.b+(1.0f-c.b)*a, c.a}; }
// 重设透明度分量，RGB 不变
// Replace the alpha component, RGB unchanged
inline constexpr Color color_set_alpha(Color c, float a) { return {c.r, c.g, c.b, a}; }
// 两颜色按 t 线性插值混合（t=0 取 a，t=1 取 b），用于颜色渐变/过渡动画
// Linearly interpolate between two colors by t (0=a, 1=b), used for gradients and transitions
inline constexpr Color color_mix(Color a, Color b, float t) { return {a.r+(b.r-a.r)*t, a.g+(b.g-a.g)*t, a.b+(b.b-a.b)*t, a.a+(b.a-a.a)*t}; }

// HSV 转 RGB：h 为色相 [0,1]、s 为饱和度 [0,1]、v 为明度 [0,1]。
// 算法把色环按 60° 分为六个扇形，根据所在扇形取 RGB 三分量。用于彩虹等动态取色。
// Convert HSV to RGB. h in [0,1], s in [0,1], v in [0,1].
// Splits the hue wheel into six 60 degree sectors and picks RGB per sector. Used for dynamic colors like rainbow.
inline Color hsv2rgb(float h, float s, float v) {
    const int i = (int)(h * 6.0f);
    const float f = h * 6.0f - i;
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - s * f);
    const float t = v * (1.0f - s * (1.0f - f));
    switch (i % 6) {
        case 0: return {v, t, p, 1};
        case 1: return {q, v, p, 1};
        case 2: return {p, v, t, 1};
        case 3: return {p, q, v, 1};
        case 4: return {t, p, v, 1};
        default: return {v, p, q, 1};
    }
}

// 常用颜色常量
// Common color constants
// 黑色
// Black
inline constexpr Color color_black       = make_color(0, 0, 0, 1);
// 白色
// White
inline constexpr Color color_white       = make_color(1, 1, 1, 1);
// 全透明
// Fully transparent
inline constexpr Color color_transparent = make_color(0, 0, 0, 0);
// 红色
// Red
inline constexpr Color color_red         = make_color(1, 0, 0, 1);
// 绿色
// Green
inline constexpr Color color_green       = make_color(0, 1, 0, 1);
// 蓝色
// Blue
inline constexpr Color color_blue        = make_color(0, 0, 1, 1);
// 黄色
// Yellow
inline constexpr Color color_yellow      = make_color(1, 1, 0, 1);
// 青色
// Cyan
inline constexpr Color color_cyan        = make_color(0, 1, 1, 1);
// 品红
// Magenta
inline constexpr Color color_magenta     = make_color(1, 0, 1, 1);
// 深红
// Dark red
inline constexpr Color color_red_dark    = color_mix(color_red,   color_black, 0.50f);
// 浅红
// Light red
inline constexpr Color color_red_light   = color_mix(color_red,   color_white, 0.50f);
// 深绿
// Dark green
inline constexpr Color color_green_dark  = color_mix(color_green, color_black, 0.50f);
// 天蓝
// Sky blue
inline constexpr Color color_sky_blue    = color_mix(color_mix(color_cyan, color_blue, 0.30f), color_white, 0.45f);
// 橙色
// Orange
inline constexpr Color color_orange      = color_mix(color_red,    color_yellow, 0.65f);
// 紫色
// Purple
inline constexpr Color color_purple      = color_mix(color_red,    color_blue,   0.50f);
// 粉色
// Pink
inline constexpr Color color_pink        = color_mix(color_red,    color_white,  0.78f);
// 棕色
// Brown
inline constexpr Color color_brown       = color_mix(color_orange, color_black,  0.60f);
// 灰色
// Gray
inline constexpr Color color_gray        = color_mix(color_white,  color_black,  0.70f);
// 金色
// Gold
inline constexpr Color color_gold        = color_mix(color_yellow, color_orange, 0.40f);

