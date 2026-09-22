// core/platform/window_effect.h
// 窗口非客户区（标题栏）外观的平台无关接口。
// 设计来源：docs/superpowers/specs/2026-09-22-eui-window-frost-design.md Phase A
//（DevDesk 需要标题栏主题联动：深浅主题切换即时影响窗体顶部标题栏区域）。
#pragma once

#include <cstdint>

namespace core::platform {

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

} // namespace core::platform
