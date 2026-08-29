#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

// 输入法（IME）定位锚点视图：作为窗口 first responder，仅向系统提供候选框所在的
// 屏幕矩形，并把 NSTextInputClient 的文本回调转发回 GLFW 的 contentView，
// 从而保留 GLFW 自身的输入法/字符处理，同时让候选框跟随搜索框光标。
// An IME anchor view: acts as the window first responder, provides the candidate box
// rect, and forwards NSTextInputClient callbacks to the GLFW content view so GLFW
// keeps handling IME/text while the candidate box follows the search cursor.
@interface ImeTextView : NSView <NSTextInputClient>
@property (nonatomic) NSRect imeRect;          // 候选框所在的屏幕矩形
@property (nonatomic, assign) NSView* hostView;  // GLFW 的 contentView，用于转发文本回调
@end

@implementation ImeTextView
@synthesize imeRect = _imeRect;
@synthesize hostView = _hostView;

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)becomeFirstResponder { return YES; }

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange {
    if (actualRange) *actualRange = range;
    return _imeRect;
}

// 把输入法确认的文本转发给 GLFW contentView，保持其字符回调正常。
// 其余 NSTextInputClient 方法返回默认值：自绘搜索框不显示组合文本（marked text）。
- (void)insertText:(id)text replacementRange:(NSRange)replacementRange {
    id<NSTextInputClient> host = (id<NSTextInputClient>)_hostView;
    if ([host respondsToSelector:@selector(insertText:replacementRange:)])
        [host insertText:text replacementRange:replacementRange];
}
- (void)setMarkedText:(id)text selectedRange:(NSRange)sel replacementRange:(NSRange)rep {
    (void)text; (void)sel; (void)rep;   // 忽略组合文本预览
}
- (void)unmarkText { }
- (NSRange)selectedRange { return NSMakeRange(NSNotFound, 0); }
- (NSRange)markedRange { return NSMakeRange(NSNotFound, 0); }
- (BOOL)hasMarkedText { return NO; }
- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actualRange {
    (void)range; if (actualRange) *actualRange = NSMakeRange(NSNotFound, 0);
    return [[[NSAttributedString alloc] init] autorelease];
}
- (NSArray*)validAttributesForMarkedText { return @[]; }
- (NSUInteger)characterIndexForPoint:(NSPoint)point { (void)point; return 0; }
- (void)doCommandBySelector:(SEL)selector { [self.nextResponder doCommandBySelector:selector]; }
@end

namespace { ImeTextView* g_ime_view = nil; }

namespace sys {

// 返回所有"拥有普通应用窗口"的进程 PID 集合（含最小化、非焦点）。
// 用 CGWindowListCopyWindowInfo 枚举全部窗口（kCGWindowListOptionAll 包含最小化的
// 离屏窗口），只取 layer==0 的正常应用窗口并取其属主 PID。这样非焦点、最小化的
// 窗口应用也会被识别为"有窗口的应用"，与 Windows 任务管理器"应用"分组语义一致。
// Collect the PIDs of every process that owns a normal (layer==0) window, including
// minimized / off-screen apps; used to detect "apps" like Windows Task Manager.
std::vector<int> app_window_pids() {
    std::vector<int> pids;
    CFArrayRef list = CGWindowListCopyWindowInfo(
        kCGWindowListOptionAll | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    if (!list) return pids;
    const CFIndex n = CFArrayGetCount(list);
    for (CFIndex i = 0; i < n; ++i) {
        CFDictionaryRef d = (CFDictionaryRef)CFArrayGetValueAtIndex(list, i);
        if (CFGetTypeID(d) != CFDictionaryGetTypeID()) continue;
        // 只取普通应用窗口层（layer==0），排除菜单栏/Dock 等系统层
        // Keep only normal app windows (layer==0); skip menu bar / Dock layers
        CFNumberRef layer = (CFNumberRef)CFDictionaryGetValue(d, kCGWindowLayer);
        if (!layer || CFGetTypeID(layer) != CFNumberGetTypeID()) continue;
        int layerVal = 0;
        CFNumberGetValue(layer, kCFNumberIntType, &layerVal);
        if (layerVal != 0) continue;
        CFNumberRef pid = (CFNumberRef)CFDictionaryGetValue(d, kCGWindowOwnerPID);
        if (!pid || CFGetTypeID(pid) != CFNumberGetTypeID()) continue;
        int pidVal = 0;
        CFNumberGetValue(pid, kCFNumberIntType, &pidVal);
        if (pidVal > 0) pids.push_back(pidVal);
    }
    CFRelease(list);
    // 去重（一个应用可能拥有多个窗口）
    // Deduplicate (an app may own several windows)
    std::sort(pids.begin(), pids.end());
    pids.erase(std::unique(pids.begin(), pids.end()), pids.end());
    return pids;
}

// 获取当前前台（激活）应用的 PID；失败返回 -1。
// 使用 Apple 官方 AppKit API（NSWorkspace.frontmostApplication），比解析 lsappinfo
// 命令行输出更可靠，且无子进程开销。独立 .mm 翻译单元，避免 AppKit/Foundation 的
// 类型与本项目 types/rect.hpp 的 Point/Rect 冲突（与 theme.cpp 同样的隔离思路）。
// Get the PID of the frontmost app via the AppKit API. Kept in its own .mm unit to
// avoid AppKit/Foundation types clashing with the project's Point/Rect.
int foreground_pid() {
    NSRunningApplication* app = [NSWorkspace sharedWorkspace].frontmostApplication;
    return app ? (int)app.processIdentifier : -1;
}

// 系统界面语言是否为中文：读取 macOS 首选语言（AppleLanguages），以 zh 开头即中文
// Whether the system UI language is Chinese: read the macOS preferred language
bool system_language_zh() {
    NSArray<NSString*>* langs = [NSLocale preferredLanguages];
    if (langs.count == 0) return false;
    NSString* first = langs.firstObject;
    return first.length >= 2 && [[first substringToIndex:2] isEqualToString:@"zh"];
}

namespace {
// 图标缓存（按 pid）：读取一次后复用，避免每帧重复栅格化
// Icon cache keyed by pid; rasterized once then reused
struct IconCache { std::vector<uint32_t> px; int w = 0, h = 0; };
std::unordered_map<int, IconCache> g_icon_cache;
} // namespace

bool app_icon_rgba(int pid, const uint32_t*& px, int& w, int& h) {
    auto it = g_icon_cache.find(pid);
    if (it != g_icon_cache.end()) {
        if (it->second.px.empty()) return false;
        px = it->second.px.data(); w = it->second.w; h = it->second.h;
        return true;
    }
    constexpr int kS = 32;  // 栅格化尺寸（位图像素），点空间按 bitmap_scale_ 缩小显示
    NSRunningApplication* app = [NSRunningApplication runningApplicationWithProcessIdentifier:(pid_t)pid];
    NSImage* icon = app ? [app icon] : nil;
    if (!icon) { g_icon_cache[pid]; return false; }
    // 用 CGImage 精确渲染到明确的 RGBA8 缓冲，避免 NSBitmapImageRep 通道顺序差异导致全透明
    // Rasterize via CGImage into a known RGBA8 buffer to avoid channel-order surprises
    NSRect drawRect = NSMakeRect(0, 0, kS, kS);
    CGImageRef cg = [icon CGImageForProposedRect:&drawRect context:nil hints:nil];
    if (!cg) { g_icon_cache[pid]; return false; }
    std::vector<unsigned char> buf((size_t)kS * kS * 4, 0);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(buf.data(), kS, kS, 8, kS * 4, cs, kCGImageAlphaPremultipliedLast);
    if (!ctx) { CGColorSpaceRelease(cs); g_icon_cache[pid]; return false; }
    CGContextDrawImage(ctx, CGRectMake(0, 0, kS, kS), cg);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    IconCache ic;
    ic.w = ic.h = kS;
    ic.px.resize((size_t)kS * kS);
    // 先统计是否有非零 alpha；若整图透明（读取异常），用亮度重建 alpha 使图标可见
    // If the alpha channel reads as all zero, rebuild it from luminance so the icon shows
    bool has_alpha = false;
    for (int i = 0; i < kS * kS; ++i) if (buf[i * 4 + 3] != 0) { has_alpha = true; break; }
    for (int i = 0; i < kS * kS; ++i) {
        unsigned char r = buf[i * 4 + 0], g = buf[i * 4 + 1], bl = buf[i * 4 + 2], a = buf[i * 4 + 3];
        if (!has_alpha) {
            a = (unsigned char)std::max({r, g, bl});
            if (a > 0) { r = 255; g = 255; bl = 255; }  // 单色剪影兜底
        }
        ic.px[i] = ((uint32_t)a << 24) | ((uint32_t)bl << 16) | ((uint32_t)g << 8) | (uint32_t)r;
    }
    g_icon_cache[pid] = std::move(ic);
    auto& c = g_icon_cache[pid];
    px = c.px.data(); w = c.w; h = c.h;
    return true;
}

// 旧版 GLFW 头可能未声明 glfwGetCocoaWindow，但该符号由 glfw 库导出；手动声明以链接。
// The bundled GLFW header may not declare glfwGetCocoaWindow; declare it manually (exported by glfw).
extern "C" NSWindow* glfwGetCocoaWindow(GLFWwindow* window);

// 把 GLFW 窗口底层的 NSWindow 设为透明标题栏：隐藏标题文字、标题栏透明、
// 内容区延伸到标题栏下，但**保留红绿灯三大键**，仍可拖动/关闭窗口。
// Style the underlying NSWindow with a transparent title bar: hide the title text,
// make the title bar transparent, extend content under it, but keep the traffic light buttons.
void style_cocoa_window(GLFWwindow* window) {
    if (!window) return;
    NSWindow* win = glfwGetCocoaWindow(window);
    if (!win) return;
    win.titleVisibility = NSWindowTitleHidden;
    win.titlebarAppearsTransparent = YES;
    win.styleMask |= NSWindowStyleMaskFullSizeContentView;

    // 创建 IME 定位锚点视图并挂到内容视图，设为 first responder，让候选框跟随搜索框光标
    // Attach an IME anchor view to the content view and make it first responder,
    // so the candidate box follows the search cursor.
    NSView* content = win.contentView;
    if (content) {
        ImeTextView* ime = [[ImeTextView alloc] initWithFrame:content.bounds];
        ime.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        ime.hostView = content;
        [content addSubview:ime];
        [win makeFirstResponder:ime];
        g_ime_view = ime;
    }
}

} // namespace sys

// 在当前窗口上执行一次系统拖动窗口的手势（无标题栏时调用）。
// Start a system window-drag gesture on the current window (for the undecorated title bar).
void platform_drag_window(float x, float y) {
    NSWindow* win = [NSApp keyWindow];
    if (!win) return;
    // location 必须是窗口内坐标（Y 轴向上），而 GLFW 的 y 轴向下，因此翻转 Y。
    // performanceWindowDragWithEvent 需要窗口内坐标，否则会把窗口瞬移/粘到屏幕边缘。
    NSEvent* ev = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDragged
        location:NSMakePoint(x, win.frame.size.height - y)
        modifierFlags:0 timestamp:NSProcessInfo.processInfo.systemUptime
        windowNumber:win.windowNumber context:nil eventNumber:0 clickCount:1 pressure:1.0];
    if (ev) [win performWindowDragWithEvent:ev];
}

// 更新输入法候选框锚点（x/y/w/h 为窗口内点坐标，GLFW y 向下），并确保 IME 视图成为 first responder。
// Update the IME candidate-box anchor (in-window point, GLFW y down) and make the
// IME view the first responder.
void platform_set_ime_rect(float x, float y, float w, float h) {
    NSWindow* win = [NSApp keyWindow];
    if (!win || !g_ime_view) return;
    NSView* content = win.contentView;
    if (!content) return;
    // GLFW 窗口内坐标（左上原点，Y 向下）→ 窗口内坐标（左下原点，Y 向上）→ 屏幕坐标
    const float yUp = (float)content.frame.size.height - y;
    NSRect r = NSMakeRect((CGFloat)x, (CGFloat)yUp, (CGFloat)w, (CGFloat)h);
    g_ime_view.imeRect = [win convertRectToScreen:r];
    if (win.firstResponder != g_ime_view) [win makeFirstResponder:g_ime_view];
}
