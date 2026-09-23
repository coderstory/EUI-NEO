// tests/unit/pet_window_config.cpp
// 子窗口配置透传（桌宠设计 §2.6 G1/G2/G4/G5，2026-09-22-desktop-pet-design.md）：
// DslWindowConfig 新增项 → DslWindowRequest 字段映射、160×120 最小尺寸下限
// 放开（128×128 桌宠窗）、既有默认值向后兼容、平台样式调用守卫。
// GLFW hint / Win32 调用本身需真窗口，属 examples/pet_window 的人工验证范畴。
#include "eui/app.h"
#include "eui/detail/dsl_app_impl.h"
#include "eui/dsl_app.h"
#include "core/platform/window_style.h"

#include <cassert>
#include <cstdio>

namespace app {

// 单测自带 dslAppConfig（与 examples 相同的注入方式）
const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}.title("pet_window_config_test");
    return config;
}

// dsl_app_impl.h 的 update/render 路径引用 app::compose，提供空实现占位
void compose(eui::Ui&, const eui::Screen&) {}

} // namespace app

int main() {
    using namespace app;

    // ---- 基线：旧式 openWindow 重载/默认 DslWindowConfig 的行为不变 ----
    openWindow(DslWindowConfig{}.title("plain"), [](eui::Ui&, const eui::Screen&) {});
    std::vector<DslWindowRequest> plain = consumeWindowRequests();
    assert(plain.size() == 1);
    assert(plain[0].decorated && plain[0].resizable && !plain[0].alwaysOnTop);
    assert(!plain[0].positionSet && plain[0].focusOnShow);
    assert(!plain[0].mousePassthrough && !plain[0].hideFromTaskbar);
    assert(!plain[0].transparentFramebuffer);  // 默认不透明帧缓冲（既有行为）
    assert(plain[0].followClearColorOverride);
    assert(!plain[0].onWindowCreated);

    // ---- 桌宠形态全量配置 → DslWindowRequest 逐字段透传（G1/G4）----
    bool createdCalled = false;
    core::window::Handle createdHandle = reinterpret_cast<core::window::Handle>(0x1);
    openWindow(DslWindowConfig{}
                   .title("pet")
                   .pageId("pet")
                   .windowSize(128, 128)          // G2：低于旧 160×120 下限
                   .windowPosition(1600, 800)
                   .decorated(false)
                   .alwaysOnTop(true)
                   .resizable(false)
                   .focusOnShow(false)
                   .clickThrough(true)
                   .hideFromTaskbar(true)
                   .transparentFramebuffer(true)  // sprite 窗逐像素透明（黑底修复）
                   .ignoreClearColorOverride(true)
                   .onWindowCreated([&](core::window::Handle handle) {
                       createdCalled = true;
                       createdHandle = handle;
                   })
                   .clearColor({0.0f, 0.0f, 0.0f, 0.0f}),
               [](eui::Ui&, const eui::Screen&) {});
    std::vector<DslWindowRequest> pet = consumeWindowRequests();
    assert(pet.size() == 1);
    assert(pet[0].width == 128 && pet[0].height == 128);  // G2：不再被强行放大
    assert(pet[0].x == 1600 && pet[0].y == 800 && pet[0].positionSet);
    assert(!pet[0].decorated && pet[0].alwaysOnTop && !pet[0].resizable);
    assert(!pet[0].focusOnShow);
    assert(pet[0].mousePassthrough && pet[0].hideFromTaskbar);
    assert(pet[0].transparentFramebuffer);  // 透明 hint 不随全局效果档位回落
    assert(!pet[0].followClearColorOverride);
    assert(pet[0].clearColor.a == 0.0f);
    assert(static_cast<bool>(pet[0].onWindowCreated));
    // onWindowCreated 由主循环在真实窗口创建后触发（本测试无窗口）；
    // 这里只验证回调可空安全搬运
    assert(!createdCalled);
    (void)createdHandle;

    // ---- 极小值防御：0/负尺寸收敛为 1（不 clamp 到 160×120）----
    openWindow(DslWindowConfig{}.windowSize(0, -5), [](eui::Ui&, const eui::Screen&) {});
    std::vector<DslWindowRequest> tiny = consumeWindowRequests();
    assert(tiny.size() == 1);
    assert(tiny[0].width == 1 && tiny[0].height == 1);

    // ---- 平台样式守卫（G5）：空句柄 → false（不崩、静默降级路径）----
    assert(!core::platform::applyWindowStyleFlags(
        nullptr, core::platform::WindowStyleFlags{true, false}));

    // ---- 请求队列卫生：consume 后再 consume 为空 ----
    assert(consumeWindowRequests().empty());

    std::printf("pet_window_config: all checks passed\n");
    return 0;
}
