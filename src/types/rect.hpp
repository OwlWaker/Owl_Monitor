#pragma once

// 二维坐标点
// 2D point
struct Point {
    // 横纵坐标
    // x and y coordinates
    float x, y;
    // 默认构造，原点
    // Default constructor, origin
    constexpr Point() : x(0), y(0) {}
    // 按坐标构造
    // Construct from coordinates
    constexpr Point(float x, float y) : x(x), y(y) {}
    // 点相加
    // Point addition
    constexpr Point operator+(Point o) const { return {x + o.x, y + o.y}; }
    // 点相减
    // Point subtraction
    constexpr Point operator-(Point o) const { return {x - o.x, y - o.y}; }
    // 点乘以标量
    // Point scaled by a scalar
    constexpr Point operator*(float s) const { return {x * s, y * s}; }
};

// 矩形，x y 为左上角，w h 为宽高
// Rectangle, x y is the top left corner, w h is the size
struct Rect {
    // 左上角坐标与宽高
    // Top left corner and size
    float x, y, w, h;
    // 默认构造，空矩形
    // Default constructor, empty rectangle
    constexpr Rect() : x(0), y(0), w(0), h(0) {}
    // 从原点构造宽高矩形
    // Construct a rectangle at origin with given size
    constexpr Rect(float w, float h) : x(0), y(0), w(w), h(h) {}
    // 按位置与宽高构造
    // Construct from position and size
    constexpr Rect(float x, float y, float w, float h) : x(x), y(y), w(w), h(h) {}

    // 矩形相加（位置与尺寸逐项加）
    // Rectangle addition
    constexpr Rect operator+(Rect o) const { return {x + o.x, y + o.y, w + o.w, h + o.h}; }
    // 矩形相减
    // Rectangle subtraction
    constexpr Rect operator-(Rect o) const { return {x - o.x, y - o.y, w - o.w, h - o.h}; }
    // 矩形乘以标量
    // Rectangle scaled by a scalar
    constexpr Rect operator*(float s) const { return {x * s, y * s, w * s, h * s}; }

    // 以左上角点构造
    // Construct from top left point
    static constexpr Rect from_tl(Point p, float w, float h) { return {p.x, p.y, w, h}; }
    // 以右上角点构造
    // Construct from top right point
    static constexpr Rect from_tr(Point p, float w, float h) { return {p.x - w, p.y, w, h}; }
    // 以左下角点构造
    // Construct from bottom left point
    static constexpr Rect from_bl(Point p, float w, float h) { return {p.x, p.y - h, w, h}; }
    // 以右下角点构造
    // Construct from bottom right point
    static constexpr Rect from_br(Point p, float w, float h) { return {p.x - w, p.y - h, w, h}; }
// 由对角两点构造（a、b 可为任意对角，自动求外接矩形），常用于拖拽选区
// Construct from two diagonal points (any order), computes the bounding rect; used for drag selections
static constexpr Rect from_diag(Point a, Point b) {
    const float l = a.x < b.x ? a.x : b.x;
    const float t = a.y < b.y ? a.y : b.y;
    const float r = a.x > b.x ? a.x : b.x;
    const float bo = a.y > b.y ? a.y : b.y;
    return {l, t, r - l, bo - t};
}
// 由四条边坐标构造（左、上、右、下）
    // Construct from the four edge coordinates
    static constexpr Rect from_edges(float l, float t, float right, float b) { return {l, t, right - l, b - t}; }

    // 左上角点
    // Top left point
    constexpr Point get_tl() const { return {x, y}; }
    // 右上角点
    // Top right point
    constexpr Point get_tr() const { return {x + w, y}; }
    // 左下角点
    // Bottom left point
    constexpr Point get_bl() const { return {x, y + h}; }
    // 右下角点
    // Bottom right point
    constexpr Point get_br() const { return {x + w, y + h}; }

    // 左边界坐标
    // Left edge coordinate
    constexpr float get_left()   const { return x; }
    // 右边界坐标
    // Right edge coordinate
    constexpr float get_right()  const { return x + w; }
    // 上边界坐标
    // Top edge coordinate
    constexpr float get_top()    const { return y; }
    // 下边界坐标
    // Bottom edge coordinate
    constexpr float get_bottom() const { return y + h; }
    // 水平中心坐标
    // Horizontal center coordinate
    constexpr float get_cx()     const { return x + w * 0.5f; }
    // 垂直中心坐标
    // Vertical center coordinate
    constexpr float get_cy()     const { return y + h * 0.5f; }
    // 平移指定偏移（位置移动、尺寸不变）
    // Translate by a given offset (moves position, size unchanged)
    constexpr Rect  translated(float dx, float dy) const { return {x + dx, y + dy, w, h}; }
    // 判断点是否落在矩形内：左/上为闭区间、右/下为开区间（[x, x+w) × [y, y+h)）
    // Test whether a point lies inside: left/top inclusive, right/bottom exclusive
    constexpr bool  contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

