// examples/pet_window.cpp
// 桌宠形态子窗口示例（桌宠设计文档 2026-09-22 §2.6 G1-G5 / M1）：
//   - 无边框 + 置顶 + 不可缩放 + 显示不抢焦点 + 不进任务栏/Alt+Tab（G1/G5）
//   - 128×128 小尺寸（G2：旧 openWindow 有 160×120 下限）
//   - clearColor alpha=0 像素级透明（透明 hint 常开 + ignoreClearColorOverride，
//     全局 setClearColor 广播不冲掉 sprite 窗自己的底色）
//   - onDrag + core::window::setWindowPos 拖拽移动（G3）
//   - 右键菜单运行时切换鼠标穿透（G4：setWindowMousePassthrough）
// 主窗口是控制面板（开关桌宠窗口 / 穿透切换），桌宠窗口是一个自绘小方块
// （真实皮肤走 sprite PNG，见 DevDesk module_pet）。
#include "eui_neo.h"

#include <string>

namespace app {

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("Pet Window")
        .pageId("pet_window")
        .clearColor({0.16f, 0.18f, 0.20f, 1.0f})
        .windowSize(420, 320)
        // 透明帧缓冲 hint 常开：子窗口跟主窗口同档创建（桌宠窗据此获得
        // 逐像素透明能力），主窗口自身 alpha=1 视觉等价实色
        .windowEffect(core::platform::WindowEffect::Transparent);
    return config;
}

namespace {

// 桌宠窗口句柄：onWindowCreated 回调里捕获（示例只有一个桌宠窗）
core::window::Handle& petHandle() {
    static core::window::Handle handle = nullptr;
    return handle;
}

struct PetWindowState {
    bool passthrough = false;
    bool menuOpen = false;
    float menuX = 0.0f;
    float menuY = 0.0f;
    bool blink = false;
};

void applyPassthrough(PetWindowState& state, bool enabled) {
    state.passthrough = enabled;
    core::window::setWindowMousePassthrough(petHandle(), enabled);
}

// 桌宠窗 compose：透明底 + 可拖拽的"宠物"（自绘方块 + 眨眼）+ 右键菜单
void petCompose(eui::Ui& ui, const eui::Screen& screen) {
    PetWindowState& state = ui.state<PetWindowState>("pet.state");

    ui.stack("pet.root")
        .size(screen.width, screen.height)
        .content([&] {
            // "宠物"本体：整窗即紧包围盒（MVP 命中方案，逐像素 alpha 命中是 M3）
            ui.rect("pet.body")
                .size(screen.width, screen.height)
                .color({0.85f, 0.47f, 0.34f, 1.0f})   // Claude Clay #D97757
                .radius(28.0f)
                .onDrag([](const core::dsl::DragEvent& event) {
                    // 逐事件按 delta 移动窗口（无需记录拖拽基点）
                    if (petHandle() == nullptr) return;
                    int x = 0;
                    int y = 0;
                    core::window::getWindowPos(petHandle(), x, y);
                    core::window::setWindowPos(
                        petHandle(),
                        x + static_cast<int>(event.deltaX),
                        y + static_cast<int>(event.deltaY));
                })
                .onContextMenu([&state](const eui::PointerEvent& event, const eui::Rect&) {
                    state.menuX = event.x;
                    state.menuY = event.y;
                    state.menuOpen = true;
                })
                .build();
            // 眨眼：onTimer 周期切换两只"眼睛"
            ui.rect("pet.eye.left")
                .position(34.0f, 40.0f)
                .size(18.0f, state.blink ? 3.0f : 18.0f)
                .color({0.14f, 0.12f, 0.10f, 1.0f})
                .radius(9.0f)
                .onTimer(0.6f, [&state] { state.blink = !state.blink; })
                .build();
            ui.rect("pet.eye.right")
                .position(76.0f, 40.0f)
                .size(18.0f, state.blink ? 3.0f : 18.0f)
                .color({0.14f, 0.12f, 0.10f, 1.0f})
                .radius(9.0f)
                .build();
            ui.rect("pet.mouth")
                .position(48.0f, 78.0f)
                .size(32.0f, 6.0f)
                .color({0.14f, 0.12f, 0.10f, 1.0f})
                .radius(3.0f)
                .build();
        })
        .build();

    // 右键菜单：穿透切换（穿透后点击落到下层窗口，需要先从主窗口关掉）
    components::contextMenu(ui, "pet.menu")
        .theme(components::theme::dark())
        .screen(screen.width, screen.height)
        .position(state.menuX, state.menuY)
        .items(std::vector<components::ContextMenuItem>{
            {state.passthrough ? "关闭鼠标穿透" : "开启鼠标穿透（挂机）"},
        })
        .open(state.menuOpen)
        .onSelect([&state](int) {
            applyPassthrough(state, !state.passthrough);
            state.menuOpen = false;
        })
        .onOpenChange([&state](bool open) { state.menuOpen = open; })
        .build();
}

void openPetWindow() {
    openWindow(DslWindowConfig{}
                   .title("Pet")
                   .pageId("pet")
                   .windowSize(128, 128)          // G2：低于旧 160×120 下限
                   .windowPosition(1200, 600)
                   .decorated(false)              // G1：无边框
                   .alwaysOnTop(true)             // G1：置顶
                   .resizable(false)
                   .focusOnShow(false)            // G1：不抢焦点
                   .hideFromTaskbar(true)         // G5：不进任务栏/Alt+Tab
                   .ignoreClearColorOverride()   // 自管背景：全局 setClearColor 不冲底色
                   .clearColor({0.0f, 0.0f, 0.0f, 0.0f})  // 全透底
                   .onWindowCreated([](core::window::Handle handle) {
                       petHandle() = handle;      // G3/G4：拖拽/穿透切换要用
                   }),
               petCompose);
}

} // namespace

void compose(eui::Ui& ui, const eui::Screen& screen) {
    struct PanelState {
        bool petOpen = false;
        bool passthrough = false;
    };
    PanelState& state = ui.state<PanelState>("panel.state");

    ui.column("root")
        .size(screen.width, screen.height)
        .padding(32.0f)
        .gap(12.0f)
        .content([&] {
            ui.text("title").text("桌宠形态子窗口（G1-G5）").fontSize(24.0f).build();
            ui.text("hint")
                .text("小窗：无边框/置顶/透明/不抢焦点/不在任务栏，可拖拽，右键切穿透")
                .fontSize(14.0f)
                .build();
            components::button(ui, "open_pet")
                .text(state.petOpen ? "再开一个桌宠窗口" : "打开桌宠窗口")
                .onClick([&state] {
                    state.petOpen = true;
                    openPetWindow();
                })
                .build();
            // 穿透时桌宠窗收不到点击（点击落到下层），从主窗这里也能切回
            components::button(ui, "toggle_passthrough")
                .text(state.passthrough ? "关闭穿透（主窗控制）" : "开启穿透（主窗控制）")
                .onClick([&state] {
                    state.passthrough = !state.passthrough;
                    core::window::setWindowMousePassthrough(petHandle(), state.passthrough);
                })
                .build();
        })
        .build();
}

} // namespace app
