// core/platform/power_events_win.cpp
// Windows 实现：SetWindowLongPtrW(GWLP_WNDPROC) 子类链截获 WM_POWERBROADCAST
//（与 ime_bridge.c 的 IME 消息过滤同一模式：保存前级 proc、处理后
// CallWindowProcW 透传，不改变消息流向）。GLFW 的 windowProc 本身不处理
// WM_POWERBROADCAST（直通 DefWindowProc），挂链截获零副作用。
// PBT_APMSUSPEND → Suspending；PBT_APMRESUMEAUTOMATIC（快速唤醒，先到、
// 必达）与 PBT_APMRESUMESUSPEND（完整恢复）/ PBT_APMRESUMECRITICAL 统一
// → Resuming。查询类（PBT_APMQUERYSUSPEND 等）一律透传，由 DefWindowProc
// 给出系统期望的应答值，不参与否决链。
#include "core/platform/power_events.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <utility>

namespace core::platform {
namespace {

std::function<void(SystemPowerEvent)>& powerHandler() {
    static std::function<void(SystemPowerEvent)> instance;
    return instance;
}

HWND g_hookedHwnd = nullptr;
WNDPROC g_previousProc = nullptr;
bool g_notificationsActive = false;

LRESULT CALLBACK powerEventsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_POWERBROADCAST && g_notificationsActive) {
        if (wParam == PBT_APMSUSPEND) {
            if (powerHandler()) {
                powerHandler()(SystemPowerEvent::Suspending);
            }
        } else if (wParam == PBT_APMRESUMEAUTOMATIC ||
                   wParam == PBT_APMRESUMESUSPEND ||
                   wParam == PBT_APMRESUMECRITICAL) {
            if (powerHandler()) {
                powerHandler()(SystemPowerEvent::Resuming);
            }
        }
        // 其余（POWERSTATUSCHANGE / QUERY* 等）不上抛，直接透传
    }
    if (g_previousProc != nullptr) {
        return CallWindowProcW(g_previousProc, hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

void setSystemPowerHandler(std::function<void(SystemPowerEvent)> handler) {
    powerHandler() = std::move(handler);
}

void installSystemPowerNotifications(void* nativeWindowHandle) {
    // 进程级幂等：已挂（或曾尝试挂）不再重复；null 句柄（无窗口后端）不动作
    if (g_hookedHwnd != nullptr || nativeWindowHandle == nullptr) {
        return;
    }
    HWND hwnd = static_cast<HWND>(nativeWindowHandle);
    const LONG_PTR current = GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
    if (current == 0) {
        return;  // 取不到窗口过程（非窗口句柄等）：放弃安装，不影响任何既有行为
    }
    g_previousProc = reinterpret_cast<WNDPROC>(current);
    const LONG_PTR applied =
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&powerEventsProc));
    if (applied == 0) {
        g_previousProc = nullptr;
        return;
    }
    g_hookedHwnd = hwnd;
    g_notificationsActive = true;
}

void uninstallSystemPowerNotifications() {
    if (g_hookedHwnd == nullptr) {
        powerHandler() = nullptr;
        return;
    }
    // 停止上抛（即便指针未能恢复，proc 体也只剩透传）
    g_notificationsActive = false;
    const LONG_PTR current = GetWindowLongPtrW(g_hookedHwnd, GWLP_WNDPROC);
    if (current == reinterpret_cast<LONG_PTR>(&powerEventsProc)) {
        // 链顶仍是我们 → 干净恢复前级
        SetWindowLongPtrW(g_hookedHwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(g_previousProc));
    }
    // 否则说明后装的上游子类（如 IME 桥）压在我们上面：恢复会把它们一起
    // 摘掉（撕链），不如让透传 proc 随窗口销毁自然回收
    g_hookedHwnd = nullptr;
    g_previousProc = nullptr;
    powerHandler() = nullptr;
}

} // namespace core::platform
