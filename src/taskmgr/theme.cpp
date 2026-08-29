#include <CoreFoundation/CoreFoundation.h>

namespace theme {

// 读取系统外观：AppleInterfaceStyle == "Dark" 为深色，否则浅色。
// 放在独立翻译单元实现：CoreFoundation 头里的全局 Point/Rect 与本项目
// types/rect.hpp 冲突，故不在此 include 任何项目头，只返回 bool。
// Read the system appearance: "Dark" means dark mode, otherwise light.
// Kept in its own translation unit because CoreFoundation defines global
// Point/Rect that clash with the project's types/rect.hpp.
bool system_dark_mode() {
    CFTypeRef v = CFPreferencesCopyAppValue(CFSTR("AppleInterfaceStyle"),
                                            kCFPreferencesAnyApplication);
    bool dark = false;
    if (v) {
        if (CFGetTypeID(v) == CFStringGetTypeID())
            dark = CFStringCompare((CFStringRef)v, CFSTR("Dark"), 0) == kCFCompareEqualTo;
        CFRelease(v);
    }
    return dark;
}

} // namespace theme
