// tests/unit/button_flat_style.cpp
// ButtonStyle 默认无阴影契约（UAT6 阴影统一直接锁）：
//   1. 默认构造（ButtonStyle() = theme::dark() 委托）与 themed 构造
//      （ButtonStyle(tokens, primary true/false)）的 shadow.enabled 全为
//      false——按钮默认 flat（WeChat 纯平体系），根治「有些有有些没有」
//      （修复前默认挂 theme::buttonShadow 的 dark 主题 14px 阴影，所有未
//      显式覆盖的按钮都带阴影）；
//   2. 组合层：components::button 经 .theme() 走 ButtonStyle(tokens)，
//      节点 <id>.bg 的 shadow.enabled 必为 false；
//   3. 反向锁：浮动层显式覆盖通道仍然有效——.shadow(blur, x, y, color)
//      设出 enabled=true 的阴影（dialog.h primary / navbar 是真实用例），
//      根修只关默认、不吞显式。
#include "components/button.h"
#include "components/theme.h"

#include <cmath>
#include <iostream>

int main() {
    core::dsl::Ui ui;

    // ---- 1. 样式层：三个构造路径默认全 flat ----
    {
        const components::ButtonStyle defaulted;
        if (defaulted.shadow.enabled) {
            std::cerr << "ButtonStyle() default ctor must not carry shadow\n";
            return 1;
        }
        const components::theme::ThemeColorTokens dark = components::theme::dark();
        const components::ButtonStyle primaryDark(dark, true);
        const components::ButtonStyle secondaryDark(dark, false);
        if (primaryDark.shadow.enabled || secondaryDark.shadow.enabled) {
            std::cerr << "Themed ButtonStyle(tokens, primary) must not carry shadow\n";
            return 2;
        }
        const components::theme::ThemeColorTokens light = components::theme::light();
        if (components::ButtonStyle(light, true).shadow.enabled ||
            components::ButtonStyle(light, false).shadow.enabled) {
            std::cerr << "Light-theme ButtonStyle must not carry shadow\n";
            return 3;
        }
    }

    // ---- 2. 组合层：themed 按钮的 .bg 节点 shadow.enabled == false ----
    {
        ui.begin("root");
        components::button(ui, "flat1")
            .size(160.0f, 44.0f)
            .text("Apply")
            .theme(components::theme::dark(), true)
            .build();
        // 未经 .theme() 的裸构造路径（ButtonStyle() 默认ctor）同样 flat。
        components::button(ui, "flat2")
            .size(160.0f, 44.0f)
            .text("Plain")
            .build();
        ui.end();

        const auto* bg1 = ui.find("flat1.bg");
        const auto* bg2 = ui.find("flat2.bg");
        if (bg1 == nullptr || bg2 == nullptr) {
            std::cerr << "Composed buttons must expose <id>.bg nodes\n";
            return 4;
        }
        if (bg1->shadow.enabled) {
            std::cerr << "Themed button .bg must be flat (shadow disabled)\n";
            return 5;
        }
        if (bg2->shadow.enabled) {
            std::cerr << "Default-constructed button .bg must be flat\n";
            return 6;
        }
    }

    // ---- 3. 反向锁：显式 .shadow(...) 通道仍有效（浮动层钮的合法出口）----
    {
        ui.begin("root");
        components::button(ui, "floating")
            .size(160.0f, 44.0f)
            .text("Floating")
            .theme(components::theme::dark(), true)
            .shadow(10.0f, 0.0f, 3.0f,
                    core::Color{0.0f, 0.0f, 0.0f, 0.18f})
            .build();
        ui.end();

        const auto* bg = ui.find("floating.bg");
        if (bg == nullptr || !bg->shadow.enabled) {
            std::cerr << "Explicit .shadow() must stay effective for floating buttons\n";
            return 7;
        }
        if (bg->shadow.blur < 9.5f || bg->shadow.blur > 10.5f) {
            std::cerr << "Explicit shadow blur must round-trip\n";
            return 8;
        }
    }

    std::cout << "Button default-flat contract and explicit-shadow escape hatch passed\n";
    return 0;
}
