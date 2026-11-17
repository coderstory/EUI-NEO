// tests/fixtures/window_lifecycle_viewer.cpp
// need272 子窗口生命周期能力的真机验证 fixture（截图 + stderr 取证）：
//   ① requestWindowFocus（含最小化态解除 + 抢前台）
//   ② setWindowTitle（运行时改标题，含标题回查）
//   ③ DslWindowConfig::onCloseRequested（先 VETO 后 ALLOW）
//   ④ app::setQuitGuard（拦一次退出，再次请求放行真退出）
// 时间轴脚本由主窗口 compose 驱动（子窗最小化期间自身 compose 冻结，
// 不能自驱）；现象全部打 stderr [lifecycle] 行。总时长约 8s 后自行退出。
#include "eui_neo.h"

#if defined(EUI_WINDOW_BACKEND_SDL2)
#include <SDL.h>
#else
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace app {
namespace {

// 跨线程事件泵：空闲主循环挂 glfwWaitEvents，单线程在 compose 里自
// requestUpdate 会被同帧末尾的 pollEvents 吃掉事件、下一帧 wait 永久挂起
//（首轮真机实证）。独立线程每 100ms 补一发事件，保证脚本时间轴推进。
// 15s 自熄（fixture ~11s 退出；退出后残余 post 被 GLFW 未初始化守卫吞掉）。
std::atomic<bool> g_pumpRunning{false};

void startPumpThread() {
    if (g_pumpRunning.exchange(true)) {
        return;
    }
    std::thread([] {
        for (int i = 0; i < 150; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            requestUpdate();
        }
        g_pumpRunning = false;
    }).detach();
}

struct LifecycleScript {
    core::window::Handle child = nullptr;
    double phaseStart = 0.0;
    int phase = 0;
    bool childOpened = false;
    bool allowClose = false;  // onCloseRequested 裁决值：先 VETO 后 ALLOW
    int closeVerdicts = 0;
    int focusEvents = 0;
    int destroyEvents = 0;

    static void log(const char* line) {
        std::fprintf(stderr, "[lifecycle] %s\n", line);
        std::fflush(stderr);
    }

    static double now() { return core::window::timeSeconds(); }

    bool phaseDue(double wait) {
        if (phaseStart == 0.0) {
            phaseStart = now();
        }
        return now() - phaseStart >= wait;
    }

    void nextPhase() {
        phaseStart = now();
        ++phase;
    }
};

LifecycleScript g_script;

} // namespace

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("Lifecycle Main")
        .pageId("window_lifecycle_viewer")
        .windowSize(720, 480)
        .showDebugStatsInTitle(false)
        .fps(0.0)
        .iconPath("");
    return config;
}

void compose(eui::Ui& ui, const eui::Screen& screen) {
    (void)ui;
    (void)screen;
    startPumpThread();
    auto& s = g_script;

    if (!s.childOpened && s.phaseDue(0.5)) {
        s.childOpened = true;
        s.log("phase0: opening child window");
        openWindow(DslWindowConfig{}
                       .title("Lifecycle Child")
                       .windowSize(520, 360)
                       .onWindowCreated([&s](core::window::Handle h) {
                           s.child = h;
                           s.log("phase0: child created");
                       })
                       .onWindowFocusChanged([&s](core::window::Handle, bool focused) {
                           ++s.focusEvents;
                           s.log(focused ? "child focus GAINED" : "child focus LOST");
                       })
                       .onWindowDestroyed([&s](core::window::Handle) {
                           ++s.destroyEvents;
                           s.child = nullptr;
                           s.log("child destroyed (onWindowDestroyed)");
                       })
                       .onCloseRequested([&s](core::window::Handle) {
                           ++s.closeVerdicts;
                           const bool allow = s.allowClose;
                           s.log(allow ? "close verdict -> ALLOW" : "close verdict -> VETO");
                           return allow;
                       }),
                   [](eui::Ui&, const eui::Screen&) {});
        s.nextPhase();
        return;
    }

    if (s.phase == 1 && s.phaseDue(1.5)) {
#if defined(EUI_WINDOW_BACKEND_SDL2)
        core::window::setWindowTitle(s.child, "Renamed-By-Runtime");
        s.log("phase1: setWindowTitle -> Renamed-By-Runtime");
#else
        const char* before = s.child
            ? glfwGetWindowTitle(static_cast<GLFWwindow*>(s.child)) : "(null)";
        core::window::setWindowTitle(s.child, "Renamed-By-Runtime");
        const char* after = s.child
            ? glfwGetWindowTitle(static_cast<GLFWwindow*>(s.child)) : "(null)";
        std::fprintf(stderr, "[lifecycle] phase1: setWindowTitle before='%s' after='%s'\n",
                     before ? before : "(null)", after ? after : "(null)");
        std::fflush(stderr);
#endif
        s.nextPhase();
        return;
    }

    if (s.phase == 2 && s.phaseDue(1.5)) {
        s.log("phase2: iconify child (minimize)");
        core::window::iconifyWindow(s.child);
        s.nextPhase();
        return;
    }

    if (s.phase == 3 && s.phaseDue(1.5)) {
        const bool ok = core::window::requestWindowFocus(s.child);
        std::fprintf(stderr, "[lifecycle] phase3: requestWindowFocus on minimized child -> %d\n",
                     ok ? 1 : 0);
        std::fflush(stderr);
        s.nextPhase();
        return;
    }

    if (s.phase == 4 && s.phaseDue(1.0)) {
#if !defined(EUI_WINDOW_BACKEND_SDL2)
        if (s.child) {
            const bool focused =
                glfwGetWindowAttrib(static_cast<GLFWwindow*>(s.child), GLFW_FOCUSED) == GLFW_TRUE;
            const bool iconified =
                glfwGetWindowAttrib(static_cast<GLFWwindow*>(s.child), GLFW_ICONIFIED) == GLFW_TRUE;
            std::fprintf(stderr, "[lifecycle] phase4: child focused=%d iconified=%d focusEvents=%d\n",
                         focused ? 1 : 0, iconified ? 1 : 0, s.focusEvents);
            std::fflush(stderr);
        }
#endif
        s.log("phase4: requestWindowClose (expect VETO, window kept)");
        core::window::requestWindowClose(s.child);
        s.nextPhase();
        return;
    }

    if (s.phase == 5 && s.phaseDue(1.0)) {
#if !defined(EUI_WINDOW_BACKEND_SDL2)
        if (s.child) {
            const bool shouldClose =
                glfwWindowShouldClose(static_cast<GLFWwindow*>(s.child)) == GLFW_TRUE;
            std::fprintf(stderr, "[lifecycle] phase5: child alive=%d shouldClose=%d (expect 1/0)\n",
                         s.child ? 1 : 0, shouldClose ? 1 : 0);
            std::fflush(stderr);
        }
#endif
        s.allowClose = true;
        s.log("phase5: requestWindowClose again (expect ALLOW -> destroy)");
        core::window::requestWindowClose(s.child);
        s.nextPhase();
        return;
    }

    if (s.phase == 6 && s.phaseDue(1.0)) {
        s.log("phase6: setQuitGuard(block once) + requestExit #1 (expect app keeps running)");
        setQuitGuard([] {
            static int asks = 0;
            ++asks;
            std::fprintf(stderr, "[lifecycle] guard asked #%d -> %s\n", asks,
                         asks >= 2 ? "ALLOW" : "BLOCK");
            std::fflush(stderr);
            return asks >= 2;
        });
        requestExit();
        s.nextPhase();
        return;
    }

    if (s.phase == 7 && s.phaseDue(1.5)) {
        s.log("phase7: requestExit #2 (guard allows -> real exit)");
        requestExit();
        s.nextPhase();
        return;
    }
    // phase >= 8：等主循环退出（guard 放行后进程结束）
}

} // namespace app
