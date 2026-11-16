// core/platform/window_effect_win.cpp
// Windows 实现：DwmSetWindowAttribute 的 DWMWA_USE_IMMERSIVE_DARK_MODE
//（深浅标题栏，Win11 22000+）+ 预留 DWMWA_CAPTION_COLOR（自定义标题栏底色，
// Win11 22000+）+ DWMWA_SYSTEMBACKDROP_TYPE（Mica/Acrylic backdrop，
// Win11 22621+，磨砂设计 Phase C）。老系统调用失败 → 返回 false 静默降级。
// 设计来源：磨砂设计文档 Phase A/C（2026-09-22-eui-window-frost-design.md §3.0/§3.1）。
#include "core/platform/window_effect.h"

#include <atomic>
#include <cstdio> // P3-6 失败路径日志（stderr）
#include <cstdlib> // std::getenv

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

// debt268 P2-3 衍生（P3-6）：AUTO 复位/blur-behind 恢复失败路径的排障日志。
// env 门控与 glfw_app_main 的 EUI_FX_DIAG 同名同变量（EUI_FX_DEBUG），本文件
// Windows 侧真打 stderr（那边 Apple-only，Windows 预处理为空语句）。
namespace window_effect_win_diag {
inline bool fxDebugEnabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("EUI_FX_DEBUG");
        return v != nullptr && *v != '\0';
    }();
    return enabled;
}
} // namespace window_effect_win_diag
#define EUI_FX_DIAG_WIN(...)                      \
    do {                                          \
        if (window_effect_win_diag::fxDebugEnabled()) { \
            std::fprintf(stderr, __VA_ARGS__);    \
            std::fflush(stderr);                  \
        }                                         \
    } while (0)


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

// ── 24H2 accent 路线（bug266）────────────────────────────────────────────
// Win11 24H2（build ≥26100）DWM 全面拒收 DwmEnableBlurBehindWindow（实测
// rc=S_OK 视觉无效，两态取证见线索 161），GLFW 逐像素透明 enable 路径失效；
// SYSTEMBACKDROP 材质又被 DWM 接管背景使 GL 帧缓冲 alpha 失效 → 实色。两条
// 旧路全断 → 改走未文档化 SetWindowCompositionAttribute(ACCENT_POLICY)，
// TranslucentTB 等生产级工具在 24H2 仍依赖的同一路线（2026-11-16 联网检索
// 锚点）。blur/tint 由 DWM 画在窗口内容后面，内容侧照旧用 clearColor alpha
// 出透见。pre-24H2 不走本路线（GLFW 原生路径照旧，零回归面）。
namespace {

// 未文档化结构/API 自声明（无 SDK 头；字段布局按社区逆向与生产级用法共识）。
struct AccentPolicy {
    unsigned long accentState;
    unsigned long accentFlags;
    unsigned long gradientColor; // AABBGGRR（alpha 在最高字节）
    unsigned long animationId;
};
struct WindowCompositionAttributeData {
    int attribute;               // WCA_ACCENT_POLICY = 19
    void* data;
    unsigned long long dataSize; // SIZE_T
};

// 24H2 判定（进程级缓存）。RtlGetVersion 不受应用 manifest 兼容垫片影响
//（VerifyVersionInfo 会把高版本谎报成 6.2）。
bool accentRouteRequired() {
    static const bool required = [] {
        struct OsVersionInfoW {
            unsigned long size;
            unsigned long major;
            unsigned long minor;
            unsigned long build;
            unsigned long platformId;
            wchar_t csdVersion[128];
        };
        using RtlGetVersionFn = long(__stdcall*)(OsVersionInfoW*);
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll == nullptr) {
            return false;
        }
        const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
            reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
        if (rtlGetVersion == nullptr) {
            return false;
        }
        OsVersionInfoW info{};
        info.size = sizeof(info);
        if (rtlGetVersion(&info) != 0) {
            return false;
        }
        return info.build >= 26100; // Win11 24H2：blur-behind 全态失效起点
    }();
    return required;
}

// 应用 accent policy（幂等）。返回 FALSE = API 拒收，调用方走既有降级链。
BOOL applyAccentPolicy(HWND hwnd, const AccentPolicy& policy) {
    using SetWcaFn = BOOL(WINAPI*)(HWND, const WindowCompositionAttributeData*);
    static const SetWcaFn setWca = []() -> SetWcaFn {
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32 == nullptr) {
            return nullptr;
        }
        return reinterpret_cast<SetWcaFn>(reinterpret_cast<void*>(
            GetProcAddress(user32, "SetWindowCompositionAttribute")));
    }();
    if (setWca == nullptr) {
        return FALSE;
    }
    WindowCompositionAttributeData data;
    data.attribute = 19; // WCA_ACCENT_POLICY
    data.data = const_cast<AccentPolicy*>(&policy);
    data.dataSize = sizeof(policy);
    const BOOL rc = setWca(hwnd, &data);
    const DWORD gle = GetLastError();
    EUI_FX_DIAG_WIN("[fxdiag] accent apply hwnd=%p state=%lu flags=%lu "
                    "color=%08lx rc=%ld gle=%lu\n",
                    static_cast<void*>(hwnd), policy.accentState,
                    policy.accentFlags, policy.gradientColor,
                    static_cast<long>(rc),
                    static_cast<unsigned long>(gle));
    return rc;
}

} // namespace

namespace core::platform {

// debt268 P2-2：窗口销毁路径清 per-hwnd 材质态（window_backend.cpp destroyWindow
// 在 glfwDestroyWindow 前调用，与 uninstallChromeHitTest 同缝）。HWND 数值销毁后
// 会被系统复用，残留标记让复用窗误判「已挂材质」→ 半透档被误写 SYSTEMBACKDROP
// 变实色。表里留 false 值项也无意义，一并 erase。
void clearWindowMaterialState(void* nativeWindowHandle) {
    if (nativeWindowHandle == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(backdropStateMutex());
    materialAppliedMap().erase(nativeWindowHandle);
}

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
    // bug266（24H2）：blur-behind 与 SYSTEMBACKDROP 两条旧路都被 DWM 断死
    //（材质档=接管背景后 GL alpha 失效=实色），改走 accent 路线。SYSTEMBACKDROP
    // 不再写（两机制并用互踩）；标记照清（24H2 上正常从未写过，纯防御）。
    if (accentRouteRequired()) {
        if (materialApplied(hwnd)) {
            const int none = systemBackdropValue(WindowEffect::None);
            DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &none,
                                  sizeof(none));
            setMaterialApplied(hwnd, false);
        }
        AccentPolicy policy{};
        policy.accentState =
            static_cast<unsigned long>(accentStateFor(effect));
        policy.accentFlags = accentFlagsFor(effect);
        policy.gradientColor = accentGradientColorFor(effect);
        return applyAccentPolicy(hwnd, policy) != 0;
    }
    // 显式档位（spike 结论：DWMSBT_AUTO 无效，必须显式 NONE/材质之一）；
    // int = DWM_SYSTEMBACKDROP_TYPE 的底层类型
    const int backdrop = systemBackdropValue(effect);
    // debt268 P2-4：判定抽纯函数（backdropTransitionFor，4 分支单测锚
    // tests/unit/window_effect_backdrop_state.cpp），本处只承载行为。
    const BackdropTransition transition =
        backdropTransitionFor(materialApplied(hwnd), backdrop);
    switch (transition) {
    case BackdropTransition::ApplyMaterial:
    case BackdropTransition::SwitchMaterial: {
        const HRESULT result = DwmSetWindowAttribute(
            hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
        if (FAILED(result)) {
            backdropAttributeSupport().store(-1, std::memory_order_relaxed);
            return false;
        }
        setMaterialApplied(hwnd, materialAppliedAfter(transition));
        break;
    }
    case BackdropTransition::ReleaseMaterial: {
        // bug266：材质→None 切换关材质。优先 DWMSBT_AUTO(0) 复位（把 SYSTEMBACKDROP
        // 拉回系统默认态，可能整体解除 DWM 对合成的接管），恢复失败再落 DWMSBT_NONE
        //（显式关材质；此路径 DWM 拒收后续 blur-behind，实测 rc=0）。
        const int autoReset = 0; // DWMSBT_AUTO（老 SDK 无此枚举，写底层值）
        DwmSetWindowAttribute(
            hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &autoReset, sizeof(autoReset));
        const BOOL rcAuto = restorePerPixelAlpha(hwnd);
        if (rcAuto == 0) {
            // debt268 P3-6：失败路径留排障日志（此前静默，盲区）
            EUI_FX_DIAG_WIN(
                "[fxdiag] backdrop release: AUTO reset + blur-behind rejected "
                "hwnd=%p, falling back to DWMSBT_NONE\n",
                static_cast<void*>(hwnd));
            // AUTO 复位后仍拒收 → 落 NONE 显式关材质再试一次
            DwmSetWindowAttribute(
                hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
            if (restorePerPixelAlpha(hwnd) == 0) {
                EUI_FX_DIAG_WIN(
                    "[fxdiag] backdrop release: NONE fallback blur-behind "
                    "still rejected hwnd=%p (24H2 known limitation)\n",
                    static_cast<void*>(hwnd));
            }
        }
        setMaterialApplied(hwnd, materialAppliedAfter(transition));
        break;
    }
    case BackdropTransition::KeepUntouched:
        // bug266：从未写过 SYSTEMBACKDROP 的窗口（冷启动即半透/off）**完全不动该
        // 属性**——GLFW 创建期打下的逐像素 alpha 原样有效，不写即保持半透能力
        //（实测：任何 SYSTEMBACKDROP 写入后 blur-behind 重打被 DWM 拒收 rc=0，
        // 见 win11 24H2 备注）。pre-24H2 机型此分支保持 GLFW 原生透明路径。
        break;
    }
    backdropAttributeSupport().store(1, std::memory_order_relaxed);
    return true;
}

} // namespace core::platform
