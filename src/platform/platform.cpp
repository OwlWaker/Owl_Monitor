#include "platform/platform.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

// macOS 原生窗口样式（透明标题栏/保留红绿灯），实现在 frontmost.mm
namespace sys { void style_cocoa_window(GLFWwindow* window); }

namespace {
// 全局窗口指针，用于 Surface 创建与窗口守卫清理
// Global window pointer for surface creation and guard cleanup
GLFWwindow* g_window = nullptr;
}

// 打印错误信息到 stderr
// Print an error message to stderr
void platform_show_error(const char* title, const char* msg) {
    std::fprintf(stderr, "[ERROR] %s: %s\n", title, msg);
}

// 用系统默认浏览器打开 URL（按平台选择命令：macOS open、Windows start、Linux xdg-open）
// Open a URL in the default browser, picking the per-platform command
void platform_open_url(const char* url) {
    if (!url || !*url) return;
#if defined(__APPLE__)
    std::string cmd = std::string("open \"") + url + "\"";
#else
    std::string cmd = std::string("xdg-open \"") + url + "\"";
#endif
    std::system(cmd.c_str());
}

// 获取主显示器工作区（位置 + 宽高），取不到时回退 1280x720
// Get the primary monitor work area; falls back to 1280x720
Rect platform_work_area() {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    if (!mode) return {0, 0, 1280, 720};

    int x = 0, y = 0;
    glfwGetMonitorPos(monitor, &x, &y);
    return {(float)x, (float)y, (float)mode->width, (float)mode->height};
}

// 当前时间（秒）
// Current time in seconds
double platform_time() { return glfwGetTime(); }

// 创建 GLFW 窗口：初始化 GLFW、按工作区计算尺寸、设置窗口属性并居中。
// 注意启用 Retina framebuffer（不关闭），使 swapchain 匹配物理像素，避免 macOS 放大模糊。
// Create the GLFW window: init GLFW, size from the work area, set hints and center it.
// Keep the Retina framebuffer so the swapchain matches physical pixels (avoids blur).
GLFWwindow* platform_create_window(const char* title) {
    glfwInitVulkanLoader(vkGetInstanceProcAddr);
    if (!glfwInit()) {
        platform_show_error("GLFW Init Failed", "Cannot initialize GLFW.");
        return nullptr;
    }

    // 窗口尺寸：工作区四周留边并设上限，避免超出屏幕
    // Window size: margins around the work area with an upper cap
    Rect area = platform_work_area();
    const int win_w = (int)std::min(area.w - 80.0f, 1280.0f);
    const int win_h = (int)std::min(area.h - 120.0f, 800.0f);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#if defined(__APPLE__)
    // 保留 Retina framebuffer：swapchain 按物理像素（2x）创建，避免 macOS 把低分辨率
    // framebuffer 非整数倍放大到 Retina 屏，导致整个画面（尤其字体）模糊。
    // Keep the Retina framebuffer: the swapchain matches physical pixels (2x), avoiding
    // macOS's non-integer upscale of a low-res framebuffer that blurs everything (esp. text).
#endif
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_FLOATING, GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(win_w, win_h, title, nullptr, nullptr);
    if (!window) {
        platform_show_error("Window Creation Failed", "Cannot create OpenGL 3.3 Core window.");
        glfwTerminate();
        return nullptr;
    }

    // 在屏幕上居中放置窗口
    // Center the window on screen
    glfwSetWindowPos(window,
        (int)(area.x + (area.w - win_w) * 0.5f),
        (int)(area.y + (area.h - win_h) * 0.5f));
#if defined(__APPLE__)
    // 透明标题栏 + 隐藏标题文字，保留红绿灯三大键，内容区延伸到标题栏
    sys::style_cocoa_window(window);
#endif
    g_window = window;
    return window;
}

// 返回最近一次创建的 GLFW 窗口（供平台原生 UI，如确认 sheet 访问宿主窗口）
// Return the most recently created GLFW window (for native platform UI)
GLFWwindow* platform_current_window() { return g_window; }

VkSurfaceKHR platform_create_vulkan_surface(VkInstance instance) {
    if (!g_window) return VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(instance, g_window, nullptr, &surface) != VK_SUCCESS) return VK_NULL_HANDLE;
    return surface;
}

WindowGuard::WindowGuard(GLFWwindow* window) : window_(window) {}
GLFWwindow* WindowGuard::get() const { return window_.get(); }

void WindowGuard::Deleter::operator()(GLFWwindow* window) const {
    if (!window) return;
    if (g_window == window) g_window = nullptr;
    glfwDestroyWindow(window);
    glfwTerminate();
}
