#include "platform/platform.hpp"
#include "render/renderer.hpp"
#include "sys/system.hpp"
#include "taskmgr/taskmgr.hpp"
#include <algorithm>
#include <string>

namespace {
// 窗口标题（跟随系统语言）
// Window title (follows the system language)
const char* window_title() { return sys::tr("猫头鹰监看器", "Owl Monitor"); }

// 窗口尺寸变化标志：framebuffer 回调置位，主循环消费后重建 swapchain
// Window resize flag: set by the framebuffer callback, consumed by the main loop
bool g_needs_resize = false;
void framebuffer_size_callback(GLFWwindow*, int, int) { g_needs_resize = true; }

// 滚轮增量累积：回调写入、主循环每帧取出并清零
// Scroll accumulation: written by the callback, drained each frame by the main loop
double g_scroll_accum = 0;
void scroll_callback(GLFWwindow*, double, double yoffset) { g_scroll_accum += yoffset; }

// 键盘输入累积：字符回调写入，主循环每帧取出并清零（用于搜索框输入）
// Keyboard input accumulation: written by char/key callbacks, drained each frame
std::string g_typed;
bool g_backspace = false;
void char_callback(GLFWwindow*, unsigned int c) {
    // 把 Unicode 码点编码为 UTF-8 追加，支持中文等多字节字符
    if (c < 0x80) {
        g_typed += (char)c;
    } else if (c < 0x800) {
        g_typed += (char)(0xC0 | (c >> 6));
        g_typed += (char)(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        g_typed += (char)(0xE0 | (c >> 12));
        g_typed += (char)(0x80 | ((c >> 6) & 0x3F));
        g_typed += (char)(0x80 | (c & 0x3F));
    } else {
        g_typed += (char)(0xF0 | (c >> 18));
        g_typed += (char)(0x80 | ((c >> 12) & 0x3F));
        g_typed += (char)(0x80 | ((c >> 6) & 0x3F));
        g_typed += (char)(0x80 | (c & 0x3F));
    }
}
void key_callback(GLFWwindow*, int key, int, int action, int) {
    if (key == GLFW_KEY_BACKSPACE && action == GLFW_PRESS) g_backspace = true;
}
}

// 应用主循环：创建窗口 → 初始化渲染/字体/任务管理器 →
// 进入事件循环（轮询输入、更新任务管理器、绘制并呈现）。
// Application main loop: create the window, init renderer/font/task manager,
// then run the event loop (poll input, update, draw and present).
int app_run() {
    // 创建窗口（RAII 守卫自动销毁）
    // Create the window (RAII guard destroys it on exit)
    WindowGuard window(platform_create_window(window_title()));
    if (!window.get()) return -1;

    // 启用 vsync（帧率限制为显示刷新率），避免无限制渲染占用 CPU
    // Enable vsync (limit to the display refresh rate) to avoid burning CPU
    glfwSwapInterval(1);

    // 初始化 Vulkan 渲染器与字体渲染器
    // Initialize the Vulkan renderer and the font renderer
    Renderer renderer;
    if (!renderer.init()) { platform_show_error("Init Failed", "Renderer initialization failed."); return -1; }
    if (!renderer.font().init()) { platform_show_error("Init Failed", "Font initialization failed."); return -1; }

    // 注册 framebuffer 尺寸回调（resize）与滚轮回调
    // Register the framebuffer size callback (resize) and the scroll callback
    glfwSetFramebufferSizeCallback(window.get(), framebuffer_size_callback);
    glfwSetScrollCallback(window.get(), scroll_callback);
    glfwSetCharCallback(window.get(), char_callback);
    glfwSetKeyCallback(window.get(), key_callback);

    // 创建任务管理器并按其窗口点尺寸布局
    // Create the task manager and lay it out at the window point size
    TaskManager tm;
    tm.init();
    int ww = 0, wh = 0;
    glfwGetWindowSize(window.get(), &ww, &wh);
    tm.resize((float)ww, (float)wh);

    // resize 处理：重建交换链并重新布局任务管理器
    // Handle resize: rebuild the swapchain and relayout the task manager
    auto handle_resize = [&]() {
        g_needs_resize = false;
        renderer.handle_resize();
        glfwGetWindowSize(window.get(), &ww, &wh);
        tm.resize((float)ww, (float)wh);
    };

    // 渲染首帧后显示窗口
    // Render the first frame then show the window
    renderer.begin_frame(ww, wh);
    tm.draw(renderer);
    renderer.present();
    glfwShowWindow(window.get());

    // 主事件循环：轮询事件与输入、更新任务管理器、绘制并呈现
    // Main event loop: poll events/input, update, draw and present
    double last = glfwGetTime();
    bool prev_down = false;
    while (!glfwWindowShouldClose(window.get())) {
        glfwPollEvents();
        if (g_needs_resize) handle_resize();

        // 读取鼠标状态：坐标、按住、本帧刚按下
        // Read mouse state: position, held, just pressed
        double mx = 0, my = 0;
        glfwGetCursorPos(window.get(), &mx, &my);
        const bool down = glfwGetMouseButton(window.get(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        const bool pressed = down && !prev_down;
        prev_down = down;

        // 计算帧间隔（钳制上限，避免卡顿后动画突变）
        // Compute the frame delta, clamped to avoid jumps after a stall
        const double now = glfwGetTime();
        const float dt = (float)std::min(now - last, 0.1);
        last = now;

        glfwGetWindowSize(window.get(), &ww, &wh);
        tm.input_text(g_typed.c_str(), g_backspace);
        g_typed.clear();
        g_backspace = false;
        tm.update((float)mx, (float)my, down, pressed, g_scroll_accum, dt);
        g_scroll_accum = 0;

        renderer.begin_frame(ww, wh);
        tm.draw(renderer);
        renderer.present();
    }
    return 0;
}

// 程序入口
// Program entry
int main() {
    return app_run();
}