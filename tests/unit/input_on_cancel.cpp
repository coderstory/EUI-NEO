#include "components/input.h"
#include <iostream>
#include <string>
#include <vector>

// onCancel 语义（DevDesk 审计 [21]：Esc 从「确认」恢复为「取消」）：
//   1. 设了 onCancel：Escape → onCancel，绝不触 onEnter；Enter → onEnter（提交）
//   2. 未设 onCancel：Escape → 仍走 onEnter（旧映射，向后兼容回归）
//   3. 多行输入框同样适用（Esc 触发 onCancel，Enter 仍插入换行）
//   4. onKeyEvent passthrough 仍先于 onCancel/onEnter 消费
int main() {
    using Model = components::input_detail::InputModel;

    auto press = [](core::InputKey key) {
        core::KeyEvent event;
        event.key = key;
        event.action = core::KeyAction::Press;
        return event;
    };

    // ---- 1. onCancel + onEnter：Esc 只触发 onCancel，Enter 只触发 onEnter ----
    {
        core::dsl::Ui ui;
        std::vector<std::string> fired;
        std::string value = "draft";
        ui.begin("root");
        components::input(ui, "field")
            .size(200.0f, 44.0f)
            .value(value)
            .onEnter([&] { fired.push_back("enter"); })
            .onCancel([&] { fired.push_back("cancel"); })
            .build();
        ui.end();
        ui.layout(400.0f, 300.0f);

        if (!ui.find("field.hit")->onKeyEvent(press(core::InputKey::Escape))) {
            std::cerr << "Escape with onCancel must report handled\n";
            return 1;
        }
        if (!ui.find("field.hit")->onKeyEvent(press(core::InputKey::Enter))) {
            std::cerr << "Enter must report handled\n";
            return 2;
        }
        if (fired.size() != 2 || fired[0] != "cancel" || fired[1] != "enter") {
            std::cerr << "Esc must call onCancel only; Enter must call onEnter only\n";
            return 3;
        }
    }

    // ---- 2. 未设 onCancel：Esc 回退到 onEnter（旧行为回归） ----
    {
        core::dsl::Ui ui;
        int enterCalls = 0;
        std::string value = "legacy";
        ui.begin("root");
        components::input(ui, "legacy.field")
            .size(200.0f, 44.0f)
            .value(value)
            .onEnter([&] { ++enterCalls; })
            .build();
        ui.end();
        ui.layout(400.0f, 300.0f);

        if (!ui.find("legacy.field.hit")->onKeyEvent(press(core::InputKey::Escape))) {
            std::cerr << "Escape without onCancel must stay handled (legacy)\n";
            return 4;
        }
        if (enterCalls != 1) {
            std::cerr << "Escape must fall back to onEnter when onCancel unset\n";
            return 5;
        }
    }

    // ---- 3. 只设 onCancel（无 onEnter）：Esc 有响应且消费；Enter 空操作 ----
    {
        core::dsl::Ui ui;
        int cancelCalls = 0;
        std::string value = "x";
        ui.begin("root");
        components::input(ui, "cancel.only")
            .size(200.0f, 44.0f)
            .value(value)
            .onCancel([&] { ++cancelCalls; })
            .build();
        ui.end();
        ui.layout(400.0f, 300.0f);

        ui.find("cancel.only.hit")->onKeyEvent(press(core::InputKey::Escape));
        ui.find("cancel.only.hit")->onKeyEvent(press(core::InputKey::Enter));
        if (cancelCalls != 1) {
            std::cerr << "Escape must trigger onCancel; Enter must not\n";
            return 6;
        }
        auto& state = ui.state<Model::InputState>("cancel.only");
        if (state.text != "x") {
            std::cerr << "Enter on single-line without onEnter must not edit text\n";
            return 7;
        }
    }

    // ---- 4. 多行输入框：Esc 触发 onCancel；Enter 仍插入换行（不调 onCancel） ----
    {
        core::dsl::Ui ui;
        int cancelCalls = 0;
        std::string value = "line1";
        ui.begin("root");
        components::input(ui, "multi")
            .size(200.0f, 120.0f)
            .value(value)
            .multiline(true)
            .onEnter([&] { cancelCalls += 1000; })  // 不应被触发
            .onCancel([&] { ++cancelCalls; })
            .build();
        ui.end();
        ui.layout(400.0f, 300.0f);
        auto& state = ui.state<Model::InputState>("multi");
        state.cursor = static_cast<int>(state.text.size());

        ui.find("multi.hit")->onKeyEvent(press(core::InputKey::Enter));
        ui.find("multi.hit")->onKeyEvent(press(core::InputKey::Escape));
        if (cancelCalls != 1) {
            std::cerr << "Multiline Enter must insert newline (no callbacks); Esc must call onCancel\n";
            return 8;
        }
        if (state.text != "line1\n") {
            std::cerr << "Multiline Enter must still insert a newline\n";
            return 9;
        }
    }

    // ---- 5. passthrough 先于 onCancel：消费 Escape 时 onCancel 不触发 ----
    {
        core::dsl::Ui ui;
        int cancelCalls = 0;
        std::string value = "v";
        ui.begin("root");
        components::input(ui, "with.passthrough")
            .size(200.0f, 44.0f)
            .value(value)
            .onEnter([] {})
            .onCancel([&] { ++cancelCalls; })
            .onKeyEvent([](const core::KeyEvent& event) {
                return event.key == core::InputKey::Escape;  // 吞掉 Esc
            })
            .build();
        ui.end();
        ui.layout(400.0f, 300.0f);

        if (!ui.find("with.passthrough.hit")->onKeyEvent(press(core::InputKey::Escape))) {
            std::cerr << "Consumed Escape must report handled\n";
            return 10;
        }
        if (cancelCalls != 0) {
            std::cerr << "Passthrough must win over onCancel\n";
            return 11;
        }
    }

    std::cout << "Input onCancel Escape-semantics passed\n";
    return 0;
}
