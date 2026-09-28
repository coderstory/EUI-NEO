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

#if defined(__APPLE__)
// macOS 专用：窗口 chrome 外观（标题栏条底 + 阴影），由宿主 clearColor 驱动。
// 背景：窗口带 GLFW_TRANSPARENT_FRAMEBUFFER 时 AppKit 侧被置为 setOpaque:NO +
// 背景 clearColor + 无阴影（GLFW cocoa_window.m 创建期三连），而 AppKit **只给
// 不透明窗口画标题栏背景**，非不透明窗的标题栏整条（含红黄绿交通灯那行）透出
// 桌面——Windows 侧由 DWM 代画非客户区，故无此问题。
// 实现不作为：**不改窗口不透明性/背景色**（GLFW 在 macOS 把
// GLFW_TRANSPARENT_FRAMEBUFFER 定义为 !isOpaque，而 GL 后端把该属性缓存在首个
// 渲染帧用于选 blit 路径——改 isOpaque 会把「透明帧缓冲可用」这个会话级真值带偏，
// 切档后 blit 走错路径、alpha 合成出错）。改为在窗口内容视图内铺一条「标题栏条底」：
//  - 磨砂档：材质视图覆盖整窗（含条区）→ 材质贯通标题栏；
//  - 关 / 半透档：条底铺 (r,g,b,a) = 宿主 clearColor（主题背景 + 档位 alpha）
//    → 关档实色条、半透档与内容区同透明度贯通；
//  - 阴影按 alpha 恢复（alpha >= 1 视为实色档，恢复原生窗观感；阴影不参与上述
//    属性判定）。
// 无边框窗（桌宠 sprite 窗）整体跳过：无标题栏条，且靠逐像素 alpha 透桌面，
// 返回 false。幂等、可运行时反复调用；返回是否实际应用（句柄无效/非 macOS → false）。
bool applyWindowChromeAppearance(void* nativeWindowHandle, float r, float g, float b, float a);
#endif

} // namespace core::platform
