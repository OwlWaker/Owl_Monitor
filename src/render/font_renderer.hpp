#pragma once

#include "deps/observer_ptr.hpp"
#include "types/color.hpp"
#include "types/rect.hpp"
#include <unordered_map>
#include <vector>

class Renderer;

// 基于 stb_truetype 的文本渲染器。
// 采样策略：字形位图按 oversample_ 倍（= 渲染分辨率/点分辨率）生成，使每个位图像素
// 对齐一个物理像素，再按点空间尺寸显示，保证 Retina 下文字清晰锐利。
// Text renderer based on stb_truetype, sampling glyph bitmaps at the render resolution.
class FontRenderer {
public:
    explicit FontRenderer(Renderer& renderer);
    ~FontRenderer();

    FontRenderer(const FontRenderer&) = delete;
    FontRenderer& operator=(const FontRenderer&) = delete;

    // 初始化字体数据：加载系统字体文件，建立指定字号/过采样倍率的基础度量；
    // 并把 oversample_ 同步给 Renderer 的 bitmap_scale_（用于位图显示尺寸换算）。
    // Load a system font and set the bitmap sampling scale on the renderer.
    bool init(float base_size = 24.0f, float oversample = 2.0f);
    // 释放字体数据与图集缓存
    // Release the font data and atlas caches
    void shutdown();

    // 在指定位置绘制文本（x,y 为点空间左基线起点，偏移由 Renderer 统一应用）
    // Draw text at the given position (x,y in points, offset applied by the Renderer)
    void font_draw(float x, float y, const char* text, Color c, float size);
    // 计算文本的渲染宽度（点单位，用于对齐/布局）
    // Compute the rendered text width in points (for alignment/layout)
    float font_text_width(const char* text, float size);
    // 计算文本墨迹的上下边界（点单位，用于视觉垂直居中）
    // Compute the ink top and bottom extents in points (for optical vertical centering)
    void font_text_ink_extent(const char* text, float size, float& top, float& bottom);

private:
    // 单个字形的图集记录
    // Atlas record of a single glyph
    struct Glyph { float u0,v0,u1,v1; int w,h,xoff,yoff; float xadvance; };

    // 某个字号对应的字形缓存（位图 + 度量），避免每帧重复栅格化
    // Glyph cache for one font size; avoids re-rasterizing every frame
    struct FontSizeCache {
        static constexpr int kMaxGlyphs = 2048;  // 每字号缓存字形数上限
        // 单个已栅格化字形的缓存记录
        // A cached, already-rasterized glyph
        struct CachedGlyph {
            std::vector<unsigned char> alpha;  // 位图 alpha 数据
            int w = 0, h = 0, xoff = 0, yoff = 0;
            float xadvance = 0;                // 前进量（点单位）
        };
        // 字体缩放与行高
        // Font scale and line metrics
        float  font_scale = 0;
        int    ascent = 0, descent = 0;
        // 按 codepoint 的字形缓存
        std::unordered_map<unsigned int, CachedGlyph> glyphs;
    };

    // 获取（或创建）指定字号（含过采样）的字形缓存
    // Get (or create) the glyph cache for a given effective size
    FontSizeCache& cache_for_size(int effective);

    // 所属渲染器
    // Owning renderer
    Renderer& renderer_;

    // stb_truetype 字体数据与信息
    // stb_truetype font data and info
    void*  font_data_ = nullptr;
    void*  font_info_ = nullptr;
    // 过采样倍率
    // Oversample factor
    float  oversample_ = 2.0f;
    // 按字号索引的图集缓存
    // Atlas caches indexed by font size
    std::unordered_map<int, FontSizeCache> size_caches_;
};

