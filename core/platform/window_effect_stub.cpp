// core/platform/window_effect_stub.cpp
// 非 Windows 平台占位（macOS 的 NSVisualEffectView 磨砂 + titlebarAppearsTransparent
// 属磨砂设计文档 Phase E；Linux 暂无）。返回 false = 未生效，调用方静默降级。
#include "core/platform/window_effect.h"

namespace core::platform {

bool applyTitleBarAppearance(void*, const TitleBarAppearance&) {
    return false;
}

bool applyWindowEffect(void*, WindowEffect) {
    return false;
}

} // namespace core::platform
