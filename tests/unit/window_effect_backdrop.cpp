// tests/unit/window_effect_backdrop.cpp
// DWM backdrop 档位（磨砂设计 Phase C）：枚举→SYSTEMBACKDROP 映射、降级语义、
// setWindowEffect/currentWindowEffect 覆盖优先级、activeWindowEffect 回退链、
// 平台调用守卫（纯逻辑部分）。DWM 调用本身需真窗口，属 examples/window_effect
// 的人工验证范畴（磨砂设计文档 §6.2）。
#include "eui/app.h"
#include "eui/detail/dsl_app_impl.h"
#include "eui/dsl_app.h"
#include "core/platform/window_effect.h"

#include <cassert>
#include <cstdio>

namespace app {

// 单测自带 dslAppConfig（与 examples 相同的注入方式）
const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("window_effect_backdrop_test")
        .windowEffect(core::platform::WindowEffect::Mica);
    return config;
}

// dsl_app_impl.h 的 update/render 路径引用 app::compose，提供空实现占位
void compose(eui::Ui&, const eui::Screen&) {}

} // namespace app

int main() {
    using core::platform::WindowEffect;

    // ---- 枚举 → DWM_SYSTEMBACKDROP_TYPE 映射（spike 结论：AUTO=0 无效必须显式）----
    assert(core::platform::systemBackdropValue(WindowEffect::None) == 1);        // DWMSBT_NONE
    assert(core::platform::systemBackdropValue(WindowEffect::Transparent) == 1); // 显式关 backdrop
    assert(core::platform::systemBackdropValue(WindowEffect::Mica) == 2);        // MAINWINDOW
    assert(core::platform::systemBackdropValue(WindowEffect::Acrylic) == 3);     // TRANSIENTWINDOW
    assert(core::platform::systemBackdropValue(WindowEffect::MicaAlt) == 4);     // TABBEDWINDOW

    // ---- 降级语义：applyWindowEffect 失败后只剩「能透/不能透」两种落点 ----
    const WindowEffect allEffects[] = {
        WindowEffect::None, WindowEffect::Transparent, WindowEffect::Acrylic,
        WindowEffect::Mica, WindowEffect::MicaAlt,
    };
    for (const WindowEffect desired : allEffects) {
        assert(core::platform::degradedWindowEffect(desired, true) == WindowEffect::Transparent);
        assert(core::platform::degradedWindowEffect(desired, false) == WindowEffect::None);
    }

    // ---- 平台调用守卫：空句柄 → false（不崩、静默降级路径）----
    assert(!core::platform::applyWindowEffect(nullptr, WindowEffect::Acrylic));

    // ---- currentWindowEffect：无覆盖时跟随启动快照（Mica）----
    assert(app::currentWindowEffect() == WindowEffect::Mica);

    // ---- setWindowEffect：覆盖优先 + 可反复调用 ----
    app::setWindowEffect(WindowEffect::Acrylic);
    assert(app::currentWindowEffect() == WindowEffect::Acrylic);
    app::setWindowEffect(WindowEffect::None);
    assert(app::currentWindowEffect() == WindowEffect::None);

    // ---- activeWindowEffect：主循环未回写时回退到期望值 ----
    assert(app::activeWindowEffect() == WindowEffect::None);
    // 主循环回写（glfw_app_main 降级后调用）之后以回写值为准
    app::detail::setActiveWindowEffect(WindowEffect::Transparent);
    assert(app::activeWindowEffect() == WindowEffect::Transparent);
    app::detail::setActiveWindowEffect(WindowEffect::Acrylic);
    assert(app::activeWindowEffect() == WindowEffect::Acrylic);

    std::printf("window_effect_backdrop: all checks passed\n");
    return 0;
}
