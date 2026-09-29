// core/platform/window_effect.h
// 窗口非客户区（标题栏）外观的平台无关接口。
// 设计来源：docs/superpowers/specs/2026-09-22-eui-window-frost-design.md Phase A
//（DevDesk 需要标题栏主题联动：深浅主题切换即时影响窗体顶部标题栏区域）。
#pragma once

#include <cstdint>

namespace core::platform {

// 窗口效果（磨砂设计文档 §3.1）。Phase B 落地 Transparent（透明帧缓冲 +
// clearColor alpha 半透）；Acrylic/Mica 的 DWM backdrop 应用在 Phase C
//（applyWindowEffect），枚举值先占位以稳定 WindowCreateRequest ABI。
enum class WindowEffect {
    None,        // 实色窗口（默认，行为与历史版本逐像素一致）
    Transparent, // 透明帧缓冲，clearColor alpha < 1 时整体半透
    Acrylic,     // DWM SYSTEMBACKDROP TRANSIENTWINDOW（Phase C）
    Mica,        // DWM SYSTEMBACKDROP MAINWINDOW（Phase C）
    MicaAlt      // DWM SYSTEMBACKDROP TABBEDWINDOW（Phase C）
};

// 标题栏外观。Phase A 只做深浅开关；customColor 预留自定义标题栏底色
//（Windows DWMWA_CAPTION_COLOR），默认不启用——留 API 不默认用，backdrop /
// 材质贯通标题栏时应让系统自绘（Phase C 起见）。
struct TitleBarAppearance {
    bool dark = false;
    bool customColor = false;     // true 时把 colorAbgr 设为标题栏底色
    std::uint32_t colorAbgr = 0;  // COLORREF（0x00BBGGRR）；0xFFFFFFFF 复位系统默认

    friend bool operator==(const TitleBarAppearance& a, const TitleBarAppearance& b) {
        return a.dark == b.dark && a.customColor == b.customColor && a.colorAbgr == b.colorAbgr;
    }
    friend bool operator!=(const TitleBarAppearance& a, const TitleBarAppearance& b) {
        return !(a == b);
    }
};

// 把标题栏外观应用到平台原生窗口句柄（Windows: HWND；其他平台/SDL2 后端由
// 调用方决定是否传入）。幂等、可运行时反复调用；返回是否实际生效
//（平台/系统版本不支持或句柄无效 → false，静默降级不抛不刷日志）。
bool applyTitleBarAppearance(void* nativeWindowHandle, const TitleBarAppearance& appearance);

// 把窗口效果（DWM backdrop 档位）应用到平台原生窗口句柄（磨砂设计 Phase C）。
// None/Transparent 显式设 DWMSBT_NONE（关 backdrop，透明/实色由帧缓冲 hint 与
// clearColor alpha 决定）；Acrylic/Mica/MicaAlt 设对应 SYSTEMBACKDROP 材质。
// 幂等、可运行时反复调用；返回是否实际生效（系统 < Win11 22621、非 Windows
// 平台或句柄无效 → false，静默降级不抛不刷日志；能力缓存避免逐帧重试）。
bool applyWindowEffect(void* nativeWindowHandle, WindowEffect effect);

// WindowEffect → DWM_SYSTEMBACKDROP_TYPE 值（纯函数，单测/实现共用）。
// spike 结论（2026-09-22）：DWMSBT_AUTO(0) 不可靠（只画默认标题栏后面、可能
// 被内部启发式关掉），必须显式选 NONE/材质档之一。
inline int systemBackdropValue(WindowEffect effect) {
    switch (effect) {
    case WindowEffect::Mica:    return 2; // DWMSBT_MAINWINDOW
    case WindowEffect::Acrylic: return 3; // DWMSBT_TRANSIENTWINDOW
    case WindowEffect::MicaAlt: return 4; // DWMSBT_TABBEDWINDOW
    default:                    return 1; // DWMSBT_NONE（None/Transparent：显式关）
    }
}

// applyWindowEffect 返回 false 时的降级语义（纯函数，单测/主循环共用）：
// 透明帧缓冲 hint 在创建期开启且不可逆——hint 实际生效（含 GLFW 静默回查通过）
// 则视觉上仍是 Transparent 半透档，否则 None。与请求的档位无关（失败的请求
// 只剩「能透/不能透」两种落点）。
inline WindowEffect degradedWindowEffect(WindowEffect /*desired*/, bool transparentFramebufferActive) {
    return transparentFramebufferActive ? WindowEffect::Transparent : WindowEffect::None;
}

// macOS 侧补充说明（2026-09-29 真机取证定案）：
// 1. `applyTitleBarAppearance` 在 macOS 有实现（不再是恒 false）：appearance.dark →
//    `[NSWindow setAppearance: Aqua/DarkAqua]`，与 Windows 侧 `DWMWA_USE_IMMERSIVE_
//    DARK_MODE` 同语义（标题栏深浅跟随**宿主主题**而非系统外观）；customColor 在
//    macOS 无对应 API，忽略且不影响 dark 主开关。调用方 `applyTitleBarAppearanceToWindow`
//    是 void、丢弃返回值，故返回值语义变化无调用方需要改。
// 2. 标题栏（非客户区）**不由本平台层自绘**，但 `applyWindowEffect` 会把标题窗的窗口
//    背景从 `[NSColor clearColor]` 换成「全透明但非 clear」色 —— AppKit 对背景为
//    clearColor 的非不透明窗不画标题栏底，整条标题栏会透出桌面/下层窗口（黑壁纸下与
//    「全透明」无异，用户原始投诉即此）；alpha=0 不引入着色，内容区像素实测不变。
// 3. 曾试 `NSWindowStyleMaskFullSizeContentView` + 容器 + 主题色条底让材质贯通标题栏，
//    真机实测 FSCV 把 GL surface 整体下移一个标题栏高（32pt，内容位移/底部被裁/留
//    全透明空带），与「内容区几何零位移」冲突 → 撤回；材质贯通标题栏属已知未解需求，
//    证据与候选方案见 docs/平台能力.md「窗口效果」。
// 4. macOS 上 `GLFW_TRANSPARENT_FRAMEBUFFER` 由 `![NSWindow isOpaque]` 推导，而 GL
//    后端把该属性缓存在首帧选 blit 路径 —— 平台层不得在运行期改 `isOpaque`。

} // namespace core::platform
