// tests/unit/exit_request.cpp
// requestExit / detail::consumeExitRequest（2ebe0ec）语义单测：
//   1. 初始无请求：consumeExitRequest 返回 false
//   2. requestExit → consumeExitRequest 恰好返回一次 true（原子标志取走即清）
//   3. 多次 requestExit 未消费合并为一次 true（bool 标志，非计数）
//   4. 消费后可再次请求/消费（下一轮退出循环）
//   5. 窗口回调上下文里 requestExit 可用（onWindowCreated 回调内请求）
//   6. requestWindowClose(nullptr) 空句柄守卫：no-op 不崩溃
// 真实 glfwSetWindowShouldClose / 主循环 prune 路径需真窗口，
// 属 examples 的人工验证范畴（同 pet_window_config 的边界声明）。
#include "eui/app.h"
#include "eui/detail/dsl_app_impl.h"
#include "eui/dsl_app.h"

#include <cassert>
#include <cstdio>

namespace app {

// 单测自带 dslAppConfig（与 examples 相同的注入方式）
const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}.title("exit_request_test");
    return config;
}

// dsl_app_impl.h 的 update/render 路径引用 app::compose，提供空实现占位
void compose(eui::Ui&, const eui::Screen&) {}

} // namespace app

int main() {
    using namespace app;

    // ---- 1. 初始无请求 ----
    assert(!detail::consumeExitRequest());

    // ---- 2. 请求 → 恰好消费一次 ----
    requestExit();
    assert(detail::consumeExitRequest());
    assert(!detail::consumeExitRequest());  // 取走即清，只返回一次

    // ---- 3. 多次请求合并为一次 ----
    requestExit();
    requestExit();
    requestExit();
    assert(detail::consumeExitRequest());
    assert(!detail::consumeExitRequest());

    // ---- 4. 消费后可再次进入请求/消费循环 ----
    requestExit();
    assert(detail::consumeExitRequest());
    assert(!detail::consumeExitRequest());

    // ---- 5. 窗口回调上下文里请求退出（桌宠右键菜单的真实调用形态）----
    // openWindow 只入队，onWindowCreated 由主循环消费——这里直接在回调
    // lambda 里调用并手动触发，验证回调内调用路径本身可用
    bool exitSeenInCallback = false;
    auto menuExitAction = [&exitSeenInCallback] {
        requestExit();  // 子窗口回调里无主窗口句柄，走应用级退出
        exitSeenInCallback = true;
    };
    menuExitAction();
    assert(exitSeenInCallback);
    assert(detail::consumeExitRequest());

    // ---- 6. requestWindowClose 空句柄守卫（no-op 不崩溃）----
    core::window::requestWindowClose(nullptr);
    core::window::requestWindowClose(nullptr);
    // 走到这里没崩溃 = 守卫生效（非空句柄的 GLFW/SDL 路径需真窗口）

    std::printf("exit_request: all assertions passed\n");
    return 0;
}
