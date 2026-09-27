// examples/scale_contract.cpp
// U1 尺度契约示例（第一版，纯增量）——新 API 的三处读法：
//   1) app::contentScale()      主窗口内容缩放系数（physical = logical * scale）
//   2) Screen::scale            compose 里拿到的同一系数
//   3) PointerEvent/DragEvent 的 scale + space
//      space 说明该回调坐标处在哪个空间（既有语义不变）：
//        onPress/onRelease/onDrag -> Physical（原始物理像素 = 帧缓冲像素）
//        onMove/onContextMenu     -> Logical（框架已除过 scale，逻辑像素）
// 窗口在 HiDPI 显示器上跑（Windows 缩放 125% => scale 1.25）即可看到数值变化。
#include "eui_neo.h"

#include <string>

namespace app {

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("Scale Contract")
        .pageId("scale_contract")
        .windowSize(600, 460);
    return config;
}

namespace {

struct ScaleState {
    float eventScale = 1.0f;
    bool eventLogical = false;
    double eventX = 0.0;
    double dragDeltaX = 0.0;
};

/// 统一换算：任意指针回调都能把坐标归一成逻辑像素（DSL 布局单位）。
/// 物理 -> 逻辑 除以 scale；逻辑 -> 物理 乘 scale。
double toLogicalX(double value, float scale, core::PointerSpace space) {
    return space == core::PointerSpace::Logical ? value : value / static_cast<double>(scale);
}

const char* spaceName(bool logical) {
    return logical ? "Logical（逻辑像素）" : "Physical（物理像素）";
}

} // namespace

void compose(eui::Ui& ui, const eui::Screen& screen) {
    ScaleState& state = ui.state<ScaleState>("scale.state");

    ui.column("root")
        .size(screen.width, screen.height)
        .padding(32.0f)
        .gap(12.0f)
        .content([&] {
            ui.text("title").text("U1 尺度契约（纯增量 API）").fontSize(24.0f).build();
            // Screen::scale 与 app::contentScale() 主窗口同源同值。
            ui.text("scale")
                .text("Screen::scale = " + std::to_string(screen.scale) +
                      "    app::contentScale() = " + std::to_string(contentScale()))
                .fontSize(15.0f)
                .build();
            ui.text("event")
                .text("最近事件：" + std::string(spaceName(state.eventLogical)) +
                      "  scale = " + std::to_string(state.eventScale) +
                      "  x = " + std::to_string(state.eventX) +
                      "  -> 逻辑 x = " +
                      std::to_string(toLogicalX(state.eventX, state.eventScale,
                                               state.eventLogical ? core::PointerSpace::Logical
                                                                  : core::PointerSpace::Physical)))
                .fontSize(15.0f)
                .build();
            ui.text("drag")
                .text("拖拽增量（物理像素）= " + std::to_string(state.dragDeltaX))
                .fontSize(15.0f)
                .build();
            // 探针：按下/拖拽拿到 Physical，悬停拿到 Logical（space 字段自行说明）。
            ui.rect("probe")
                .size(280.0f, 120.0f)
                .color({0.35f, 0.45f, 0.60f, 1.0f})
                .onPress([&state](const core::PointerEvent& event, const core::Rect&) {
                    state.eventScale = event.scale;
                    state.eventLogical = event.space == core::PointerSpace::Logical;
                    state.eventX = event.x;
                })
                .onMove([&state](const core::PointerEvent& event, const core::Rect&) {
                    state.eventScale = event.scale;
                    state.eventLogical = event.space == core::PointerSpace::Logical;
                    state.eventX = event.x;
                })
                .onDrag([&state](const core::dsl::DragEvent& event) {
                    state.eventScale = event.scale;
                    state.eventLogical = event.space == core::PointerSpace::Logical;
                    state.dragDeltaX = event.deltaX;
                })
                .build();
        })
        .build();
}

} // namespace app
