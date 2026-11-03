#include "components/button.h"
#include "components/switch.h"
#include <iostream>

// SwitchBuilder/ButtonBuilder.onHover 最小断言：
// - 接了 onHover：回调必须落在 states 所在元素（switch→.hit，button→.bg）；
// - 不接 onHover：该元素回调保持空（既有调用方行为不变）。
int main() {
    core::dsl::Ui ui;
    bool switchHover = false;
    bool buttonHover = false;

    {
        ui.begin("root");
        components::toggleSwitch(ui, "sw.plain").size(120.0f, 28.0f).build();
        components::toggleSwitch(ui, "sw.hover")
            .size(120.0f, 28.0f)
            .onHover([&switchHover](bool h) { switchHover = h; })
            .build();
        ui.end();
        ui.layout(400.0f, 300.0f);

        auto* plainHit = ui.find("sw.plain.hit");
        auto* hoverHit = ui.find("sw.hover.hit");
        if (plainHit == nullptr || hoverHit == nullptr) {
            std::cerr << "switch hit nodes missing\n";
            return 1;
        }
        if (plainHit->onHoverChanged != nullptr) {
            std::cerr << "switch without onHover must leave .hit callback empty\n";
            return 2;
        }
        if (hoverHit->onHoverChanged == nullptr) {
            std::cerr << "switch onHover must land on the .hit element\n";
            return 3;
        }
        hoverHit->onHoverChanged(true);
        if (!switchHover) {
            std::cerr << "switch onHover callback not invoked on enter\n";
            return 4;
        }
        hoverHit->onHoverChanged(false);
        if (switchHover) {
            std::cerr << "switch onHover callback not invoked on leave\n";
            return 5;
        }
    }

    {
        ui.begin("root");
        components::button(ui, "btn.hover")
            .size(160.0f, 44.0f)
            .text("Apply")
            .onHover([&buttonHover](bool h) { buttonHover = h; })
            .build();
        components::button(ui, "btn.plain").size(160.0f, 44.0f).text("Apply").build();
        ui.end();
        ui.layout(400.0f, 300.0f);

        auto* bg = ui.find("btn.hover.bg");
        auto* plainBg = ui.find("btn.plain.bg");
        if (bg == nullptr || plainBg == nullptr) {
            std::cerr << "button bg nodes missing\n";
            return 6;
        }
        if (plainBg->onHoverChanged != nullptr) {
            std::cerr << "button without onHover must leave .bg callback empty\n";
            return 7;
        }
        if (bg->onHoverChanged == nullptr) {
            std::cerr << "button onHover must land on the .bg element\n";
            return 8;
        }
        bg->onHoverChanged(true);
        if (!buttonHover) {
            std::cerr << "button onHover callback not invoked\n";
            return 9;
        }
    }

    std::cout << "switch_button_hover unit tests passed\n";
    return 0;
}
