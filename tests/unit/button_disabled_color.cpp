#include "components/button.h"
#include <cmath>
#include <iostream>

int main() {
    core::dsl::Ui ui;

    auto compose = [&](bool disabled, bool hasOverride = false,
                       core::Color overrideColor = {1.0f, 1.0f, 1.0f, 1.0f},
                       const std::string& id = "btn") {
        ui.begin("root");
        auto builder = components::button(ui, id)
            .size(160.0f, 44.0f)
            .text("Apply")
            .textColor(core::Color{0.10f, 0.12f, 0.16f, 1.0f})
            .iconColor(core::Color{0.10f, 0.12f, 0.16f, 1.0f})
            .disabled(disabled);
        if (hasOverride) {
            builder.disabledTextColor(overrideColor);
        }
        builder.build();
        ui.end();
        ui.layout(400.0f, 300.0f);
    };

    auto nearly = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
    auto sameColor = [&](const core::Color& c, float r, float g, float b, float a) {
        return nearly(c.r, r) && nearly(c.g, g) && nearly(c.b, b) && nearly(c.a, a);
    };

    // 正常态：文字颜色原样。
    compose(false);
    auto* normal = ui.find("btn.text");
    auto* normalIcon = ui.find("btn.icon");
    if (!normal || !sameColor(normal->textColor, 0.10f, 0.12f, 0.16f, 1.0f)) {
        std::cerr << "Enabled button text color changed unexpectedly\n";
        return 1;
    }
    if (normalIcon && !sameColor(normalIcon->textColor, 0.10f, 0.12f, 0.16f, 1.0f)) {
        std::cerr << "Enabled button icon color changed unexpectedly\n";
        return 2;
    }

    // disabled 默认置灰：现有 text 色 40% 透明度（alpha 同比缩放）。
    compose(true);
    auto* gray = ui.find("btn.text");
    if (!gray || !sameColor(gray->textColor, 0.10f, 0.12f, 0.16f, 0.40f)) {
        std::cerr << "Disabled button must gray text to 40% opacity\n";
        return 3;
    }
    if (!ui.find("btn.bg")->disabled) {
        std::cerr << "Disabled button background must stay non-interactive\n";
        return 4;
    }

    // disabledTextColor 覆盖：完全使用传入色。
    compose(true, true, core::Color{0.5f, 0.5f, 0.55f, 0.8f}, "btn2");
    auto* overridden = ui.find("btn2.text");
    if (!overridden || !sameColor(overridden->textColor, 0.5f, 0.5f, 0.55f, 0.8f)) {
        std::cerr << "disabledTextColor override was not applied\n";
        return 5;
    }

    std::cout << "Button disabled auto-gray, override and enabled regression passed\n";
    return 0;
}
