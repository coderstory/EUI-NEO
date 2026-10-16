#include "components/switch.h"
#include <cmath>
#include <iostream>

int main() {
    core::dsl::Ui ui;
    auto nearly = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };

    // 默认（不设 position）：根节点留在 flow 原点。
    {
        ui.begin("root");
        components::toggleSwitch(ui, "sw.flow").size(120.0f, 28.0f).text("Sync").build();
        ui.end();
        ui.layout(400.0f, 300.0f);
        auto* node = ui.find("sw.flow");
        if (node == nullptr) {
            std::cerr << "flow switch node missing\n";
            return 1;
        }
        if (!nearly(node->frame.x, 0.0f) || !nearly(node->frame.y, 0.0f)) {
            std::cerr << "switch without position must stay at flow origin\n";
            return 2;
        }
    }

    // .position(x, y)：落在根节点 frame 上。
    {
        ui.begin("root");
        components::toggleSwitch(ui, "sw.pos")
            .position(140.0f, 36.0f)
            .size(120.0f, 28.0f)
            .text("Sync")
            .build();
        ui.end();
        ui.layout(400.0f, 300.0f);
        auto* node = ui.find("sw.pos");
        if (node == nullptr) {
            std::cerr << "positioned switch node missing\n";
            return 3;
        }
        if (!nearly(node->frame.x, 140.0f) || !nearly(node->frame.y, 36.0f)) {
            std::cerr << "switch .position(x, y) was not applied to the root frame\n";
            return 4;
        }
    }

    // 只设 x / 只设 y：未设置的轴保持 flow。
    {
        ui.begin("root");
        components::toggleSwitch(ui, "sw.xonly").x(64.0f).size(120.0f, 28.0f).build();
        components::toggleSwitch(ui, "sw.yonly").y(48.0f).size(120.0f, 28.0f).build();
        ui.end();
        ui.layout(400.0f, 300.0f);
        auto* xNode = ui.find("sw.xonly");
        auto* yNode = ui.find("sw.yonly");
        if (xNode == nullptr || yNode == nullptr) {
            std::cerr << "x-only/y-only switch nodes missing\n";
            return 5;
        }
        if (!nearly(xNode->frame.x, 64.0f) || !nearly(xNode->frame.y, 0.0f)) {
            std::cerr << "switch .x() must move x and leave y in flow\n";
            return 6;
        }
        if (!nearly(yNode->frame.y, 48.0f) || !nearly(yNode->frame.x, 0.0f)) {
            std::cerr << "switch .y() must move y and leave x in flow\n";
            return 7;
        }
    }

    std::cout << "switch_position unit tests passed\n";
    return 0;
}
