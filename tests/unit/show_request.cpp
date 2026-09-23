// tests/unit/show_request.cpp
// requestShow / detail::consumeShowRequest 语义单测（与 exit_request 同构）：
//   1. 初始无请求：consumeShowRequest 返回 false
//   2. requestShow → consumeShowRequest 恰好返回一次 true（原子标志取走即清）
//   3. 多次 requestShow 未消费合并为一次 true（bool 标志，非计数）
//   4. 消费后可再次请求/消费（下一轮循环）
//   5. 与 requestExit 标志互不干扰（两个独立标志位）
//   6. 子窗口回调上下文里 requestShow 可用（桌宠/托盘菜单的真实调用形态）
// 主循环的 restoreWindowFromTray / glfwRestoreWindow 路径需真窗口，
// 属 examples 的人工验证范畴（同 exit_request 的边界声明）。
#include "eui/app.h"
#include "eui/detail/dsl_app_impl.h"
#include "eui/dsl_app.h"

#include <cassert>
#include <cstdio>

namespace app {

// 单测自带 dslAppConfig（与 examples 相同的注入方式）
const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}.title("show_request_test");
    return config;
}

// dsl_app_impl.h 的 update/render 路径引用 app::compose，提供空实现占位
void compose(eui::Ui&, const eui::Screen&) {}

} // namespace app

int main() {
    using namespace app;

    // ---- 1. 初始无请求 ----
    assert(!detail::consumeShowRequest());

    // ---- 2. 请求 → 恰好消费一次 ----
    requestShow();
    assert(detail::consumeShowRequest());
    assert(!detail::consumeShowRequest());  // 取走即清，只返回一次

    // ---- 3. 多次请求合并为一次 ----
    requestShow();
    requestShow();
    requestShow();
    assert(detail::consumeShowRequest());
    assert(!detail::consumeShowRequest());

    // ---- 4. 消费后可再次进入请求/消费循环 ----
    requestShow();
    assert(detail::consumeShowRequest());
    assert(!detail::consumeShowRequest());

    // ---- 5. 与 requestExit 标志互不干扰（两个独立标志位）----
    requestShow();
    requestExit();
    assert(detail::consumeExitRequest());
    assert(!detail::consumeExitRequest());
    assert(detail::consumeShowRequest());  // exit 消费不影响 show 标志
    assert(!detail::consumeShowRequest());

    // ---- 6. 子窗口回调上下文里请求显示（托盘菜单「显示主窗口」的形态）----
    bool showSeenInCallback = false;
    auto trayShowAction = [&showSeenInCallback] {
        requestShow();  // 自定义托盘菜单项：无主窗口句柄，走应用级 API
        showSeenInCallback = true;
    };
    trayShowAction();
    assert(showSeenInCallback);
    assert(detail::consumeShowRequest());

    std::printf("show_request: all assertions passed\n");
    return 0;
}
