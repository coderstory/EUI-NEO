// tests/unit/title_bar_appearance.cpp
// 标题栏外观（Phase A）：运行时覆盖优先级 / 配置快照回退 / 平台调用守卫。
// DWM 调用本身需真窗口，属 examples/window_effect 的人工验证范畴，
// 这里只测纯逻辑（磨砂设计文档 §6.2）。
#include "eui/app.h"
#include "eui/detail/dsl_app_impl.h"
#include "eui/dsl_app.h"

#include <cassert>

namespace app {

// 单测自带 dslAppConfig（与 examples 相同的注入方式）
const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("title_bar_appearance_test")
        .darkTitleBar(true);
    return config;
}

// dsl_app_impl.h 的 update/render 路径引用 app::compose，提供空实现占位
void compose(eui::Ui&, const eui::Screen&) {}

} // namespace app

int main() {
    using core::platform::TitleBarAppearance;

    // 默认值：浅色、无自定义色
    const TitleBarAppearance defaults;
    assert(!defaults.dark);
    assert(!defaults.customColor);
    assert(defaults.colorAbgr == 0);
    assert(defaults.colorAlpha == 1.0f);   // 缺省不透明（macOS 标题栏底 alpha）

    // 无运行时覆盖时：跟随 dslAppConfig 启动快照（darkTitleBar(true)）
    assert(app::currentTitleBarAppearance().dark);

    // 运行时覆盖优先于启动快照，且可反复调用
    app::setTitleBarAppearance(TitleBarAppearance{false});
    assert(!app::currentTitleBarAppearance().dark);
    app::setTitleBarAppearance(TitleBarAppearance{true});
    assert(app::currentTitleBarAppearance().dark);

    // 平台调用守卫：空句柄 → false（不崩、静默降级路径）
    assert(!core::platform::applyTitleBarAppearance(nullptr, TitleBarAppearance{true}));

    // 相等性（帧内变更检测依赖）；带多参数花括号初始化的先落局部变量，
    // 避免逗号被 assert 宏当参数分隔符
    const TitleBarAppearance dark{true};
    const TitleBarAppearance darkNoCustom{true, false, 0};
    const TitleBarAppearance darkCustom{true, true, 0};
    assert(dark == TitleBarAppearance{true});
    assert(dark != TitleBarAppearance{false});
    assert(darkNoCustom != darkCustom);
    // colorAlpha 参与相等性：帧内变更检测靠它发现「同一底色、窗口效果档位改了透明度」
    //（宿主 DevDesk 切半透/磨砂只改 clearColor 的 alpha，dark/customColor/colorAbgr 一字未变）
    const TitleBarAppearance opaqueCustom{true, true, 0x00F5F9FA, 1.0f};
    const TitleBarAppearance translucentCustom{true, true, 0x00F5F9FA, 0.9f};
    const TitleBarAppearance opaqueCustomCopy{true, true, 0x00F5F9FA, 1.0f};
    assert(opaqueCustom != translucentCustom);
    assert(opaqueCustom == opaqueCustomCopy);

    std::printf("title_bar_appearance: all checks passed\n");
    return 0;
}
