// examples/window_effect.cpp
// 窗口效果示例（磨砂设计文档 Phase A 起建）：标题栏深浅主题联动演示。
// 按钮运行时切换 app::setTitleBarAppearance，主窗口标题栏即时跟随、无需重启。
// 后续 Phase（透明/backdrop）在本例上逐档扩展。
#include "eui_neo.h"

#include <string>

namespace app {

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("Window Effect")
        .pageId("window_effect")
        .clearColor({0.16f, 0.18f, 0.20f, 1.0f})
        .windowSize(560, 220);
    return config;
}

namespace {

struct WindowEffectState {
    bool darkTitleBar = false;
};

} // namespace

void compose(eui::Ui& ui, const eui::Screen& screen) {
    WindowEffectState& state = ui.state<WindowEffectState>("effect.state");

    ui.column("root")
        .size(screen.width, screen.height)
        .padding(32.0f)
        .gap(16.0f)
        .content([&] {
            ui.text("title")
                .text("标题栏主题联动（Phase A）")
                .fontSize(24.0f)
                .build();
            ui.text("status")
                .text(state.darkTitleBar ? "当前：深色标题栏" : "当前：浅色标题栏")
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
        })
        .build();
}

} // namespace app
