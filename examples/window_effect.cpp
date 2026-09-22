// examples/window_effect.cpp
// 窗口效果示例（磨砂设计文档逐 Phase 扩展）：
//   Phase A：标题栏深浅主题联动（按钮运行时切换 app::setTitleBarAppearance）
//   Phase B：透明窗口半透（windowEffect(Transparent) + app::setClearColor 运行时调 alpha）
//   Phase C：DWM backdrop 档位（app::setWindowEffect 切 None/Acrylic/Mica +
//            app::activeWindowEffect 回显实际生效/降级值）
#include "eui_neo.h"

#include <string>

namespace app {

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("Window Effect")
        .pageId("window_effect")
        .clearColor({0.16f, 0.18f, 0.20f, 1.0f})
        .windowSize(560, 480)
        // Phase B：透明帧缓冲（创建期 hint，clearColor alpha < 1 即半透；
        // GL 后端自动做 premultiplied 合成修正）。hint 常开：None 档靠
        // alpha=1 视觉等价（设计 §3.1.5），运行时四档切换不重建窗口。
        .windowEffect(core::platform::WindowEffect::Transparent);
    return config;
}

namespace {

struct WindowEffectState {
    bool darkTitleBar = false;
    int alphaPercent = 100; // clearColor alpha，100 = 不透明
    core::platform::WindowEffect effect = core::platform::WindowEffect::Transparent;
};

void applyClearColor(WindowEffectState& state) {
    const float alpha = static_cast<float>(state.alphaPercent) / 100.0f;
    setClearColor(eui::Color{0.16f, 0.18f, 0.20f, alpha});
}

const char* effectName(core::platform::WindowEffect effect) {
    switch (effect) {
    case core::platform::WindowEffect::Transparent: return "Transparent（半透）";
    case core::platform::WindowEffect::Acrylic: return "Acrylic（磨砂）";
    case core::platform::WindowEffect::Mica: return "Mica（云母）";
    case core::platform::WindowEffect::MicaAlt: return "MicaAlt（云母 Alt）";
    default: return "None（无效果）";
    }
}

void applyEffect(WindowEffectState& state, core::platform::WindowEffect effect) {
    state.effect = effect;
    setWindowEffect(effect);
}

} // namespace

void compose(eui::Ui& ui, const eui::Screen& screen) {
    WindowEffectState& state = ui.state<WindowEffectState>("effect.state");

    ui.column("root")
        .size(screen.width, screen.height)
        .padding(32.0f)
        .gap(12.0f)
        .content([&] {
            ui.text("title")
                .text("窗口效果（Phase A+B+C）")
                .fontSize(24.0f)
                .build();
            ui.text("status")
                .text(state.darkTitleBar ? "标题栏：深色" : "标题栏：浅色")
                .fontSize(15.0f)
                .build();
            // 运行时切换：即时生效（覆盖主窗口 + 之后新开的子窗口）
            components::button(ui, "toggle_titlebar")
                .text(state.darkTitleBar ? "切换到浅色标题栏" : "切换到深色标题栏")
                .onClick([&state] {
                    state.darkTitleBar = !state.darkTitleBar;
                    setTitleBarAppearance(core::platform::TitleBarAppearance{state.darkTitleBar});
                })
                .build();
            ui.text("alpha_label")
                .text("窗口不透明度：" + std::to_string(state.alphaPercent) + "%")
                .fontSize(15.0f)
                .build();
            // 运行时调 clearColor alpha：主窗口 + 存活子窗口下一帧生效
            components::button(ui, "alpha_down")
                .text("alpha - 15%")
                .onClick([&state] {
                    state.alphaPercent = state.alphaPercent > 15 ? state.alphaPercent - 15 : 15;
                    applyClearColor(state);
                })
                .build();
            components::button(ui, "alpha_up")
                .text("alpha + 15%")
                .onClick([&state] {
                    state.alphaPercent = state.alphaPercent < 100 ? state.alphaPercent + 15 : 100;
                    applyClearColor(state);
                })
                .build();
            // Phase C：DWM backdrop 档位运行时切换 + 实际生效值回显
            //（老系统降级时 activeWindowEffect 与所选档不符，本行如实显示）
            ui.text("backdrop_label")
                .text("DWM backdrop 实际生效：" + std::string(effectName(activeWindowEffect())))
                .fontSize(15.0f)
                .build();
            components::button(ui, "effect_none")
                .text("效果：无（None）")
                .onClick([&state] { applyEffect(state, core::platform::WindowEffect::None); })
                .build();
            components::button(ui, "effect_transparent")
                .text("效果：半透（Transparent）")
                .onClick([&state] { applyEffect(state, core::platform::WindowEffect::Transparent); })
                .build();
            components::button(ui, "effect_acrylic")
                .text("效果：磨砂（Acrylic，Win11 22H2+）")
                .onClick([&state] { applyEffect(state, core::platform::WindowEffect::Acrylic); })
                .build();
            components::button(ui, "effect_mica")
                .text("效果：云母（Mica，Win11 22H2+）")
                .onClick([&state] { applyEffect(state, core::platform::WindowEffect::Mica); })
                .build();
        })
        .build();
}

} // namespace app
