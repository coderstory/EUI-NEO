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
#include <mutex>         // bug266：per-hwnd 材质态表
#include <unordered_map> // 同上

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
#ifndef DWMSBT_NONE
#define DWMSBT_NONE 1
#endif

// bug266：DWMWA_SYSTEMBACKDROP_TYPE 属性（含显式 DWMSBT_NONE 关材质）会让 DWM 接管
// 窗口合成，GLFW 创建期为透明帧缓冲打下的逐像素 alpha（3rd/glfw/src/win32_window.c
// updateFramebufferTransparency：空区域 blur-behind + fEnable）随之失效 → 半透档
//（None backdrop + clearColor α=0.72）变成实色窗。这里按 GLFW 同口径把 blur-behind
// 重新打上恢复逐像素透明；off 档 clearColor α=1，视觉仍不透明；材质档（Mica）本身
// 接管底衬，不需要也不受害。
// bug266：per-hwnd「材质挂过」标记——DWMSBT 材质档写过的窗口切回 None 档时必须显式
// 关材质；从未写过 SYSTEMBACKDROP 的窗口则完全不动该属性（写了含 NONE 在内都会让
// DWM 接管合成、拒收后续 blur-behind，实测 rc=0），GLFW 创建期的逐像素 alpha 原样保留。
std::mutex& backdropStateMutex() {
    static std::mutex mu;
    return mu;
}
std::unordered_map<void*, bool>& materialAppliedMap() {
    static std::unordered_map<void*, bool> map;
    return map;
}
bool materialApplied(HWND hwnd) {
    std::lock_guard<std::mutex> lock(backdropStateMutex());
    const auto it = materialAppliedMap().find(static_cast<void*>(hwnd));
    return it != materialAppliedMap().end() && it->second;
}
void setMaterialApplied(HWND hwnd, bool applied) {
    std::lock_guard<std::mutex> lock(backdropStateMutex());
    materialAppliedMap()[static_cast<void*>(hwnd)] = applied;
}

BOOL restorePerPixelAlpha(HWND hwnd) {
    BOOL composition = FALSE;
    if (FAILED(DwmIsCompositionEnabled(&composition)) || !composition) {
        return FALSE;
    }
    HRGN region = CreateRectRgn(0, 0, -1, -1); // 空区域（GLFW 同口径）
    if (region == nullptr) {
        return FALSE;
    }
    DWM_BLURBEHIND bb = {0};
    bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
    bb.hRgnBlur = region;
    bb.fEnable = TRUE;
    const BOOL rc = DwmEnableBlurBehindWindow(hwnd, &bb);
    DeleteObject(region);
    return rc;
}

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
    if (backdrop != DWMSBT_NONE || materialApplied(hwnd)) {
        const HRESULT result = DwmSetWindowAttribute(
            hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
        if (FAILED(result)) {
            backdropAttributeSupport().store(-1, std::memory_order_relaxed);
            return false;
        }
        setMaterialApplied(hwnd, backdrop != DWMSBT_NONE);
        // bug266：材质→None 切换关材质。优先 DWMSBT_AUTO(0) 复位（把 SYSTEMBACKDROP
        // 拉回系统默认态，可能整体解除 DWM 对合成的接管），恢复失败再落 DWMSBT_NONE
        //（显式关材质；此路径 DWM 拒收后续 blur-behind，实测 rc=0）。
        if (backdrop == DWMSBT_NONE) {
            const int autoReset = 0; // DWMSBT_AUTO（老 SDK 无此枚举，写底层值）
            DwmSetWindowAttribute(
                hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &autoReset, sizeof(autoReset));
            const BOOL rcAuto = restorePerPixelAlpha(hwnd);
            if (rcAuto == 0) {
                // AUTO 复位后仍拒收 → 落 NONE 显式关材质再试一次
                DwmSetWindowAttribute(
                    hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
                restorePerPixelAlpha(hwnd);
            }
        }
    } else {
        // bug266：从未写过 SYSTEMBACKDROP 的窗口（冷启动即半透/off）**完全不动该
        // 属性**——GLFW 创建期打下的逐像素 alpha 原样有效，不写即保持半透能力
        //（实测：任何 SYSTEMBACKDROP 写入后 blur-behind 重打被 DWM 拒收 rc=0，
        // 见 win11 24H2 备注）。pre-24H2 机型此分支保持 GLFW 原生透明路径。
    }
    backdropAttributeSupport().store(1, std::memory_order_relaxed);
    return true;
}

} // namespace core::platform
