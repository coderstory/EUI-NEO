#include "components/switch.h"
#include <cmath>
#include <iostream>

// 池136：开关标签垂直居中。契约：标签文本盒=整行高，垂直对齐=ink 居中
//（VerticalAlign::Center），行高保持单行紧凑（≤fontSize）。修复前为
//「盒高=fontSize + 公式 y + Top」，ink 下挂出盒、行内视觉偏下。
// 注：真实 ink 位置需字体栅格化，headless 断言锁结构契约（盒/对齐/行高）。

int main() {
    core::dsl::Ui ui;
    auto nearly = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };

    // DevDesk 消费口径（settings 开关卡 h=28，池136 现象档）
    {
        ui.begin("root");
        components::toggleSwitch(ui, "sw.28")
            .size(220.0f, 28.0f)
            .text("随应用启动代理")
            .build();
        ui.end();
        ui.layout(400.0f, 300.0f);
        auto* node = ui.find("sw.28.label");
        if (node == nullptr) {
            std::cerr << "h=28 switch label node missing\n";
            return 1;
        }
        if (node->verticalAlign != core::VerticalAlign::Center) {
            std::cerr << "switch label must ink-center vertically (Center)\n";
            return 2;
        }
        if (!nearly(node->frame.height, 28.0f)) {
            std::cerr << "switch label box must span the full row height\n";
            return 3;
        }
        if (node->lineHeight <= 0.0f || node->lineHeight > node->fontSize + 1e-4f) {
            std::cerr << "switch label lineHeight must stay single-line tight\n";
            return 4;
        }
    }

    // 默认 h=32 档同口径（组件默认高度不随 DevDesk 摆放参数漂移）
    {
        ui.begin("root2");
        components::toggleSwitch(ui, "sw.32").text("Sync").build();
        ui.end();
        ui.layout(400.0f, 300.0f);
        auto* node = ui.find("sw.32.label");
        if (node == nullptr) {
            std::cerr << "default-height switch label node missing\n";
            return 5;
        }
        if (node->verticalAlign != core::VerticalAlign::Center ||
            !nearly(node->frame.height, 32.0f)) {
            std::cerr << "default-height switch label must ink-center in a full-row box\n";
            return 6;
        }
    }

    std::cout << "switch_label_centering unit tests passed\n";
    return 0;
}
