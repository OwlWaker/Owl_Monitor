#pragma once

#include "types/color.hpp"
#include "types/rect.hpp"
#include "render/font_renderer.hpp"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

// Vulkan 渲染器：CPU 侧把 UI 节点树收集为图元列表（Primitive + 位图 alpha），
// 写入 GPU storage buffer；GPU 以实例化绘制逐图元 SDF 光栅化并呈现到窗口。
// 渲染坐标系为窗口点尺寸（布局空间）；swapchain 按物理像素工作，Retina 下由 GPU 自动 2x 放大。
// Vulkan renderer: CPU collects UI primitives into GPU storage buffers; the GPU
// instance-draws each primitive (SDF rounded rect / bitmap text) and presents.
class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // 初始化 Vulkan：实例、Surface、物理设备、交换链、管线与字体渲染器
    // Initialize Vulkan: instance, surface, device, swapchain, pipeline and font
    bool init();
    // 释放全部 Vulkan 资源（先等待 GPU 空闲，再按创建逆序销毁）
    // Release all Vulkan resources (wait idle, then destroy in reverse order)
    void shutdown();
    // 开始一帧：以窗口点尺寸设置坐标系，清空上一帧收集的图元与位图数据
    // Begin a frame: set the coordinate space from the window size, clear collected data
    void begin_frame(int width, int height);
    // 提交并呈现当前帧：获取交换链图像 → 上传图元/位图 → 录制命令 → 提交 → 呈现
    // Submit and present the current frame: acquire, upload, record, submit, present
    void present();

    // 窗口尺寸变化时重建 swapchain 与依赖尺寸的资源（视图、管线、帧缓冲）
    // Rebuild the swapchain and size-dependent resources on a window resize
    bool handle_resize();

    // 绘制实心矩形
    // Draw a solid rectangle
    void draw_rect(Rect rect, Color c);
    // 绘制圆角矩形
    // Draw a rounded rectangle
    void draw_rounded_rect(Rect rect, float radius, Color c);
    // 绘制圆角描边
    // Draw a rounded outline
    void draw_rounded_outline(Rect rect, float radius, Color c, float thickness);
    // 绘制四角各自圆角的矩形
    // Draw a rectangle with per corner radii
    void draw_rounded_rect_ex(Rect rect, float rad_tl, float rad_tr, float rad_br, float rad_bl, Color c);
    // 绘制四角各自圆角的描边
    // Draw an outline with per corner radii
    void draw_rounded_outline_ex(Rect rect, float rad_tl, float rad_tr, float rad_br, float rad_bl, Color c, float t);
    // 绘制向外扩散的阴影
    // Draw an outward spreading shadow
    void draw_shadow(Rect rect, float radius, Color color, float spread);
    // 绘制四角阴影
    // Draw a shadow with per corner radii
    void draw_shadow_ex(Rect rect, float rad_tl, float rad_tr, float rad_br, float rad_bl, Color color, float spread);

    // 设置裁剪区域
    // Set the scissor region
    void set_scissor(Rect rect);
    // 清除裁剪
    // Clear the scissor
    void clear_scissor();

    // 设置整场景全局透明度（用于显隐淡入淡出）
    // Set the global scene alpha for show hide fading
    void set_alpha(float a);
    // 当前全局透明度
    // Current global alpha
    float alpha() const;

    // 屏幕宽高
    // Screen width and height
    float screen_w() const;
    float screen_h() const;
    // 本帧已收集的图元数量（调试用）
    // Primitive count collected this frame (debug)
    size_t primitive_count() const { return primitives_.size(); }
    // 当前偏移
    // Current offset
    Point off() const;

    // 获取字体渲染器
    // Get the font renderer
    FontRenderer& font();

    // 绘制位图：width/height 为位图像素，显示尺寸 = 位图 / bitmap_scale_
    // （字体按渲染分辨率采样，使每个位图像素对齐一个物理像素，保证清晰）。
    // Draw a bitmap: width/height are bitmap pixels, displayed size = bitmap / bitmap_scale_
    void draw_bitmap(int x, int y, int width, int height, const unsigned char* alpha, Color color);
    // 绘制彩色位图（RGBA8 打包：R=低8位...A=高8位）。x,y 为点空间坐标，
    // 显示尺寸 = 位图 / bitmap_scale_（点）。用于显示应用图标等彩色图像。
    void draw_bitmap_rgba(float x, float y, const uint32_t* rgba, int pw, int ph);
    // 设置位图采样倍率（渲染分辨率 / 点分辨率），由字体渲染器在初始化时设置
    // Set the bitmap sampling scale (render/point), set by the font renderer at init
    void set_bitmap_scale(float scale);

    // 压入与弹出坐标偏移
    // Push and pop a coordinate offset
    void push_offset(Point p);
    void pop_offset();

private:
    bool create_vulkan();
    bool recreate_swapchain();
    // 重建随 swapchain 尺寸变化的资源（视图、管线、帧缓冲）
    // Rebuild size-dependent resources: swapchain views, pipeline and framebuffers
    bool create_pipeline_resources();
    uint32_t find_memory_type(uint32_t filter, VkMemoryPropertyFlags properties) const;
    bool create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory);
    VkImageView create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspect);
    VkShaderModule load_shader(const uint32_t* code, size_t word_count);
    bool create_gpu_pipeline();

    // 矩形绘制核心实现
    // Core rectangle drawing implementation
    void draw_rect_impl(Rect rect, float rad_tl, float rad_tr, float rad_br, float rad_bl, Color fill, Color border, float border_w, bool shadow = false);

    // 屏幕尺寸（窗口点坐标）与当前累积偏移（draw_node 经 push_offset 叠加）
    // Screen size (window points) and the current accumulated offset
    float screen_w_ = 0, screen_h_ = 0;
    Point off_ = {};
    // 整场景全局透明度（用于整体淡入淡出）
    // Global scene alpha applied to everything
    float global_alpha_ = 1.0f;
    // 当前裁剪区域（set_scissor 设置，绘制时随偏移平移）
    // Current scissor rect, translated with the offset
    Rect clip_ = {};
    // 偏移栈：draw_node 进出节点时 push/pop 保存上级偏移
    // Offset stack: push/pop preserves the parent offset while walking the node tree
    std::vector<Point> offset_stack_;

    // 字体渲染器
    // Font renderer
    std::unique_ptr<FontRenderer> font_;

    // 位图采样倍率：渲染分辨率相对布局（点）分辨率的倍数，默认 1（1:1）
    // Bitmap sampling scale: render resolution vs layout (point) resolution, default 1 (1:1)
    float bitmap_scale_ = 1.0f;

    // —— Vulkan 实例、物理设备与逻辑设备 ——
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = 0;
    // —— 交换链：格式、物理尺寸、图像与图像视图 ——
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchain_format_ = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D swapchain_extent_{};
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_views_;
    std::vector<VkFramebuffer> framebuffers_;
    // —— 命令缓冲与同步原语 ——
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
    VkSemaphore image_available_ = VK_NULL_HANDLE;
    VkSemaphore render_finished_ = VK_NULL_HANDLE;
    VkFence in_flight_ = VK_NULL_HANDLE;
    uint32_t image_index_ = 0;
    // —— 渲染管线、布局与描述符 ——
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline primitive_pipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    // —— 图元与位图 storage buffer（CPU 写入、GPU 只读）——
    VkBuffer primitive_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory primitive_memory_ = VK_NULL_HANDLE;
    VkBuffer bitmap_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory bitmap_memory_ = VK_NULL_HANDLE;
    // 单个 UI 图元：矩形、四角圆角、填充/描边色、参数（描边宽/阴影/位图偏移与尺寸）、裁剪
    // A single UI primitive: rect, radii, fill/border colors, params and clip
    struct Primitive {
        float rect[4];
        float radii[4];
        float fill[4];
        float border[4];
        float params[4];
        float clip[4];
    };
    // 本帧 CPU 收集的图元列表与位图 alpha 数据（present 时整块上传）
    // Primitives and bitmap alpha data collected this frame on the CPU
    std::vector<Primitive> primitives_;
    std::vector<uint32_t> bitmap_data_;
};

