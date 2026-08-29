// 仅引入 GLFW 窗口类型与 Cocoa。注意：不要 include platform/platform.hpp——
// 其依赖的 types/rect.hpp 定义的自有 Point/Rect 与本项目 macOS SDK 的 MacTypes.h
// 中的 Point/Rect 冲突（与 theme.cpp 的隔离策略一致）。所需函数以前置声明使用。
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#import <Cocoa/Cocoa.h>

GLFWwindow* platform_current_window();

// 弹出原生确认框（macOS NSAlert），以窗口 sheet 形式挂载，非阻塞主循环。
// 用户点击后，通过 on_done 在主线程回调：choice=1 表示确认/OK，0 表示取消。
// 若未取到宿主窗口（或关联的 NSWindow），则立即以 0（取消）回调。
// Show a native NSAlert as a non-blocking window sheet. The result is delivered
// to on_done on the main thread: 1 = OK/confirm, 0 = cancel. If no host window
// (or its NSWindow) is available, on_done is called immediately with 0.
void platform_confirm_sheet(const char* title, const char* message,
                            const char* ok_label, const char* cancel_label,
                            void (*on_done)(int choice, void* user), void* user) {
    GLFWwindow* win = platform_current_window();
    NSWindow* ns_window = win ? (__bridge NSWindow*)glfwGetCocoaWindow(win) : nil;
    if (!ns_window) {
        if (on_done) on_done(0, user);
        return;
    }

    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = [NSString stringWithUTF8String:title ? title : ""];
    alert.informativeText = [NSString stringWithUTF8String:message ? message : ""];
    [alert addButtonWithTitle:[NSString stringWithUTF8String:ok_label ? ok_label : "OK"]];
    [alert addButtonWithTitle:[NSString stringWithUTF8String:cancel_label ? cancel_label : "Cancel"]];
    [alert beginSheetModalForWindow:ns_window completionHandler:^(NSModalResponse response) {
        const int choice = (response == NSAlertFirstButtonReturn) ? 1 : 0;
        if (on_done) on_done(choice, user);
    }];
}
