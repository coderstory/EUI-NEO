// tests/unit/window_chrome.cpp
// 自绘窗口壳（undecorated 主窗口的自绘标题栏）的无头可测部分：平台能力查询、
// 空句柄/空回调安全、DslAppConfig::onCaptionHitTest 配置回写、主窗口控制动线
// 在「主循环句柄回写前」的降级（全无操作不崩）。
// WM_NCHITTEST/双击最大化/八向边缘热区需真窗口，属宿主 probe / 人工验证范畴
//（消费方 DevDesk 的 home_probes 与真机 UAT 承担）。
#include "eui/app.h"
#include "eui/detail/dsl_app_impl.h"
#include "eui/dsl_app.h"
#include "core/window/window_backend.h"

#include <cassert>
#include <cstdio>

namespace app {

// 单测自带 dslAppConfig（与 examples 相同的注入方式）
const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("window_chrome_test")
        // 拖拽区判定示例：y < 40 的横带（真实宿主按自绘标题栏几何判定）
        .onCaptionHitTest([](const CaptionHitQuery& q) { return q.y < 40.0f; });
    return config;
}

// dsl_app_impl.h 的 update/render 路径引用 app::compose，提供空实现占位
void compose(eui::Ui&, const eui::Screen&) {}

} // namespace app

int main() {
    // ---- 平台能力查询：app 层透传与 core 层一致（同一目标同一后端分支）----
    assert(app::customWindowChromeSupported() ==
           core::window::customWindowChromeSupported());

    // ---- 配置回写：builder 落字段 + 回调语义（坐标入参逐字段可达）----
    const app::DslAppConfig& config = app::dslAppConfig();
    assert(config.captionHitTestHandlerValue);
    app::CaptionHitQuery query{8.0f, 12.0f, 1000.0f, 640.0f, false};
    assert(config.captionHitTestHandlerValue(query));      // y<40 = 标题栏拖拽区
    query.y = 41.0f;
    assert(!config.captionHitTestHandlerValue(query));     // 内容区不拖拽
    query.maximized = true;  // maximized 仅作回传入参，判定权在回调（本例不变结果）
    assert(!config.captionHitTestHandlerValue(query));

    // ---- 主循环句柄回写前（无头）的降级：控制动线全无操作，不崩 ----
    app::maximizeMainWindow();
    app::restoreMainWindow();
    app::iconifyMainWindow();
    app::closeMainWindow();
    assert(!app::mainWindowMaximized());

    // ---- core 层空句柄安全：无操作 / 恒 false，不崩 ----
    core::window::maximizeWindow(nullptr);
    core::window::restoreWindow(nullptr);
    core::window::iconifyWindow(nullptr);
    assert(!core::window::windowIsMaximized(nullptr));
    // 卸载语义（空回调）对空句柄为安全无操作
    core::window::setWindowCaptionHitTestHandler(nullptr, {}, false);

    std::printf("window_chrome: all checks passed\n");
    return 0;
}
