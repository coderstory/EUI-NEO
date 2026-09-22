// tests/unit/window_effect_clear_color.cpp
// 窗口效果 + 运行时 clearColor（磨砂设计 Phase B）：配置字段默认值/回写、
// setClearColor 覆盖优先级、DslWindowRuntime setter 生效链路（纯逻辑部分）。
// 透明 hint / premultiply blit / DWM 调用需真窗口，属 examples/window_effect
// 的人工验证范畴（磨砂设计文档 §6.2）。
#include "eui/app.h"
#include "eui/detail/dsl_app_impl.h"
#include "eui/dsl_app.h"
#include "core/app/dsl_window_runtime.h"
#include "core/window/window_types.h"

#include <cassert>
#include <cstdio>

namespace app {

// 单测自带 dslAppConfig（与 examples 相同的注入方式）
const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("window_effect_clear_color_test")
        .clearColor({0.25f, 0.50f, 0.75f, 1.0f});
    return config;
}

// dsl_app_impl.h 的 update/render 路径引用 app::compose，提供空实现占位
void compose(eui::Ui&, const eui::Screen&) {}

} // namespace app

int main() {
    using core::platform::WindowEffect;

    // ---- 窗口层字段：默认 None（向后兼容，实色路径不回归）----
    const core::window::WindowCreateRequest defaultRequest;
    assert(defaultRequest.windowEffect == WindowEffect::None);

    // ---- 配置字段：默认值 + 流式回写 ----
    const app::DslAppConfig defaultConfig;
    assert(defaultConfig.windowEffectValue == WindowEffect::None);
    app::DslAppConfig config;
    config.windowEffect(WindowEffect::Transparent);
    assert(config.windowEffectValue == WindowEffect::Transparent);
    config.windowEffect(WindowEffect::Acrylic);
    assert(config.windowEffectValue == WindowEffect::Acrylic);
    assert(app::windowEffect() == WindowEffect::None); // 未设置时读启动快照默认

    // ---- clearColor：无覆盖时跟随启动快照 ----
    const eui::Color snapshot = app::currentClearColor();
    assert(snapshot.r == 0.25f && snapshot.g == 0.50f && snapshot.b == 0.75f && snapshot.a == 1.0f);

    // ---- setClearColor：覆盖优先 + 可反复调用 ----
    const eui::Color translucent{0.10f, 0.12f, 0.14f, 0.85f};
    app::setClearColor(translucent);
    const eui::Color overridden = app::currentClearColor();
    assert(overridden.r == 0.10f && overridden.g == 0.12f &&
           overridden.b == 0.14f && overridden.a == 0.85f);
    app::setClearColor(eui::Color{0.0f, 0.0f, 0.0f, 1.0f});
    assert(app::currentClearColor().a == 1.0f);

    // ---- DslWindowRuntime::setClearColor：值变化更新快照，幂等 ----
    // 不调用 initialize（避免真窗口依赖），setter 只碰 request_ + 重绘标志。
    app::DslWindowRuntime runtime;
    assert(runtime.request().clearColor.a == 1.0f);
    runtime.setClearColor(translucent);
    assert(runtime.request().clearColor.r == 0.10f &&
           runtime.request().clearColor.g == 0.12f &&
           runtime.request().clearColor.b == 0.14f &&
           runtime.request().clearColor.a == 0.85f);
    // 同值再调是幂等 no-op（不触发额外重绘路径，仅验证不崩溃/值不变）
    runtime.setClearColor(translucent);
    assert(runtime.request().clearColor.a == 0.85f);

    std::printf("window_effect_clear_color: all checks passed\n");
    return 0;
}
