#include "components/input.h"
#include <iostream>
#include <vector>

int main() {
    using Model = components::input_detail::InputModel;
    core::dsl::Ui ui;
    std::vector<core::InputKey> seen;
    std::string value = "hello";

    auto compose = [&]() {
        ui.begin("root");
        components::input(ui, "field")
            .size(200.0f, 44.0f)
            .value(value)
            .onKeyEvent([&seen](const core::KeyEvent& event) {
                seen.push_back(event.key);
                // Tab 视为已消费，其余交给内置编辑。
                return event.key == core::InputKey::Tab;
            })
            .build();
        ui.end();
        ui.layout(400.0f, 300.0f);
    };
    compose();
    auto& state = ui.state<Model::InputState>("field");
    state.cursor = 5;
    Model::clearSelection(state);
    state.followCaret = false;

    auto press = [](core::InputKey key) {
        core::KeyEvent event;
        event.key = key;
        event.action = core::KeyAction::Press;
        return event;
    };

    // 已消费（Tab）：passthrough 返回 true，内置编辑完全跳过。
    if (!ui.find("field.hit")->onKeyEvent(press(core::InputKey::Tab))) {
        std::cerr << "Consumed key must report handled\\n";
        return 1;
    }
    if (state.followCaret || state.cursor != 5 || state.text != "hello") {
        std::cerr << "Consumed key must not run built-in editing\\n";
        return 2;
    }

    // 未消费（Left）：passthrough 返回 false，内置编辑照常执行。
    if (!ui.find("field.hit")->onKeyEvent(press(core::InputKey::Left))) {
        std::cerr << "Built-in Left handling must still report handled\\n";
        return 3;
    }
    if (!state.followCaret || state.cursor != 4 || state.text != "hello") {
        std::cerr << "Built-in cursor movement broke with passthrough attached\\n";
        return 4;
    }

    // 未消费且内置也不处理（F5）：整体返回未消费，passthrough 仍能观察到按键。
    if (ui.find("field.hit")->onKeyEvent(press(core::InputKey::F5))) {
        std::cerr << "Unhandled key must report unhandled\\n";
        return 5;
    }
    if (seen.size() != 3 || seen[0] != core::InputKey::Tab ||
        seen[1] != core::InputKey::Left || seen[2] != core::InputKey::F5) {
        std::cerr << "Passthrough must observe every key in order\\n";
        return 6;
    }

    // 未挂 onKeyEvent 的 input 行为不变（回归）。
    ui.begin("plain");
    components::input(ui, "plain.field").size(200.0f, 44.0f).value("abc").build();
    ui.end();
    ui.layout(400.0f, 300.0f);
    auto& plain = ui.state<Model::InputState>("plain.field");
    plain.cursor = 3;
    Model::clearSelection(plain);
    if (!ui.find("plain.field.hit")->onKeyEvent(press(core::InputKey::Backspace)) ||
        plain.text != "ab" || plain.cursor != 2) {
        std::cerr << "Input without onKeyEvent changed behavior\\n";
        return 7;
    }

    std::cout << "Input onKeyEvent passthrough consume/observe/regression passed\\n";
    return 0;
}
