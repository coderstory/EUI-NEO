// tests/unit/window_lifecycle_api.cpp
// 子窗口生命周期能力的无头单测（need272）：
//   1. DslWindowConfig.onCloseRequested → DslWindowRequest.closeRequestedHandler
//      透传：非空、可调用、返回值与句柄原样传递；未注册 = 空（默认零回归）
//   2. setQuitGuard：未设不拦截 / 返回 false 拦截（每次判定都问，无缓存）/
//      返回 true 放行 / 传空清除
//   3. guard × requestExit 组合语义：主循环判定等价式
//      consumeExitRequest() && shouldProceedExit()
//   4. requestWindowFocus(nullptr) / setWindowTitle(nullptr, ...) 空句柄守卫
// 真窗口路径（focus 实际生效 / 标题即时可见 / close callback 拦截）由真机
// fixture（window_lifecycle_viewer）覆盖，此处不重复。
#include "eui/app.h"
#include "eui/detail/dsl_app_impl.h"
#include "eui/dsl_app.h"
#include "core/window/window_backend.h"

#include <cassert>
#include <cstdio>

namespace app {

// 单测自带 dslAppConfig（与 examples 相同的注入方式）
const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}.title("window_lifecycle_api_test");
    return config;
}

// dsl_app_impl.h 的 update/render 路径引用 app::compose，提供空实现占位
void compose(eui::Ui&, const eui::Screen&) {}

} // namespace app

int main() {
    using namespace app;
    auto kSentinel = reinterpret_cast<core::window::Handle>(0x1);

    // ---- 1. onCloseRequested 透传：非空 + 可调用 + 返回值/句柄原样 ----
    {
        core::window::Handle seen = nullptr;
        bool verdict = true;
        openWindow(DslWindowConfig{}
                       .title("veto")
                       .onCloseRequested([&seen, &verdict](core::window::Handle h) {
                           seen = h;
                           return verdict;
                       }),
                   [](eui::Ui&, const eui::Screen&) {});
        auto requests = consumeWindowRequests();
        assert(requests.size() == 1);
        assert(requests[0].closeRequestedHandler != nullptr);
        assert(requests[0].closeRequestedHandler(kSentinel) == true);
        assert(seen == kSentinel);
        verdict = false;
        assert(requests[0].closeRequestedHandler(kSentinel) == false);
    }

    // ---- 2. 未注册 = 空（默认行为与既有版本一致）----
    {
        openWindow(DslWindowConfig{}.title("plain"), [](eui::Ui&, const eui::Screen&) {});
        auto requests = consumeWindowRequests();
        assert(requests.size() == 1);
        assert(!requests[0].closeRequestedHandler);
    }

    // ---- 3. setQuitGuard 状态机 ----
    setQuitGuard(nullptr);  // 清场（幂等）
    assert(detail::shouldProceedExit());  // 未设 = 放行
    int asks = 0;
    setQuitGuard([&asks] { ++asks; return false; });
    assert(!detail::shouldProceedExit());
    assert(!detail::shouldProceedExit());
    assert(asks == 2);  // 每次判定都问一次，不缓存
    setQuitGuard([&asks] { ++asks; return true; });
    assert(detail::shouldProceedExit());
    setQuitGuard(nullptr);  // 清除 → 放行
    assert(detail::shouldProceedExit());

    // ---- 4. guard × requestExit 组合（主循环判定等价式）----
    bool blocked = true;
    setQuitGuard([&blocked] { return !blocked; });
    requestExit();
    // guard 拦截：请求被消费，但裁决不放行 → 主循环继续
    assert(detail::consumeExitRequest() && !detail::shouldProceedExit());
    assert(!detail::consumeExitRequest());  // 请求不重放（再次请求才再问）
    blocked = false;
    requestExit();
    assert(detail::consumeExitRequest() && detail::shouldProceedExit());
    setQuitGuard(nullptr);
    requestExit();
    assert(detail::consumeExitRequest() && detail::shouldProceedExit());

    // ---- 5. 空句柄守卫（no-op 不崩溃；非空句柄真窗口路径归 fixture）----
    assert(!core::window::requestWindowFocus(nullptr));
    core::window::setWindowTitle(nullptr, "x");
    core::window::setWindowTitle(nullptr, nullptr);

    std::printf("window_lifecycle_api: all assertions passed\n");
    return 0;
}
