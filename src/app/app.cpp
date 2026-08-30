#include "app/app.hpp"
#include "input/input.hpp"
#include "platform/platform.hpp"
#include "render/renderer.hpp"
#include "sys/system.hpp"
#include "taskmgr/taskmgr.hpp"
#include <algorithm>

namespace {
// 窗口标题（跟随系统语言）
// Window title (follows the system language)
const char* window_title() { return sys::tr("猫头鹰监看器", "Owl Monitor"); }

// 窗口尺寸变化标志：framebuffer 回调置位，主循环消费后重建 swapchain
// Window resize flag: set by the framebuffer callback, consumed by the main loop
bool g_needs_resize = false;
void framebuffer_size_callback(GLFWwindow*, int, int) { g_needs_resize = true; }
}

// 应用主循环：创建窗口 → 初始化渲染/字体/任务管理器 →
// 进入事件循环（轮询输入、更新任务管理器、绘制并呈现）。
// Application main loop: create the window, init renderer/font/task manager,
// then run the event loop (poll input, update, draw and present).
int app_run() {
    WindowGuard window(platform_create_window(window_title()));
    if (!window.get()) return -1;

    glfwSwapInterval(1);

    Renderer renderer;
    if (!renderer.init()) { platform_show_error("Init Failed", "Renderer initialization failed."); return -1; }
    if (!renderer.font().init()) { platform_show_error("Init Failed", "Font initialization failed."); return -1; }

    glfwSetFramebufferSizeCallback(window.get(), framebuffer_size_callback);

    input::InputState input;
    input.attach(window.get());

    TaskManager tm;
    tm.init();
    int ww = 0, wh = 0;
    glfwGetWindowSize(window.get(), &ww, &wh);
    tm.resize((float)ww, (float)wh);

    auto handle_resize = [&]() {
        g_needs_resize = false;
        renderer.handle_resize();
        glfwGetWindowSize(window.get(), &ww, &wh);
        tm.resize((float)ww, (float)wh);
    };

    renderer.begin_frame(ww, wh);
    tm.draw(renderer);
    renderer.present();
    glfwShowWindow(window.get());

    double last = glfwGetTime();
    bool prev_down = false;
    while (!glfwWindowShouldClose(window.get())) {
        glfwPollEvents();
        bool resized = false;
        if (g_needs_resize) { handle_resize(); resized = true; }

        input.update_mouse_state(window.get());

        const double now = glfwGetTime();
        const float dt = (float)std::min(now - last, 0.1);
        last = now;

        glfwGetWindowSize(window.get(), &ww, &wh);
        tm.input_text(input.typed().c_str(), input.backspace());

        const bool input_changed =
            resized ||
            input.mouse_pressed() ||
            input.scroll_delta() != 0.0 ||
            !input.typed().empty() || input.backspace();

        const bool tm_changed = tm.update(
            (float)input.mouse_x(),
            (float)input.mouse_y(),
            input.mouse_down(),
            input.mouse_pressed(),
            input.scroll_delta(),
            dt);
        input.consume_frame();

        if (input_changed || tm_changed) {
            renderer.begin_frame(ww, wh);
            tm.draw(renderer);
            renderer.present();
        } else {
            glfwWaitEventsTimeout(0.02);
        }
    }
    return 0;
}
