#include "eui_neo.h"

#include <functional>
#include <string>

namespace app {

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("Switch Position")
        .pageId("switch_position")
        .clearColor({0.055f, 0.062f, 0.075f, 1.0f})
        .windowSize(620, 360)
        .fps(90.0);
    return config;
}

namespace {

struct SwitchState {
    bool autoSync = true;
    bool autoPush = false;
    bool compactRows = true;
};

constexpr float kCardWidth = 480.0f;
constexpr float kCardHeight = 184.0f;
constexpr float kPad = 20.0f;
constexpr float kRowHeight = 40.0f;
constexpr float kRowGap = 12.0f;
constexpr float kLabelWidth = 240.0f;
constexpr float kLabelHeight = 18.0f;
constexpr float kSwitchWidth = 132.0f;
constexpr float kSwitchHeight = 26.0f;

// Row shape "label left + switch right, vertically centered": the switch is
// placed with .position() directly — no wrapping positioned stack needed.
void row(eui::Ui& ui, const std::string& id, int index, const char* label,
         bool checked, const components::theme::ThemeColorTokens& tokens,
         std::function<void(bool)> onChange) {
    const float rowTop = kPad + static_cast<float>(index) * (kRowHeight + kRowGap);
    const float switchY = rowTop + (kRowHeight - kSwitchHeight) * 0.5f;
    ui.text(id + ".label")
        .position(kPad, switchY + (kSwitchHeight - kLabelHeight) * 0.5f)
        .size(kLabelWidth, kLabelHeight)
        .text(label)
        .fontSize(14.0f)
        .lineHeight(kLabelHeight)
        .color(tokens.text)
        .build();
    components::toggleSwitch(ui, id + ".switch")
        .position(kCardWidth - kPad - kSwitchWidth, switchY)
        .size(kSwitchWidth, kSwitchHeight)
        .checked(checked)
        .theme(tokens)
        .onChange(std::move(onChange))
        .build();
}

} // namespace

void compose(eui::Ui& ui, const eui::Screen& screen) {
    SwitchState& state = ui.state<SwitchState>("switch.position.state");
    const components::theme::ThemeColorTokens tokens = components::theme::dark();

    ui.stack("page")
        .size(screen.width, screen.height)
        .align(eui::Align::CENTER, eui::Align::CENTER)
        .content([&] {
            ui.rect("card.background")
                .size(kCardWidth, kCardHeight)
                .radius(14.0f)
                .color(tokens.surface)
                .build();

            row(ui, "row.sync", 0, "Auto sync", state.autoSync, tokens,
                [&state](bool value) { state.autoSync = value; });
            row(ui, "row.push", 1, "Auto push", state.autoPush, tokens,
                [&state](bool value) { state.autoPush = value; });
            row(ui, "row.compact", 2, "Compact rows", state.compactRows, tokens,
                [&state](bool value) { state.compactRows = value; });
        })
        .build();
}

} // namespace app
