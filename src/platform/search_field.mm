// macOS 原生搜索输入框（AppKit NSTextField）桥接。
// C++ 层不再自绘搜索框，改由原生 AppKit 输入框接收文本，并通过回调把内容回传给
// C++ 用于进程过滤。位置由 C++ 每帧通过 platform_set_search_field_rect 对齐。
// The macOS native search field (AppKit NSTextField) bridge: the C++ layer no longer
// draws the search box; the AppKit field receives text and reports it back to C++ for
// process filtering. Its position is kept in sync by platform_set_search_field_rect.

#define Point OWL_Point
#define Rect OWL_Rect
#include "platform/platform.hpp"
#undef Point
#undef Rect

#if defined(__APPLE__)
#import <AppKit/AppKit.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

static NSSearchField* g_search_field = nil;
static void (*g_on_text)(const char*, void*) = nullptr;
static void* g_user = nullptr;

// delegate 是弱引用，需强引用保留，避免回调失效
// NSTextField.delegate is weak; keep a strong reference so callbacks survive.
static id g_search_delegate = nil;

// 系统界面语言是否为中文：决定输入框占位符文案
// Whether the system UI language is Chinese: pick the placeholder text.
static bool system_language_zh() {
    NSArray<NSString*>* langs = [NSLocale preferredLanguages];
    if (langs.count == 0) return false;
    NSString* first = langs.firstObject;
    return first.length >= 2 && [[first substringToIndex:2] isEqualToString:@"zh"];
}

@interface SearchFieldDelegate : NSObject <NSSearchFieldDelegate>
@end
@implementation SearchFieldDelegate
- (void)controlTextDidChange:(NSNotification*)notification {
    NSTextField* tf = (NSTextField*)[notification object];
    if (g_on_text) {
        const char* s = tf.stringValue.UTF8String;
        g_on_text(s ? s : "", g_user);
    }
}
@end

void platform_install_search_field(GLFWwindow* window,
                                   void (*on_text)(const char*, void*), void* user) {
    g_on_text = on_text;
    g_user = user;
    if (!window) return;
    NSWindow* nativeWindow = glfwGetCocoaWindow(window);
    if (!nativeWindow) return;
    NSView* content = nativeWindow.contentView;
    if (!content) return;

    if (g_search_field) { [g_search_field removeFromSuperview]; g_search_field = nil; }

    NSSearchField* tf = [[NSSearchField alloc] initWithFrame:NSMakeRect(0, 0, 220, 32)];
    tf.placeholderString = system_language_zh() ? @"搜索名称、发布者或 PID" : @"Search name, publisher or PID";
    tf.font = [NSFont systemFontOfSize:12];
    tf.controlSize = NSControlSizeLarge;
    tf.hidden = YES;
    tf.autoresizingMask = NSViewNotSizable;

    SearchFieldDelegate* delegate = [[SearchFieldDelegate alloc] init];
    tf.delegate = delegate;
    g_search_delegate = delegate;

    [content addSubview:tf];
    g_search_field = tf;
}

void platform_set_search_field_rect(float x, float y, float w, float h) {
    if (!g_search_field) return;
    NSView* super = g_search_field.superview;
    if (!super) return;
    // GLFW 坐标（左上原点、y 向下）→ AppKit 坐标（左下原点、y 向上）
    const CGFloat ch = super.bounds.size.height;
    g_search_field.frame = NSMakeRect((CGFloat)x, ch - (CGFloat)(y + h), (CGFloat)w, (CGFloat)h);
}

void platform_set_search_field_text(const char* text) {
    if (!g_search_field) return;
    g_search_field.stringValue = text ? [NSString stringWithUTF8String:text] : @"";
}

void platform_set_search_field_visible(bool visible) {
    if (!g_search_field) return;
    g_search_field.hidden = !visible;
    if (!visible) {
        // 隐藏时让输入框放弃焦点，避免键盘输入仍被其捕获
        // Resign focus when hiding so keyboard input isn't still captured by it.
        [[g_search_field window] makeFirstResponder:nil];
    }
}
#else
void platform_install_search_field(GLFWwindow*, void (*)(const char*, void*), void*) {}
void platform_set_search_field_rect(float, float, float, float) {}
void platform_set_search_field_text(const char*) {}
void platform_set_search_field_visible(bool) {}
#endif
