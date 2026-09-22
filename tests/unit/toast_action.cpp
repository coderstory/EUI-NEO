#include "components/toast.h"
#include <iostream>

int main() {
    core::dsl::Ui ui;

    // 默认无 action：不产生任何按钮元素，完全向后兼容。
    ui.begin("plain");
    components::toast(ui, "plain.toast")
        .visible(true)
        .screen(800.0f, 600.0f)
        .title("Saved")
        .message("File written")
        .build();
    ui.end();
    ui.layout(800.0f, 600.0f);
    if (ui.find("plain.toast.action.hit") || ui.find("plain.toast.action")) {
        std::cerr << "Default toast must not render an action button\n";
        return 1;
    }
    if (!ui.find("plain.toast.close.hit")) {
        std::cerr << "Default toast lost its close button\n";
        return 2;
    }

    // 带 action：按钮存在、点击先执行回调再关闭、不可见时禁用。
    int actionRuns = 0;
    int dismissRuns = 0;
    auto compose = [&] {
        ui.begin("withAction");
        components::toast(ui, "act.toast")
            .visible(true)
            .screen(800.0f, 600.0f)
            .size(360.0f, 120.0f)
            .title("External change")
            .message("File changed on disk")
            .action("Reload", [&actionRuns] { ++actionRuns; })
            .onDismiss([&dismissRuns] { ++dismissRuns; })
            .build();
        ui.end();
        ui.layout(800.0f, 600.0f);
    };
    compose();
    auto* hit = ui.find("act.toast.action.hit");
    auto* label = ui.find("act.toast.action");
    auto* card = ui.find("act.toast");
    if (!hit || !label || !card) {
        std::cerr << "action() did not render the action button\n";
        return 3;
    }
    if (hit->frame.width < 56.0f ||
        hit->frame.x + hit->frame.width > card->frame.x + card->frame.width + 0.5f ||
        hit->frame.y + hit->frame.height > card->frame.y + card->frame.height + 0.5f ||
        label->frame.x < hit->frame.x) {
        std::cerr << "Action button layout is out of place\n";
        return 4;
    }
    if (!hit->onClick) {
        std::cerr << "Action button has no click handler\n";
        return 5;
    }
    hit->onClick();
    if (actionRuns != 1 || dismissRuns != 1) {
        std::cerr << "Action click must run callback then dismiss\n";
        return 6;
    }
    hit->onClick();
    if (actionRuns != 2 || dismissRuns != 2) {
        std::cerr << "Action click is not repeatable\n";
        return 7;
    }

    // 不可见时按钮禁用（与 close.hit 一致）。
    ui.begin("hidden");
    components::toast(ui, "hidden.toast")
        .visible(false)
        .screen(800.0f, 600.0f)
        .action("Reload", [] {})
        .build();
    ui.end();
    ui.layout(800.0f, 600.0f);
    auto* hiddenHit = ui.find("hidden.toast.action.hit");
    if (!hiddenHit || !hiddenHit->disabled) {
        std::cerr << "Hidden toast action button must be disabled\n";
        return 8;
    }

    std::cout << "Toast action button defaults, layout, click ordering and disabled state passed\n";
    return 0;
}
