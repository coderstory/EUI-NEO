// core/platform/window_effect_win.cpp
// Windows 实现：DwmSetWindowAttribute 的 DWMWA_USE_IMMERSIVE_DARK_MODE
//（深浅标题栏，Win11 22000+）+ 预留 DWMWA_CAPTION_COLOR（自定义标题栏底色，
// Win11 22000+）。老系统调用失败 → 返回 false 静默降级。
// 设计来源：磨砂设计文档 Phase A（2026-09-22-eui-window-frost-design.md §3.0/§3.1）。
#include "core/platform/window_effect.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>

// MinGW-w64 的 dwmapi.h 版本可能早于 Win11 SDK，缺这几个属性枚举时补齐值
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_COLOR_DEFAULT
#define DWMWA_COLOR_DEFAULT 0xFFFFFFFF
#endif

namespace core::platform {

bool applyTitleBarAppearance(void* nativeWindowHandle, const TitleBarAppearance& appearance) {
    if (nativeWindowHandle == nullptr) {
        return false;
    }
    HWND hwnd = static_cast<HWND>(nativeWindowHandle);

    BOOL dark = appearance.dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark)))) {
        return false;
    }

    // 自定义底色是可选增强：失败（老系统无 CAPTION_COLOR）不影响 dark 主开关
    const COLORREF captionColor = appearance.customColor
        ? static_cast<COLORREF>(appearance.colorAbgr)
        : static_cast<COLORREF>(DWMWA_COLOR_DEFAULT);
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
    return true;
}

} // namespace core::platform
