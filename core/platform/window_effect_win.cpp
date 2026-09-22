// core/platform/window_effect_win.cpp
// Windows 实现：DwmSetWindowAttribute 的 DWMWA_USE_IMMERSIVE_DARK_MODE
//（深浅标题栏，Win11 22000+）+ 预留 DWMWA_CAPTION_COLOR（自定义标题栏底色，
// Win11 22000+）+ DWMWA_SYSTEMBACKDROP_TYPE（Mica/Acrylic backdrop，
// Win11 22621+，磨砂设计 Phase C）。老系统调用失败 → 返回 false 静默降级。
// 设计来源：磨砂设计文档 Phase A/C（2026-09-22-eui-window-frost-design.md §3.0/§3.1）。
#include "core/platform/window_effect.h"

#include <atomic>

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
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

namespace core::platform {
namespace {

// 能力缓存（设计 §3.1.3 / §4-R5）：SYSTEMBACKDROP 属性首次失败（< Win11 22621
// 返回 E_INVALIDARG）后记住「不支持」，后续调用直接 false，不逐帧打 DWM。
// 0 = 未探测，1 = 支持，-1 = 不支持。
std::atomic<int>& backdropAttributeSupport() {
    static std::atomic<int> support{0};
    return support;
}

} // namespace

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

bool applyWindowEffect(void* nativeWindowHandle, WindowEffect effect) {
    if (nativeWindowHandle == nullptr) {
        return false;
    }

    const int support = backdropAttributeSupport().load(std::memory_order_relaxed);
    if (support < 0) {
        return false;
    }

    HWND hwnd = static_cast<HWND>(nativeWindowHandle);
    // 显式档位（spike 结论：DWMSBT_AUTO 无效，必须显式 NONE/材质之一）；
    // int = DWM_SYSTEMBACKDROP_TYPE 的底层类型
    const int backdrop = systemBackdropValue(effect);
    const HRESULT result = DwmSetWindowAttribute(
        hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    if (FAILED(result)) {
        backdropAttributeSupport().store(-1, std::memory_order_relaxed);
        return false;
    }
    backdropAttributeSupport().store(1, std::memory_order_relaxed);
    return true;
}

} // namespace core::platform
