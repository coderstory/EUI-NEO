// tests/unit/window_effect_backdrop_state.cpp
// debt268 P2-4：applyWindowEffect 判定纯函数 backdropTransitionFor 的 4 分支
//（材质挂过 × 关材质档/材质档 → KeepUntouched/ApplyMaterial/SwitchMaterial/
// ReleaseMaterial）+ 过渡后 materialAppliedAfter 标记账。此前该状态机从未写过
// 自动化测试（从未写过/材质挂过切 None/AUTO 复位失败三态零覆盖）。
// DWM 调用本身需真窗口，仍归 examples/window_effect 人工验证。
#include "core/platform/window_effect.h"

#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    using core::platform::BackdropTransition;
    using core::platform::systemBackdropValue;
    using VE = core::platform::WindowEffect;

    const int none = systemBackdropValue(VE::None);            // 1 = DWMSBT_NONE
    const int mica = systemBackdropValue(VE::Mica);            // 2
    const int acrylic = systemBackdropValue(VE::Acrylic);      // 3
    const int micaAlt = systemBackdropValue(VE::MicaAlt);      // 4

    // ---- 分支 1：从未挂材质 + 材质档 → 写档位并置「已挂」----
    for (const int value : {mica, acrylic, micaAlt}) {
        assert(core::platform::backdropTransitionFor(false, value) ==
               BackdropTransition::ApplyMaterial);
        assert(core::platform::materialAppliedAfter(
                   core::platform::backdropTransitionFor(false, value)));
    }

    // ---- 分支 2：已挂材质 + 材质档 → 换档位，「已挂」保持 ----
    for (const int value : {mica, acrylic, micaAlt}) {
        assert(core::platform::backdropTransitionFor(true, value) ==
               BackdropTransition::SwitchMaterial);
        assert(core::platform::materialAppliedAfter(
                   core::platform::backdropTransitionFor(true, value)));
    }

    // ---- 分支 3：已挂材质 + 关材质档 → 写 NONE 关材质 + AUTO 复位 + blur 恢复 ----
    // None 与 Transparent 同映射 DWMSBT_NONE（显式关档），判定同分支。
    for (const int value : {none, systemBackdropValue(VE::Transparent)}) {
        assert(core::platform::backdropTransitionFor(true, value) ==
               BackdropTransition::ReleaseMaterial);
        assert(!core::platform::materialAppliedAfter(
                   core::platform::backdropTransitionFor(true, value)));
    }

    // ---- 分支 4：从未挂材质 + 关材质档 → 完全不动 SYSTEMBACKDROP（保 GLFW
    // 创建期逐像素 alpha；写了含 NONE 在内都会让 DWM 接管、拒收后续 blur-behind）----
    for (const int value : {none, systemBackdropValue(VE::Transparent)}) {
        assert(core::platform::backdropTransitionFor(false, value) ==
               BackdropTransition::KeepUntouched);
        assert(!core::platform::materialAppliedAfter(
                   core::platform::backdropTransitionFor(false, value)));
    }

    // ---- bug266：24H2 accent 路线的档位→ACCENT 参数映射 ----
    // bug288（2026-11-17 老板令）：磨砂撤回 ACRYLICBLURBEHIND，「磨砂直接叠加
    // 半透」——磨砂族与 Transparent 同走 TRANSPARENTGRADIENT(2)、tint/flags 全同。
    for (const VE effect : {VE::Acrylic, VE::Mica, VE::MicaAlt, VE::Transparent}) {
        assert(core::platform::accentStateFor(effect) == 2);
        assert(core::platform::accentFlagsFor(effect) == 0);
        assert(core::platform::accentGradientColorFor(effect) == 0x01000000u);
    }
    assert(core::platform::accentStateFor(VE::None) == 0);
    assert(core::platform::accentFlagsFor(VE::None) == 0);
    // tint alpha 字节必须 ≥1（GradientColor alpha=0 在 24H2 有黑底已知问题）
    assert((core::platform::accentGradientColorFor(VE::Mica) >> 24) >= 1);

    std::printf("window_effect_backdrop_state: all checks passed\n");
    return 0;
}
