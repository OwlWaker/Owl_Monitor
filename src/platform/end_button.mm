// macOS 原生结束进程按钮（AppKit NSButton）桥接。
// 与搜索框一样，C++ 层不再自绘结束进程按钮，改由原生 AppKit 按钮接收点击，
// 并通过回调把点击通知给 C++，沿用现有的结束进程确认流程。
// The macOS native end-process button (AppKit NSButton) bridge. Like the search field,
// the C++ layer no longer draws the button; the AppKit button reports clicks back to C++.

#define Point OWL_Point
#define Rect OWL_Rect
#include "platform/platform.hpp"
#undef Point
#undef Rect

#if defined(__APPLE__)
#import <AppKit/AppKit.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

static NSButton* g_end_button = nil;
static void (*g_on_click)(void* user) = nullptr;
static void* g_user = nullptr;

// target/action 的 target 需强引用保留，避免点击回调失效
// The target of the target/action mechanism must be retained.
static id g_end_target = nil;

// 系统界面语言是否为中文：决定按钮文字
// Whether the system UI language is Chinese: pick the button title.
static bool system_language_zh() {
    NSArray<NSString*>* langs = [NSLocale preferredLanguages];
    if (langs.count == 0) return false;
    NSString* first = langs.firstObject;
    return first.length >= 2 && [[first substringToIndex:2] isEqualToString:@"zh"];
}

@interface EndButtonTarget : NSObject
- (void)clicked:(id)sender;
@end
@implementation EndButtonTarget
- (void)clicked:(id)sender {
    (void)sender;
    if (g_on_click) g_on_click(g_user);
}
@end

void platform_install_end_button(GLFWwindow* window,
                                 void (*on_click)(void* user), void* user) {
    g_on_click = on_click;
    g_user = user;
    if (!window) return;
    NSWindow* nativeWindow = glfwGetCocoaWindow(window);
    if (!nativeWindow) return;
    NSView* content = nativeWindow.contentView;
    if (!content) return;

    if (g_end_button) { [g_end_button removeFromSuperview]; g_end_button = nil; }

    NSButton* btn = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 110, 32)];
    btn.title = system_language_zh() ? @"结束进程" : @"End process";
    btn.font = [NSFont systemFontOfSize:13];
    btn.controlSize = NSControlSizeLarge;
    // 液态玻璃按钮（macOS 26+）：自带玻璃质感、大圆角与系统悬停高亮；低版本回退到 Push。
    // NSBezelStyleGlass 是 macOS 26 才引入的枚举，旧 SDK（如 CI 的 Xcode 15.4）不存在，
    // 用 SDK 版本条件编译以免旧 SDK 编译失败。
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
    if (@available(macOS 26.0, *)) {
        btn.bezelStyle = NSBezelStyleGlass;
    } else {
        btn.bezelStyle = NSBezelStylePush;
    }
#else
    btn.bezelStyle = NSBezelStylePush;
#endif
    btn.hidden = YES;
    btn.autoresizingMask = NSViewNotSizable;

    EndButtonTarget* target = [[EndButtonTarget alloc] init];
    btn.target = target;
    btn.action = @selector(clicked:);
    g_end_target = target;

    [content addSubview:btn];
    g_end_button = btn;
}

void platform_set_end_button_rect(float x, float y, float w, float h) {
    if (!g_end_button) return;
    NSView* super = g_end_button.superview;
    if (!super) return;
    // GLFW 坐标（左上原点、y 向下）→ AppKit 坐标（左下原点、y 向上）
    const CGFloat ch = super.bounds.size.height;
    g_end_button.frame = NSMakeRect((CGFloat)x, ch - (CGFloat)(y + h), (CGFloat)w, (CGFloat)h);
}

void platform_set_end_button_enabled(bool enabled) {
    if (!g_end_button) return;
    g_end_button.enabled = enabled;
}

void platform_set_end_button_visible(bool visible) {
    if (!g_end_button) return;
    g_end_button.hidden = !visible;
}
#else
void platform_install_end_button(GLFWwindow*, void (*)(void*), void*) {}
void platform_set_end_button_rect(float, float, float, float) {}
void platform_set_end_button_enabled(bool) {}
void platform_set_end_button_visible(bool) {}
#endif
