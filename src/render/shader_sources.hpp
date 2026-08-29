#pragma once

#include <cstddef>
#include <cstdint>

// Vulkan 着色器全部集成在本文件，运行时无需任何外部着色器文件：
//  1) GLSL 源码字符串（kPrimitiveVertSrc / kPrimitiveFragSrc）——构建期由
//     CMake 从本文件提取，再用 glslangValidator 编译为 SPIR-V；
//  2) 编译产物（kPrimitiveVert[] / kPrimitiveFrag[]）——构建期生成的 .inc 数组直接嵌入。
// 修改 GLSL 后重新构建即可（hpp 变更会自动触发重新提取与重新编译）。
// All shaders live in this file: GLSL sources extracted and compiled by CMake at build
// time, and the resulting SPIR-V arrays embedded directly — no shader files at runtime.
namespace owl_shaders {

// —— GLSL 源码（构建期输入，勿改动下述字符串的定界格式）——
// —— GLSL sources; do not change the R"(" ... ")" delimiters ——

// 实例化图元顶点着色器：每个实例对应一个 Primitive，展开为全屏三角形两枚，
// 把矩形从点坐标映射到 NDC（Vulkan NDC 的 Y 轴向下，与 framebuffer 同向，无需翻转）。
// Instanced primitive vertex shader: expands each primitive into two fullscreen
// triangles and maps the rect to NDC (Vulkan NDC Y is down, same as the framebuffer).
inline constexpr const char* kPrimitiveVertSrc = R"(#version 450

struct Primitive {
    vec4 rect;
    vec4 radii;
    vec4 fill;
    vec4 border;
    vec4 params;
    vec4 clip;
};

layout(std430, set = 0, binding = 1) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};

layout(location = 0) out vec2 local_position;
layout(location = 1) flat out uint primitive_index;
layout(push_constant) uniform Viewport {
    vec2 screen_size;
};

void main() {
    const vec2 corners[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1));
    Primitive primitive = primitives[gl_InstanceIndex];
    local_position = corners[gl_VertexIndex];
    primitive_index = gl_InstanceIndex;
    vec2 pixel = primitive.rect.xy + local_position * primitive.rect.zw;
    // Vulkan 的 NDC Y 轴与 framebuffer 同向（向下），直接映射即可，不能像 OpenGL 那样翻转
    vec2 clip_position = pixel;
    gl_Position = vec4(clip_position.x / screen_size.x * 2.0 - 1.0, clip_position.y / screen_size.y * 2.0 - 1.0, 0.0, 1.0);
}
)";

// 实例化图元片元着色器（SDF 圆角矩形 / 描边 / 柔和投影 / 文本位图）
// Instanced primitive fragment shader
inline constexpr const char* kPrimitiveFragSrc = R"(#version 450

struct Primitive {
    vec4 rect;
    vec4 radii;
    vec4 fill;
    vec4 border;
    vec4 params;
    vec4 clip;
};

layout(std430, set = 0, binding = 1) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};
layout(std430, set = 0, binding = 2) readonly buffer BitmapBuffer {
    uint bitmap_data[];
};

layout(location = 0) in vec2 local_position;
layout(location = 1) flat in uint primitive_index;
layout(location = 0) out vec4 out_color;

float rounded_box(vec2 point, vec2 half_size, vec4 radii) {
    float radius = point.x < 0.0 ? (point.y < 0.0 ? radii.x : radii.w) : (point.y < 0.0 ? radii.y : radii.z);
    vec2 q = abs(point) - half_size + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

void main() {
    Primitive primitive = primitives[primitive_index];
    vec2 pixel = primitive.rect.xy + local_position * primitive.rect.zw;
    if (pixel.x < primitive.clip.x || pixel.y < primitive.clip.y || pixel.x >= primitive.clip.x + primitive.clip.z || pixel.y >= primitive.clip.y + primitive.clip.w) discard;
    if (primitive.params.z > 0.0 && primitive.params.w > 0.0) {
        const ivec2 bmp_size = ivec2(primitive.params.z, primitive.params.w);
        const uint base = uint(primitive.params.y);
        // 彩色图标位图（RGBA8 打包：R=低8位...A=高8位），params.x==2 表示彩色
        if (primitive.params.x == 2.0) {
            const float bx = float(bmp_size.x), by = float(bmp_size.y);
            const ivec2 i = ivec2(clamp(local_position * vec2(bx, by), vec2(0.0), vec2(bx - 1.0, by - 1.0)));
            const uint c = bitmap_data[base + uint(i.y) * uint(bmp_size.x) + uint(i.x)];
            // RGBA8 解包：R/G/B 各自除以 255 归一；alpha 由最高字节给出，不要再重复除以 255
            const float a = float(c >> 24u) / 255.0 * primitive.fill.a;
            if (a <= 0.0) discard;
            out_color = vec4(float(c & 255u) / 255.0, float((c >> 8u) & 255u) / 255.0, float((c >> 16u) & 255u) / 255.0, a);
            return;
        }
        // 位图按超采样（oversample）渲染，显示尺寸小于位图；此处双线性过滤平滑缩小，
        // 配合超采样使字体边缘清晰不发虚
        vec2 p = local_position * vec2(bmp_size) - 0.5;
        vec2 f = fract(p);
        ivec2 i = ivec2(floor(p));
        const int mx = bmp_size.x - 1, my = bmp_size.y - 1;
        float a00 = float(bitmap_data[base + uint(clamp(i.y, 0, my)) * uint(bmp_size.x) + uint(clamp(i.x, 0, mx))]) / 255.0;
        float a10 = float(bitmap_data[base + uint(clamp(i.y, 0, my)) * uint(bmp_size.x) + uint(clamp(i.x + 1, 0, mx))]) / 255.0;
        float a01 = float(bitmap_data[base + uint(clamp(i.y + 1, 0, my)) * uint(bmp_size.x) + uint(clamp(i.x, 0, mx))]) / 255.0;
        float a11 = float(bitmap_data[base + uint(clamp(i.y + 1, 0, my)) * uint(bmp_size.x) + uint(clamp(i.x + 1, 0, mx))]) / 255.0;
        float alpha = primitive.fill.a * mix(mix(a00, a10, f.x), mix(a01, a11, f.x), f.y);
        if (alpha <= 0.0) discard;
        out_color = vec4(primitive.fill.rgb, alpha);
        return;
    }
    // 柔和投影：alpha 从形状边缘向外随 spread 线性淡出
    // Soft drop shadow: alpha fades outward from the shape edge over `spread` pixels
    if (primitive.params.y == 1.0) {
        float d = rounded_box(pixel - (primitive.rect.xy + primitive.rect.zw * 0.5), primitive.rect.zw * 0.5, primitive.radii);
        float spread = max(primitive.params.x, 0.0001);
        // 阴影矩形比原形状大 spread，故距形状边缘 ≈ d + spread
        float dist_to_edge = d + spread;
        float alpha = primitive.fill.a * (1.0 - smoothstep(0.0, spread, dist_to_edge));
        if (alpha <= 0.0) discard;
        out_color = vec4(primitive.fill.rgb, alpha);
        return;
    }
    float distance = rounded_box(pixel - (primitive.rect.xy + primitive.rect.zw * 0.5), primitive.rect.zw * 0.5, primitive.radii);
    float aa = max(fwidth(distance), 0.75);
    float outer = 1.0 - smoothstep(0.0, aa, distance);
    float inner = 1.0 - smoothstep(0.0, aa, distance + primitive.params.x);
    float fill_alpha = primitive.fill.a * inner;
    float border_alpha = primitive.border.a * max(outer - inner, 0.0);
    vec3 color = primitive.fill.rgb * fill_alpha + primitive.border.rgb * border_alpha;
    float alpha = fill_alpha + border_alpha;
    if (alpha <= 0.0) discard;
    out_color = vec4(color / alpha, alpha);
}
)";

// —— 编译产物（构建期生成，请勿手改）——
// —— Compiled SPIR-V, generated at build time, do not edit ——

inline constexpr uint32_t kPrimitiveVert[] = {
#include "primitive.vert.inc"
};
inline constexpr uint32_t kPrimitiveFrag[] = {
#include "primitive.frag.inc"
};

// 返回内嵌着色器数组的字数（uint32_t 个数）
// Word count of an embedded shader array
template <typename T, size_t N>
constexpr size_t shader_words(const T (&)[N]) { return N; }

} // namespace owl_shaders

