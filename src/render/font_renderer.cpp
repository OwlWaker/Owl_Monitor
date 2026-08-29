#include "render/font_renderer.hpp"
#include "render/renderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#define STB_TRUETYPE_IMPLEMENTATION
#include "deps/stb_truetype.h"

static const char* utf8_decode(const char* s, unsigned int* cp) {
    unsigned char c = (unsigned char)*s++;
    if (c < 0x80) *cp = c;
    else if ((c & 0xE0) == 0xC0) *cp = ((c & 0x1F) << 6) | ((unsigned char)*s++ & 0x3F);
    else if ((c & 0xF0) == 0xE0) {
        unsigned char c1 = (unsigned char)*s++, c2 = (unsigned char)*s++;
        *cp = ((c & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
    } else if ((c & 0xF8) == 0xF0) {
        unsigned char c1 = (unsigned char)*s++, c2 = (unsigned char)*s++, c3 = (unsigned char)*s++;
        *cp = ((c & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
    } else *cp = 0xFFFD;
    return s;
}

static unsigned char* read_file_binary(const char* path, size_t* size) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return nullptr;
    std::fseek(file, 0, SEEK_END);
    long length = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (length <= 0) { std::fclose(file); return nullptr; }
    auto* data = (unsigned char*)std::malloc((size_t)length);
    if (!data) { std::fclose(file); return nullptr; }
    *size = std::fread(data, 1, (size_t)length, file);
    std::fclose(file);
    return data;
}

FontRenderer::FontRenderer(Renderer& renderer) : renderer_(renderer) {}
FontRenderer::~FontRenderer() { shutdown(); }

// 初始化字体：依次尝试系统字体路径，用 stb_truetype 解析第一个可用的字体文件，
// 缓存基础度量（缩放比、ascent/descent），并把 oversample_ 同步给渲染器。
// Init: try system font paths, parse the first usable font and cache base metrics.
bool FontRenderer::init(float base_size, float oversample) {
    oversample_ = oversample;
    const char* paths[] = {
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc", nullptr
    };
    for (int i = 0; paths[i]; ++i) {
        size_t size = 0;
        font_data_ = read_file_binary(paths[i], &size);
        if (!font_data_) continue;
        auto* info = (stbtt_fontinfo*)std::malloc(sizeof(stbtt_fontinfo));
        // 解析字体（取第 0 个字体），失败则释放并尝试下一个路径
        // Parse the font (first face); on failure free and try the next path
        if (!info || !stbtt_InitFont(info, (unsigned char*)font_data_, stbtt_GetFontOffsetForIndex((unsigned char*)font_data_, 0))) {
            std::free(info); std::free(font_data_); font_info_ = nullptr; font_data_ = nullptr; continue;
        }
        font_info_ = info;
        // 以基准字号（含过采样）缓存缩放比与字体度量
        // Cache the scale and metrics for the base size (including oversampling)
        const int effective = (int)(base_size * oversample_ + 0.5f);
        FontSizeCache cache;
        cache.font_scale = stbtt_ScaleForPixelHeight(info, (float)effective);
        stbtt_GetFontVMetrics(info, &cache.ascent, &cache.descent, nullptr);
        size_caches_.emplace(effective, cache);
        renderer_.set_bitmap_scale(oversample_);
        return true;
    }
    return false;
}

// 释放字体解析信息与字体文件数据
// Release the font info and font file data
void FontRenderer::shutdown() {
    size_caches_.clear();
    std::free(font_info_); font_info_ = nullptr;
    std::free(font_data_); font_data_ = nullptr;
}

// 获取（或创建）指定字号（含过采样）的字形缓存；缓存度量供对齐使用。
FontRenderer::FontSizeCache& FontRenderer::cache_for_size(int effective) {
    auto it = size_caches_.find(effective);
    if (it == size_caches_.end()) {
        auto* info = (stbtt_fontinfo*)font_info_;
        FontSizeCache c;
        c.font_scale = stbtt_ScaleForPixelHeight(info, (float)effective);
        stbtt_GetFontVMetrics(info, &c.ascent, &c.descent, nullptr);
        it = size_caches_.emplace(effective, std::move(c)).first;
    }
    return it->second;
}

void FontRenderer::font_draw(float x, float y, const char* text, Color color, float size) {
    if (!font_info_ || !text || !*text) return;
    // 偏移由 Renderer::draw_bitmap 统一应用（与 draw_rect_impl 等一致），此处不再叠加。
    // The offset is applied once inside Renderer::draw_bitmap (consistent with draw_rect_impl),
    // so it must NOT be added here again — otherwise text is shifted by an extra off_.
    auto* info = (stbtt_fontinfo*)font_info_;
    // 按渲染分辨率采样：位图以 oversample_（=渲染分辨率/点分辨率）生成，每个位图像素
    // 恰好对齐一个物理像素；显示尺寸 = 位图尺寸 / oversample_（点空间）。
    // Sample at the render resolution: bitmaps are generated at oversample_ (= render/point),
    // so each bitmap pixel lands on one physical pixel; displayed size = bitmap / oversample_ (points).
    const float scale = stbtt_ScaleForPixelHeight(info, size * oversample_);
    const float point_scale = stbtt_ScaleForPixelHeight(info, size);
    int ascent = 0, descent = 0;
    stbtt_GetFontVMetrics(info, &ascent, &descent, nullptr);
    // 该字号（含过采样）的字形缓存：字形首次出现时栅格化并缓存，后续直接复用位图
    const int effective = (int)(size * oversample_ + 0.5f);
    FontSizeCache& cache = cache_for_size(effective);
    // 在物理像素（oversample）空间计算 pen 与基线：字符位置在物理像素上取整，
    // 避免 /oversample 引入 0.5 点精度损失导致英文字符上下左右错位。
    // Work in physical pixels so each glyph snaps to a physical-pixel boundary,
    // avoiding 0.5pt misalignment of Latin letters inside a word.
    float pen_px = x * oversample_;
    const float baseline_px = (y + (float)ascent * point_scale) * oversample_;
    const char* p = text;
    while (*p) {
        unsigned int cp = 0;
        p = utf8_decode(p, &cp);
        auto it = cache.glyphs.find(cp);
        if (it == cache.glyphs.end()) {
            // 未命中：栅格化一次并缓存（上限满时清空该字号缓存）
            if (cache.glyphs.size() >= (size_t)FontSizeCache::kMaxGlyphs) cache.glyphs.clear();
            int advance = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(info, (int)cp, &advance, &lsb);
            FontSizeCache::CachedGlyph g;
            g.xadvance = (float)advance * point_scale;
            unsigned char* bitmap = stbtt_GetCodepointBitmap(info, scale, scale, (int)cp, &g.w, &g.h, &g.xoff, &g.yoff);
            if (bitmap) {
                if (g.w > 0 && g.h > 0) g.alpha.assign(bitmap, bitmap + (size_t)g.w * g.h);
                stbtt_FreeBitmap(bitmap, nullptr);
            }
            it = cache.glyphs.emplace(cp, std::move(g)).first;
        }
        const FontSizeCache::CachedGlyph& g = it->second;
        if (!g.alpha.empty()) {
            const int x_px = (int)std::floor(pen_px + (float)g.xoff + 0.5f);
            const int y_px = (int)std::floor(baseline_px + (float)g.yoff + 0.5f);
            renderer_.draw_bitmap(x_px, y_px, g.w, g.h, g.alpha.data(), color);
        }
        pen_px += g.xadvance * oversample_;
    }
}

// 计算文本渲染宽度（点单位）。
// 遍历每个字符累加 advance，并用点空间缩放比（含过采样再除回）换算。
// Compute the rendered text width in points by summing advances.
float FontRenderer::font_text_width(const char* text, float size) {
    if (!font_info_ || !text) return 0;
    auto* info = (stbtt_fontinfo*)font_info_;
    // 点空间缩放：先按 size*oversample 取缩放，再除以 oversample 回到点单位
    // Point-space scale: sample at size*oversample, then divide back to points
    const float scale = stbtt_ScaleForPixelHeight(info, size * oversample_) / oversample_;
    float width = 0;
    const char* p = text;
    while (*p) {
        unsigned int cp = 0; p = utf8_decode(p, &cp);
        int advance = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(info, (int)cp, &advance, &lsb);
        width += advance * scale;
    }
    return width;
}

// 计算文本墨迹的上下边界（点单位），供文本在给定高度内视觉垂直居中。
// 取每个字形位图框（bitmap box）相对基线的 y 范围并集。
// Compute the ink top/bottom in points by unioning each glyph's bitmap box.
void FontRenderer::font_text_ink_extent(const char* text, float size, float& top, float& bottom) {
    if (!font_info_ || !text) { top = bottom = 0; return; }
    auto* info = (stbtt_fontinfo*)font_info_;
    // 点空间缩放比（同上）
    // Point-space scale as above
    const float scale = stbtt_ScaleForPixelHeight(info, size * oversample_) / oversample_;
    int ascent = 0, descent = 0;
    stbtt_GetFontVMetrics(info, &ascent, &descent, nullptr);
    const float baseline = ascent * scale;
    float ink_top = std::numeric_limits<float>::max();
    float ink_bottom = std::numeric_limits<float>::lowest();
    const char* p = text;
    while (*p) {
        unsigned int cp = 0;
        p = utf8_decode(p, &cp);
        // 取单个字形的位图框，累积求全局墨迹上下界
        // Get each glyph's bitmap box and union the global ink bounds
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBox(info, (int)cp, scale, scale, &x0, &y0, &x1, &y1);
        ink_top = std::min(ink_top, baseline + (float)y0);
        ink_bottom = std::max(ink_bottom, baseline + (float)y1);
    }
    // 空文本时退回基线高度；否则返回实际墨迹范围
    // Empty text falls back to the baseline; otherwise return the real ink range
    if (ink_top == std::numeric_limits<float>::max()) { top = 0; bottom = baseline; }
    else { top = ink_top; bottom = ink_bottom; }
}
