#pragma once

#define GLFW_INCLUDE_VULKAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include "types/rect.hpp"
#include <memory>

// 创建标准窗口（GLFW，NO_API）并准备好 Vulkan 使用的 Surface；失败返回 nullptr。
// Create a standard GLFW window (NO_API) and prepare the Vulkan surface; returns nullptr on failure.
GLFWwindow* platform_create_window(const char* title);
// 为给定 Vulkan 实例创建窗口 Surface
// Create a Vulkan surface for the given instance
VkSurfaceKHR platform_create_vulkan_surface(VkInstance instance);
// 用系统默认浏览器打开链接
// Open a URL in the system default browser
void platform_open_url(const char* url);
// 获取主显示器工作区矩形（用于窗口定位与尺寸限制）
// Get the primary monitor work area rectangle
Rect platform_work_area();
// 弹出或打印错误信息（当前实现打印到 stderr）
// Show or print an error message
void platform_show_error(const char* title, const char* msg);

// 获取当前（最近一次创建的）GLFW 窗口指针，供平台层（如原生弹窗）访问宿主窗口
// Get the current GLFW window pointer (most recently created), for platform UI access
GLFWwindow* platform_current_window();

// 以窗口 sheet（非阻塞）弹出原生确认框（macOS NSAlert）。
// title/message 为弹窗标题与说明；ok_label/cancel_label 为按钮文字；
// on_done 在主线程回调：choice=1 表示点击确认/OK，0 表示取消。
// Show a native confirmation sheet (macOS NSAlert); non-blocking window sheet.
// on_done is called on the main thread with choice=1 (OK) or 0 (cancel).
void platform_confirm_sheet(const char* title, const char* message,
                            const char* ok_label, const char* cancel_label,
                            void (*on_done)(int choice, void* user), void* user);

// 获取当前时间（秒），供动画与帧间计时
// Get the current time in seconds
 double platform_time();

// 在当前窗口上执行一次"拖动窗口"（macOS 用系统 API；无标题栏时手动拖动）。
// x/y 为鼠标在当前窗口内的点坐标，用于构造正确的手势事件，避免窗口瞬移。
// Start a window drag (macOS: system API); x/y is the in-window point for the gesture.
void platform_drag_window(float x, float y);

// 更新输入法候选框锚点（x/y/w/h 为窗口内点坐标，GLFW y 向下）；macOS 生效，其余平台为空操作。
// Update the IME candidate-box anchor (in-window point, GLFW y down); macOS only.
void platform_set_ime_rect(float x, float y, float w, float h);

// RAII 窗口守卫，析构时销毁窗口并终止 GLFW
// RAII window guard destroying the window and terminating GLFW on destruction
class WindowGuard {
public:
    // 接管给定窗口
    // Take ownership of the given window
    explicit WindowGuard(GLFWwindow* w);
    // 获取原始窗口指针
    // Get the raw window pointer
    GLFWwindow* get() const;
private:
    // 删除器，释放时销毁窗口
    // Deleter that destroys the window on release
    struct Deleter {
        void operator()(GLFWwindow* w) const;
    };
    std::unique_ptr<GLFWwindow, Deleter> window_;
};
