#pragma once

#include "types/rect.hpp"
#include <vector>

// 轻量弹性容器（flexbox 风格布局引擎），用于取代 UI 中散落的硬编码坐标偏移。
// 使用方式：构造 Flex{dir, justify, align, pad, gap, items}，
// 调用 layout(box) 一次性算出每个子项在 box 内的矩形，绘制与命中测试均使用该矩形。
// A minimal flexbox-style layout engine to replace scattered hardcoded offsets.

namespace fl {

// 主轴方向
// Main-axis direction
enum class Dir { Row, Column };
// 主轴对齐
// Main-axis justification
enum class Justify { Start, Center, End, SpaceBetween, SpaceEvenly };
// 交叉轴对齐
// Cross-axis alignment
enum class Align { Stretch, Start, Center, End };

// 内边距（左、上、右、下）
// Padding on the four sides
struct Pad {
    float l = 0, t = 0, r = 0, b = 0;
    constexpr Pad() = default;
    constexpr Pad(float all) : l(all), t(all), r(all), b(all) {}
    constexpr Pad(float h, float v) : l(h), t(v), r(h), b(v) {}
    constexpr Pad(float l, float t, float r, float b) : l(l), t(t), r(r), b(b) {}
};

// 弹性子项
// A flex child item
struct Item {
    float flex = 0;   // 弹性占比；0 = 按 size 固定
    float size = 0;   // 主轴尺寸（flex=0 时生效）
    float min = 0;    // 主轴最小尺寸
    float max = 0;    // 主轴最大尺寸（0 = 不限制）
    float cross = 0;  // 交叉轴尺寸（self != Stretch 时生效；0 = 按 0 对齐）
    Align self = Align::Stretch;  // 交叉轴对齐（覆盖容器 align）
    float mt = 0, mb = 0, ml = 0, mr = 0;  // 外边距（主轴方向参与占位）
};

// 弹性容器
// A flex container
struct Flex {
    Dir dir = Dir::Column;
    Justify justify = Justify::Start;
    Align align = Align::Stretch;
    Pad pad;
    float gap = 0;
    std::vector<Item> items;

    // 计算 items 在 box 内的矩形，返回与 items 等长的结果
    // Compute each item's rect inside box; result length equals items
    std::vector<Rect> layout(const Rect& box) const;
};

inline std::vector<Rect> Flex::layout(const Rect& box) const {
    const size_t n = items.size();
    std::vector<Rect> out(n);
    if (n == 0) return out;
    const bool row = (dir == Dir::Row);

    // 内容区与主/交叉轴原点
    const float cx = box.x + pad.l;
    const float cy = box.y + pad.t;
    const float cw = box.w - pad.l - pad.r;
    const float ch = box.h - pad.t - pad.b;
    const float main0  = row ? cx : cy;
    const float cross0 = row ? cy : cx;
    const float main_len  = row ? cw : ch;
    const float cross_len = row ? ch : cw;

    // 主轴尺寸分配：固定项按 size；弹性项分剩余空间。
    // 所有项的 margin 统一从可用主轴长度中扣除，避免与逐项推进时重复计入。
    std::vector<float> sz(n, 0), mf(n, 0), mbk(n, 0);
    float used = 0;
    float margin_sum = 0;
    float flex_sum = 0;
    for (size_t i = 0; i < n; ++i) {
        const Item& it = items[i];
        mf[i]  = row ? it.ml : it.mt;
        mbk[i] = row ? it.mr : it.mb;
        margin_sum += mf[i] + mbk[i];
        if (it.flex > 0) { flex_sum += it.flex; }
        else {
            float s = it.size;
            if (it.max > 0 && s > it.max) s = it.max;
            if (s < it.min) s = it.min;
            sz[i] = s;
            used += s;
        }
    }
    const float gaps = (n > 0) ? (float)(n - 1) * gap : 0;
    const float free = main_len - used - margin_sum - gaps;
    for (size_t i = 0; i < n; ++i) {
        if (items[i].flex > 0) {
            float s = (flex_sum > 0) ? free * (items[i].flex / flex_sum) : 0;
            if (s < 0) s = 0;
            if (items[i].max > 0 && s > items[i].max) s = items[i].max;
            if (s < items[i].min) s = items[i].min;
            sz[i] = s;
        }
    }

    // justify 决定主轴起点与项间空隙
    float total = gaps;
    for (size_t i = 0; i < n; ++i) total += sz[i] + mf[i] + mbk[i];
    const float extra = main_len - total;
    float pos = main0;
    float between = 0;
    switch (justify) {
        case Justify::Start: break;
        case Justify::Center: pos = main0 + extra * 0.5f; break;
        case Justify::End: pos = main0 + extra; break;
        case Justify::SpaceBetween:
            if (n > 1 && extra > 0) between = extra / (float)(n - 1);
            break;
        case Justify::SpaceEvenly:
            if (n > 0) pos = main0 + extra / (float)(n + 1);
            break;
    }

    for (size_t i = 0; i < n; ++i) {
        const Item& it = items[i];
        const float a = pos + mf[i];
        const float b = a + sz[i];
        // 交叉轴
        float cs, cl;
        const Align al = (it.self != Align::Stretch) ? it.self : align;
        if (al == Align::Stretch) { cs = cross0; cl = cross_len; }
        else {
            cl = it.cross;
            if (al == Align::Start) cs = cross0;
            else if (al == Align::Center) cs = cross0 + (cross_len - cl) * 0.5f;
            else cs = cross0 + (cross_len - cl);
        }
        out[i] = row ? Rect{a, cs, sz[i], cl} : Rect{cs, a, cl, sz[i]};
        pos = b + mbk[i] + gap + between;
    }
    return out;
}

} // namespace fl
