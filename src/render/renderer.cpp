#include "render/renderer.hpp"
#include "render/font_renderer.hpp"
#include "render/shader_sources.hpp"
#include "platform/platform.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <limits>

namespace {
// 把透明度钳制到 [0,1]，供全局 alpha 与描边等使用
// Clamp a value to [0,1], used for global alpha and borders
float clamp01(float value) { return std::max(0.0f, std::min(1.0f, value)); }
}

Renderer::~Renderer() { shutdown(); }

// 初始化渲染器：先创建字体渲染器，再初始化整个 Vulkan 栈
// Initialize: create the font renderer, then bring up the whole Vulkan stack
bool Renderer::init() {
    font_ = std::make_unique<FontRenderer>(*this);
    return create_vulkan();
}

// 释放全部资源：先让 GPU 空闲，再按创建逆序销毁帧缓冲、视图、管线、描述符、
// 同步原语、命令池、缓冲区、交换链、设备、Surface 与实例。
// Release everything: wait idle, then destroy in reverse creation order.
void Renderer::shutdown() {
    if (!device_) return;
    vkDeviceWaitIdle(device_);
    for (VkFramebuffer framebuffer : framebuffers_) vkDestroyFramebuffer(device_, framebuffer, nullptr);
    for (VkImageView view : swapchain_views_) vkDestroyImageView(device_, view, nullptr);
    if (primitive_pipeline_) vkDestroyPipeline(device_, primitive_pipeline_, nullptr);
    if (pipeline_layout_) vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
    if (render_pass_) vkDestroyRenderPass(device_, render_pass_, nullptr);
    if (descriptor_pool_) vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
    if (descriptor_layout_) vkDestroyDescriptorSetLayout(device_, descriptor_layout_, nullptr);
    if (in_flight_) vkDestroyFence(device_, in_flight_, nullptr);
    if (image_available_) vkDestroySemaphore(device_, image_available_, nullptr);
    if (render_finished_) vkDestroySemaphore(device_, render_finished_, nullptr);
    if (command_pool_) vkDestroyCommandPool(device_, command_pool_, nullptr);
    if (primitive_buffer_) vkDestroyBuffer(device_, primitive_buffer_, nullptr);
    if (primitive_memory_) vkFreeMemory(device_, primitive_memory_, nullptr);
    if (bitmap_buffer_) vkDestroyBuffer(device_, bitmap_buffer_, nullptr);
    if (bitmap_memory_) vkFreeMemory(device_, bitmap_memory_, nullptr);
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    vkDestroyDevice(device_, nullptr);
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);
    device_ = VK_NULL_HANDLE; instance_ = VK_NULL_HANDLE;
}

// 创建完整 Vulkan 栈：实例 → Surface → 物理设备（含队列族）→ 逻辑设备 →
// 交换链 → 命令池/命令缓冲/同步原语 → 图形管线。任一步失败即返回 false。
// Bring up the full Vulkan stack; any failure returns false.
bool Renderer::create_vulkan() {
    // 从 GLFW 获取窗口系统扩展名，并查询 Vulkan loader 可用性
    // Get windowing extensions from GLFW and check the Vulkan loader
    uint32_t extension_count = 0;
    if (!glfwVulkanSupported()) { std::fprintf(stderr, "Vulkan: GLFW cannot load Vulkan loader\n"); return false; }
    const char** extensions = glfwGetRequiredInstanceExtensions(&extension_count);
    if (!extensions) { std::fprintf(stderr, "Vulkan: GLFW extensions unavailable\n"); return false; }
    std::vector<const char*> instance_extensions(extensions, extensions + extension_count);
#if defined(__APPLE__)
    // MoltenVK 需要额外的可移植性枚举扩展
    // MoltenVK needs the portability enumeration extension
    instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

    // 创建 Vulkan 实例（应用信息 + 扩展 + 可移植性标志）
    // Create the Vulkan instance with app info, extensions and the portability flag
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "OwlMonitor"; app.applicationVersion = 1;
    app.pEngineName = "OwlMonitor"; app.engineVersion = 1; app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &app;
    instance_info.enabledExtensionCount = (uint32_t)instance_extensions.size();
    instance_info.ppEnabledExtensionNames = instance_extensions.data();
#if defined(__APPLE__)
    instance_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    VkResult result = vkCreateInstance(&instance_info, nullptr, &instance_);
    if (result != VK_SUCCESS) { std::fprintf(stderr, "Vulkan: instance failed (%d)\n", result); return false; }

    // 用 GLFW 为窗口创建 Vulkan Surface
    // Create the Vulkan surface for the window via GLFW
    surface_ = platform_create_vulkan_surface(instance_);
    if (!surface_) { std::fprintf(stderr, "Vulkan: surface failed\n"); return false; }

    // 枚举物理设备，选一个同时支持图形队列且能呈现到 Surface 的
    // Enumerate devices and pick one with a graphics queue that can present
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
    if (!device_count) { std::fprintf(stderr, "Vulkan: no physical devices (%d)\n", device_count); return false; }
    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());
    for (VkPhysicalDevice candidate : devices) {
        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, families.data());
        for (uint32_t i = 0; i < family_count; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface_, &present);
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                physical_device_ = candidate; queue_family_ = i; break;
            }
        }
        if (physical_device_) break;
    }
    if (!physical_device_) { std::fprintf(stderr, "Vulkan: no graphics/present queue\n"); return false; }

    // 创建逻辑设备：启用交换链扩展，请求一个图形/呈现队列
    // Create the logical device with the swapchain extension and one queue
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = queue_family_; queue_info.queueCount = 1; queue_info.pQueuePriorities = &priority;
    const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount = 1; device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1; device_info.ppEnabledExtensionNames = device_extensions;
    result = vkCreateDevice(physical_device_, &device_info, nullptr, &device_);
    if (result != VK_SUCCESS) { std::fprintf(stderr, "Vulkan: device failed (%d)\n", result); return false; }
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

    // 创建交换链（图像、视图在 create_pipeline_resources 中完成）
    // Create the swapchain (views are built in create_pipeline_resources)
    if (!recreate_swapchain()) { std::fprintf(stderr, "Vulkan: swapchain failed\n"); return false; }

    // 命令池（允许重置命令缓冲）与主命令缓冲
    // Command pool (allowing reset) and the primary command buffer
    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.queueFamilyIndex = queue_family_; pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_) != VK_SUCCESS) return false;
    VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_info.commandPool = command_pool_; command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; command_info.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device_, &command_info, &command_buffer_) != VK_SUCCESS) return false;
    VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if (vkCreateSemaphore(device_, &semaphore_info, nullptr, &image_available_) != VK_SUCCESS) return false;
    if (vkCreateSemaphore(device_, &semaphore_info, nullptr, &render_finished_) != VK_SUCCESS) return false;
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(device_, &fence_info, nullptr, &in_flight_) != VK_SUCCESS) return false;
    return create_gpu_pipeline();
}

// 创建交换链：查询 Surface 能力与格式，选择 32 位 BGRA 或首个可用格式，
// 以当前尺寸（物理像素）与 FIFO 呈现模式创建，并取回交换链图像。
// Create the swapchain: query capabilities/formats, prefer BGRA8, use the current
// (physical pixel) extent and FIFO present mode, then fetch the swapchain images.
bool Renderer::recreate_swapchain() {
    VkSurfaceCapabilitiesKHR capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &capabilities);
    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr);
    if (!format_count) return false;
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, formats.data());
    // 默认用首个格式，优先选 B8G8R8A8_UNORM（常见、与混合/呈现最兼容）
    // Default to the first format, prefer B8G8R8A8_UNORM when available
    swapchain_format_ = formats[0].format;
    for (auto format : formats) if (format.format == VK_FORMAT_B8G8R8A8_UNORM) swapchain_format_ = format.format;
    // currentExtent 为实际帧缓冲尺寸（Retina 下是物理像素）；若未定义则回退 1280x800
    // currentExtent is the real framebuffer size; fall back to 1280x800 when undefined
    swapchain_extent_ = capabilities.currentExtent;
    if (swapchain_extent_.width == std::numeric_limits<uint32_t>::max()) swapchain_extent_ = {1280, 800};
    // 双缓冲起步，并按能力钳制
    // Double buffering at least, clamped by the device capabilities
    uint32_t image_count = std::max(2u, capabilities.minImageCount);
    if (capabilities.maxImageCount) image_count = std::min(image_count, capabilities.maxImageCount);
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface_; info.minImageCount = image_count; info.imageFormat = swapchain_format_; info.imageColorSpace = formats[0].colorSpace;
    info.imageExtent = swapchain_extent_; info.imageArrayLayers = 1; info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; info.presentMode = VK_PRESENT_MODE_FIFO_KHR; info.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_) != VK_SUCCESS) return false;
    uint32_t count = 0; vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr); swapchain_images_.resize(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, swapchain_images_.data());
    return true;
}

// 按内存类型位掩码找符合属性要求的索引（用于可主机访问的缓冲区）
// Find a memory type index matching the required properties for the given mask
uint32_t Renderer::find_memory_type(uint32_t filter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memory{}; vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory);
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (memory.memoryTypes[i].propertyFlags & properties) == properties) return i;
    return UINT32_MAX;
}

// 创建缓冲区并绑定合适的内存（一般取 HOST_VISIBLE | HOST_COHERENT 以便 CPU 直写）
// Create a buffer and bind suitable memory (HOST_VISIBLE|HOST_COHERENT for CPU writes)
bool Renderer::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size = size; info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &info, nullptr, &buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(device_, buffer, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = find_memory_type(requirements.memoryTypeBits, properties);
    if (allocation.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(device_, &allocation, nullptr, &memory) != VK_SUCCESS) return false;
    return vkBindBufferMemory(device_, buffer, memory, 0) == VK_SUCCESS;
}

// 为一张图像创建 2D 图像视图（指定格式与访问方面）
// Create a 2D image view for an image with the given format and aspect
VkImageView Renderer::create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspect) {
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = image; info.viewType = VK_IMAGE_VIEW_TYPE_2D; info.format = format;
    info.subresourceRange.aspectMask = aspect; info.subresourceRange.levelCount = 1; info.subresourceRange.layerCount = 1;
    VkImageView view = VK_NULL_HANDLE;
    return vkCreateImageView(device_, &info, nullptr, &view) == VK_SUCCESS ? view : VK_NULL_HANDLE;
}

// 从内嵌的 SPIR-V 数组创建着色器模块（数组由 shader_sources.hpp 在构建期生成）
// Create a shader module from an embedded SPIR-V array (generated at build time)
VkShaderModule Renderer::load_shader(const uint32_t* code, size_t word_count) {
    if (!code || word_count == 0) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = word_count * sizeof(uint32_t); info.pCode = code;
    VkShaderModule shader = VK_NULL_HANDLE;
    return vkCreateShaderModule(device_, &info, nullptr, &shader) == VK_SUCCESS ? shader : VK_NULL_HANDLE;
}

// 创建 GPU 管线的静态资源：图元/位图 storage buffer、描述符布局/池/集、
// 渲染通道、管线布局，最后按交换链尺寸重建视图与帧缓冲（create_pipeline_resources）。
// Create static pipeline resources: buffers, descriptors, render pass, layout and framebuffers.
bool Renderer::create_gpu_pipeline() {
    // 图元 storage buffer：上限 32768 个图元；位图 buffer：上限 8MB alpha 数据
    // Primitive storage buffer capped at 32768 primitives; bitmap buffer capped at 8MB alpha
    if (!create_buffer(sizeof(Primitive) * 32768, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        primitive_buffer_, primitive_memory_)) return false;
    if (!create_buffer(sizeof(uint32_t) * 8 * 1024 * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        bitmap_buffer_, bitmap_memory_)) return false;
    // 描述符布局仅含着色器实际使用的两个 storage buffer（binding 1 = 图元，2 = 位图）
    // Descriptor layout only has the two storage buffers the shaders actually use
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 1; bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; bindings[0].descriptorCount = 1; bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 2; bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; bindings[1].descriptorCount = 1; bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; layout_info.bindingCount = 2; layout_info.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &descriptor_layout_) != VK_SUCCESS) return false;
    // 描述符池与单个集合，绑定上述两个 buffer
    // Descriptor pool and a single set binding both buffers
    VkDescriptorPoolSize pool_sizes[1]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2}};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pool_info.maxSets = 1; pool_info.poolSizeCount = 1; pool_info.pPoolSizes = pool_sizes;
    if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &descriptor_pool_) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; set_info.descriptorPool = descriptor_pool_; set_info.descriptorSetCount = 1; set_info.pSetLayouts = &descriptor_layout_;
    if (vkAllocateDescriptorSets(device_, &set_info, &descriptor_set_) != VK_SUCCESS) return false;
    // 把两个 buffer 写入描述符集（buffer 句柄 + 内存范围）
    // Write both buffers into the descriptor set
    VkDescriptorBufferInfo primitive_buffer_info{primitive_buffer_, 0, sizeof(Primitive) * 32768};
    VkWriteDescriptorSet primitive_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; primitive_write.dstSet = descriptor_set_; primitive_write.dstBinding = 1; primitive_write.descriptorCount = 1; primitive_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; primitive_write.pBufferInfo = &primitive_buffer_info;
    VkDescriptorBufferInfo bitmap_buffer_info{bitmap_buffer_, 0, sizeof(uint32_t) * 8 * 1024 * 1024};
    VkWriteDescriptorSet bitmap_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; bitmap_write.dstSet = descriptor_set_; bitmap_write.dstBinding = 2; bitmap_write.descriptorCount = 1; bitmap_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; bitmap_write.pBufferInfo = &bitmap_buffer_info;
    VkWriteDescriptorSet writes[2]{primitive_write, bitmap_write};
    vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);

    // 渲染通道：单颜色附件，从 CLEAR 到 PRESENT_SRC，Alpha 混合开启
    // Render pass: one color attachment, CLEAR -> PRESENT_SRC, blending enabled
    VkAttachmentDescription attachment{}; attachment.format = swapchain_format_; attachment.samples = VK_SAMPLE_COUNT_1_BIT; attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &reference;
    VkRenderPassCreateInfo render_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; render_info.attachmentCount = 1; render_info.pAttachments = &attachment; render_info.subpassCount = 1; render_info.pSubpasses = &subpass;
    if (vkCreateRenderPass(device_, &render_info, nullptr, &render_pass_) != VK_SUCCESS) return false;
    // 管线布局：一个 push constant（vec2 屏幕尺寸，供顶点着色器映射 NDC）
    // Pipeline layout with one push constant: vec2 screen size for NDC mapping
    VkPushConstantRange push_constants{}; push_constants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; push_constants.offset = 0; push_constants.size = sizeof(float) * 2;
    VkPipelineLayoutCreateInfo pipeline_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pipeline_layout_info.setLayoutCount = 1; pipeline_layout_info.pSetLayouts = &descriptor_layout_; pipeline_layout_info.pushConstantRangeCount = 1; pipeline_layout_info.pPushConstantRanges = &push_constants;
    if (vkCreatePipelineLayout(device_, &pipeline_layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS) return false;
    return create_pipeline_resources();
}

// 重建随 swapchain 尺寸变化的资源：为每张交换链图像建视图、加载并链接着色器、
// 以物理像素视口创建图形管线，并为每张视图创建帧缓冲。
// Rebuild size-dependent resources: image views, pipeline (physical-pixel viewport) and framebuffers.
bool Renderer::create_pipeline_resources() {
    // 为每张交换链图像创建颜色图像视图
    // Create a color image view for every swapchain image
    swapchain_views_.clear();
    for (VkImage image : swapchain_images_) {
        VkImageView view = create_image_view(image, swapchain_format_, VK_IMAGE_ASPECT_COLOR_BIT);
        if (!view) return false;
        swapchain_views_.push_back(view);
    }
    // 从内嵌 SPIR-V 数组创建顶点与片元着色器模块
    // Load the vertex and fragment shader modules from embedded SPIR-V
    VkShaderModule primitive_vertex = load_shader(owl_shaders::kPrimitiveVert, owl_shaders::shader_words(owl_shaders::kPrimitiveVert));
    VkShaderModule primitive_fragment = load_shader(owl_shaders::kPrimitiveFrag, owl_shaders::shader_words(owl_shaders::kPrimitiveFrag));
    if (!primitive_vertex || !primitive_fragment) return false;
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, primitive_vertex, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, primitive_fragment, "main", nullptr};
    // 无顶点缓冲（数据全在 storage buffer，由实例索引取用）；三角形列表
    // No vertex buffer (data lives in storage buffers, indexed by instance); triangle list
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    // 视口为物理像素（swapchain 尺寸）；坐标系（点）经 NDC 由 GPU 放大到物理像素
    // Viewport is in physical pixels; the point coordinate space is upscaled via NDC
    VkViewport viewport{0, 0, (float)swapchain_extent_.width, (float)swapchain_extent_.height, 0, 1}; VkRect2D scissor{{0, 0}, swapchain_extent_};
    VkPipelineViewportStateCreateInfo viewport_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; viewport_state.viewportCount = 1; viewport_state.pViewports = &viewport; viewport_state.scissorCount = 1; viewport_state.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE; raster.frontFace = VK_FRONT_FACE_CLOCKWISE; raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    // Alpha 混合：src_alpha / one-minus-src_alpha（支持抗锯齿边缘与半透明）
    // Alpha blending: src_alpha / one-minus-src_alpha for AA edges and translucency
    VkPipelineColorBlendAttachmentState blend{}; blend.blendEnable = VK_TRUE; blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; blend.colorBlendOp = VK_BLEND_OP_ADD; blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; blend.alphaBlendOp = VK_BLEND_OP_ADD; blend.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo blend_state{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; blend_state.attachmentCount = 1; blend_state.pAttachments = &blend;
    // 组装并创建图形管线
    // Assemble and create the graphics pipeline
    VkGraphicsPipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.stageCount = 2; pipeline_info.pStages = stages; pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &assembly; pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &raster; pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &blend_state; pipeline_info.layout = pipeline_layout_; pipeline_info.renderPass = render_pass_; pipeline_info.subpass = 0;
    const bool created = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &primitive_pipeline_) == VK_SUCCESS;
    vkDestroyShaderModule(device_, primitive_vertex, nullptr); vkDestroyShaderModule(device_, primitive_fragment, nullptr);
    if (!created) return false;
    // 为每张交换链图像视图创建帧缓冲（尺寸与 swapchain 一致）
    // Create a framebuffer per swapchain image view
    framebuffers_.clear();
    for (VkImageView view : swapchain_views_) { VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; framebuffer.renderPass = render_pass_; framebuffer.attachmentCount = 1; framebuffer.pAttachments = &view; framebuffer.width = swapchain_extent_.width; framebuffer.height = swapchain_extent_.height; framebuffer.layers = 1; VkFramebuffer handle = VK_NULL_HANDLE; if (vkCreateFramebuffer(device_, &framebuffer, nullptr, &handle) != VK_SUCCESS) return false; framebuffers_.push_back(handle); }
    return true;
}

// 窗口尺寸变化：等 GPU 空闲后销毁旧帧缓冲、视图与管线，重建交换链及依赖尺寸的资源
// Window resize: wait idle, destroy old size-dependent resources, rebuild swapchain and pipeline
bool Renderer::handle_resize() {
    if (!device_) return false;
    vkDeviceWaitIdle(device_);
    for (VkFramebuffer f : framebuffers_) vkDestroyFramebuffer(device_, f, nullptr);
    framebuffers_.clear();
    for (VkImageView v : swapchain_views_) vkDestroyImageView(device_, v, nullptr);
    swapchain_views_.clear();
    if (primitive_pipeline_) { vkDestroyPipeline(device_, primitive_pipeline_, nullptr); primitive_pipeline_ = VK_NULL_HANDLE; }
    if (swapchain_) { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }
    if (!recreate_swapchain()) return false;
    return create_pipeline_resources();
}

// 开始一帧：以窗口点尺寸设置坐标系，清空上一帧收集的图元与位图。
// 注意坐标系是点（布局）空间；swapchain/framebuffer 按物理像素在 present 内部处理，
// Retina 下 GPU 通过 NDC（screen_size=点）自动 2x 放大。
// Begin a frame: set the coordinate space from the window point size and clear collected data.
void Renderer::begin_frame(int width, int height) {
    screen_w_ = (float)width; screen_h_ = (float)height;
    clip_ = {0, 0, screen_w_, screen_h_}; primitives_.clear(); bitmap_data_.clear();
}

// 提交并呈现当前帧：等待上一帧完成 → 获取交换链图像 → 上传图元/位图到 storage buffer →
// 录制命令（清屏深灰 + 实例化绘制）→ 提交队列 → 呈现；遇 OUT_OF_DATE 则重建交换链。
// Submit and present: wait, acquire, upload, record, submit and present; rebuild on OUT_OF_DATE.
void Renderer::present() {
    if (!device_) return;
    // 等待上一帧（用 fence 同步，避免覆盖仍在被读取的交换链图像）
    // Wait for the previous frame via the in-flight fence
    vkWaitForFences(device_, 1, &in_flight_, VK_TRUE, UINT64_MAX);
    // 只有成功取到图像后才重置 fence 并提交；否则保持 signaled，避免下一帧永久等待
    // Only reset the fence after a successful acquire; otherwise keep it signaled
    const VkResult acquire = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, image_available_, VK_NULL_HANDLE, &image_index_);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) { handle_resize(); return; }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) return;
    vkResetFences(device_, 1, &in_flight_);
    // 上传本帧收集的图元数据（超限截断）
    // Upload the collected primitives, truncated if over the cap
    void* mapped = nullptr;
    if (primitives_.size() > 32768) primitives_.resize(32768);
    if (!primitives_.empty()) { vkMapMemory(device_, primitive_memory_, 0, primitives_.size() * sizeof(Primitive), 0, &mapped); std::memcpy(mapped, primitives_.data(), primitives_.size() * sizeof(Primitive)); vkUnmapMemory(device_, primitive_memory_); }
    // 上传本帧收集的位图 alpha 数据（超限截断）
    // Upload the collected bitmap alpha data, truncated if over the cap
    if (bitmap_data_.size() > 8 * 1024 * 1024) bitmap_data_.resize(8 * 1024 * 1024);
    if (!bitmap_data_.empty()) { vkMapMemory(device_, bitmap_memory_, 0, bitmap_data_.size() * sizeof(uint32_t), 0, &mapped); std::memcpy(mapped, bitmap_data_.data(), bitmap_data_.size() * sizeof(uint32_t)); vkUnmapMemory(device_, bitmap_memory_); }
    // 录制命令：清屏（普通窗口深灰背景）后实例化绘制所有图元
    // Record commands: clear to the plain dark-gray window background, then instance-draw
    vkResetCommandBuffer(command_buffer_, 0); VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkBeginCommandBuffer(command_buffer_, &begin);
    VkClearValue clear{};
    // 深色窗口背景（与深色主题一致）
    clear.color.float32[0] = 0.13f; clear.color.float32[1] = 0.15f; clear.color.float32[2] = 0.19f; clear.color.float32[3] = 1.0f;
    VkRenderPassBeginInfo render_begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; render_begin.renderPass = render_pass_; render_begin.framebuffer = framebuffers_[image_index_]; render_begin.renderArea.extent = swapchain_extent_; render_begin.clearValueCount = 1; render_begin.pClearValues = &clear;
    vkCmdBeginRenderPass(command_buffer_, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    if (!primitives_.empty()) {
        vkCmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, primitive_pipeline_);
        vkCmdBindDescriptorSets(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1, &descriptor_set_, 0, nullptr);
        // 推入屏幕尺寸（点）供顶点着色器把坐标映射到 NDC
        // Push the screen size (points) for the vertex shader's NDC mapping
        const float screen_size[2] = {screen_w_, screen_h_};
        vkCmdPushConstants(command_buffer_, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(screen_size), screen_size);
        // 每图元 6 个顶点（全屏三角形两枚），实例数 = 图元数
        // 6 vertices per primitive (two fullscreen triangles), one instance per primitive
        vkCmdDraw(command_buffer_, 6, (uint32_t)primitives_.size(), 0, 0);
    }
    vkCmdEndRenderPass(command_buffer_);
    vkEndCommandBuffer(command_buffer_);
    // 提交命令并呈现；OUT_OF_DATE/SUBOPTIMAL 时重建交换链
    // Submit and present; rebuild on OUT_OF_DATE or SUBOPTIMAL
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT; VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &image_available_; submit.pWaitDstStageMask = &wait_stage; submit.commandBufferCount = 1; submit.pCommandBuffers = &command_buffer_; submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &render_finished_; vkQueueSubmit(queue_, 1, &submit, in_flight_);
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; present.waitSemaphoreCount = 1; present.pWaitSemaphores = &render_finished_; present.swapchainCount = 1; present.pSwapchains = &swapchain_; present.pImageIndices = &image_index_; const VkResult presented = vkQueuePresentKHR(queue_, &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) handle_resize();
}

// 矩形绘制核心实现：平移偏移、钳制圆角、按是否阴影设置参数并提交一个图元。
// 阴影时 params = {spread, 1, 0, 0}，片元着色器按 spread 向外渐变淡化。
// Core rect drawing: translate by offset, clamp radii, pack params and push a primitive.
void Renderer::draw_rect_impl(Rect rect, float tl, float tr, float br, float bl, Color fill, Color border, float border_w, bool shadow) {
    rect = rect.translated(off_.x, off_.y);
    const float max_radius = std::max(0.0f, std::min(rect.w, rect.h) * 0.5f);
    const float p1 = shadow ? 1.0f : 0.0f;
    primitives_.push_back({{rect.x, rect.y, rect.w, rect.h},
        {std::min(tl, max_radius), std::min(tr, max_radius), std::min(br, max_radius), std::min(bl, max_radius)},
        {fill.r, fill.g, fill.b, fill.a * global_alpha_},
        {border.r, border.g, border.b, border.a * global_alpha_},
        {border_w, p1, 0, 0}, {clip_.x, clip_.y, clip_.w, clip_.h}});
}
void Renderer::draw_rect(Rect r, Color c) { draw_rounded_rect_ex(r, 0, 0, 0, 0, c); }
void Renderer::draw_rounded_rect(Rect r, float radius, Color c) { draw_rounded_rect_ex(r, radius, radius, radius, radius, c); }
void Renderer::draw_rounded_outline(Rect r, float radius, Color c, float t) { draw_rounded_outline_ex(r, radius, radius, radius, radius, c, t); }
void Renderer::draw_rounded_rect_ex(Rect r, float tl, float tr, float br, float bl, Color c) { draw_rect_impl(r, tl, tr, br, bl, c, {0,0,0,0}, 0); }
void Renderer::draw_rounded_outline_ex(Rect r, float tl, float tr, float br, float bl, Color c, float t) { draw_rect_impl(r, tl, tr, br, bl, {0,0,0,0}, c, t); }
void Renderer::draw_shadow(Rect r, float radius, Color c, float spread) { draw_shadow_ex(r, radius, radius, radius, radius, c, spread); }
void Renderer::draw_shadow_ex(Rect r, float tl, float tr, float br, float bl, Color c, float spread) { Rect shadow = r.translated(-spread, -spread); shadow.w += spread * 2; shadow.h += spread * 2; draw_rect_impl(shadow, tl + spread, tr + spread, br + spread, bl + spread, c, {0,0,0,0}, spread, /*shadow=*/true); }
void Renderer::set_scissor(Rect r) { clip_ = r.translated(off_.x, off_.y); }
void Renderer::clear_scissor() { clip_ = {0,0,screen_w_,screen_h_}; }
void Renderer::set_alpha(float a) { global_alpha_ = clamp01(a); }
float Renderer::alpha() const { return global_alpha_; }
float Renderer::screen_w() const { return screen_w_; }
float Renderer::screen_h() const { return screen_h_; }
Point Renderer::off() const { return off_; }
FontRenderer& Renderer::font() { return *font_; }
void Renderer::push_offset(Point p) { offset_stack_.push_back(off_); off_.x += p.x; off_.y += p.y; }
void Renderer::pop_offset() { if (!offset_stack_.empty()) { off_ = offset_stack_.back(); offset_stack_.pop_back(); } }
void Renderer::draw_bitmap(int x, int y, int width, int height, const unsigned char* alpha, Color color) {
    if (!alpha || width <= 0 || height <= 0 || bitmap_data_.size() + (size_t)width * height > 8 * 1024 * 1024 || primitives_.size() >= 32768) return;
    // x,y,width,height 为物理像素（oversample 采样分辨率）；显示位置与尺寸统一除以
    // bitmap_scale_ 转回点空间。保留 .5 步进的浮点坐标（不取整），使位图 1 像素精确
    // 对应 1 物理像素（Retina 2x 下 GPU 放大后逐像素对齐，避免字符上下左右错位）。
    // x,y,width,height are physical pixels; position/size divide by bitmap_scale_ to
    // points, keeping .5-step floats so each bitmap pixel lands on a physical pixel.
    const float dw = (float)width / bitmap_scale_;
    const float dh = (float)height / bitmap_scale_;
    const uint32_t offset = (uint32_t)bitmap_data_.size();
    const size_t n = (size_t)width * height;
    bitmap_data_.reserve(bitmap_data_.size() + n);
    bitmap_data_.insert(bitmap_data_.end(), alpha, alpha + n);  // 批量拷贝，替代逐像素 push_back
    const Rect rect = Rect{(float)x / bitmap_scale_ + off_.x, (float)y / bitmap_scale_ + off_.y, dw, dh};
    primitives_.push_back({{rect.x, rect.y, rect.w, rect.h}, {0, 0, 0, 0},
        {color.r, color.g, color.b, color.a * global_alpha_}, {0, 0, 0, 0},
        {0, (float)offset, (float)width, (float)height}, {clip_.x, clip_.y, clip_.w, clip_.h}});
}

void Renderer::draw_bitmap_rgba(float x, float y, const uint32_t* rgba, int pw, int ph) {
    if (!rgba || pw <= 0 || ph <= 0 || bitmap_data_.size() + (size_t)pw * ph > 8 * 1024 * 1024 || primitives_.size() >= 32768) return;
    const float dw = (float)pw / bitmap_scale_;
    const float dh = (float)ph / bitmap_scale_;
    const uint32_t offset = (uint32_t)bitmap_data_.size();
    const size_t n = (size_t)pw * ph;
    bitmap_data_.reserve(bitmap_data_.size() + n);
    bitmap_data_.insert(bitmap_data_.end(), rgba, rgba + n);  // 批量拷贝，替代逐像素 push_back
    const Rect rect{x + off_.x, y + off_.y, dw, dh};
    primitives_.push_back({{rect.x, rect.y, rect.w, rect.h}, {0, 0, 0, 0},
        {1, 1, 1, global_alpha_}, {0, 0, 0, 0},
        {2.0f, (float)offset, (float)pw, (float)ph}, {clip_.x, clip_.y, clip_.w, clip_.h}});
}

void Renderer::set_bitmap_scale(float scale) { bitmap_scale_ = scale > 0.0f ? scale : 1.0f; }


