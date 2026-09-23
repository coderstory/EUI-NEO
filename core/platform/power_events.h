#pragma once
// core/platform/power_events.h
// 系统电源事件（睡眠/唤醒）桥：宿主注册进程级回调，Windows 在主窗口
// WndProc 子类链上截获 WM_POWERBROADCAST 后转发（回调发生在消息泵线程，
// 即主线程，可直接改窗口/运行时状态）。非 Windows 平台走 stub——回调
// 永不触发，注册与安装均为安全空操作。
// 设计来源：DevDesk 桌宠 M2「Sleep 零渲染」（系统挂起时停掉渲染循环，
// 唤醒后恢复），纯增量、向后兼容，不注册回调 = 行为与旧版完全一致。

#include <functional>

namespace core::platform {

// 系统电源事件：
//   Suspending = 即将睡眠/休眠（Win32 PBT_APMSUSPEND，发生在系统挂起之前）
//   Resuming   = 已唤醒回到桌面（PBT_APMRESUMEAUTOMATIC / PBT_APMRESUMESUSPEND /
//                PBT_APMRESUMECRITICAL 三者统一上抛，宿主按「恢复渲染」处理）
enum class SystemPowerEvent {
    Suspending,
    Resuming
};

// 注册系统电源事件回调（进程级单实例；传空 std::function = 注销）。
// 需在主窗口创建前或主循环线程调用；回调在 Win32 消息泵线程触发。
void setSystemPowerHandler(std::function<void(SystemPowerEvent)> handler);

// 平台内部：把 hook 挂到指定原生窗口（HWND）的 WndProc 上，将广播的
// WM_POWERBROADCAST 转发给已注册回调。进程级幂等（首个有效句柄生效，
// 后续调用忽略）；nativeWindowHandle 为 null（非 Windows / 无原生句柄）
// 时什么都不做。由后端主循环在主窗口创建后调用，宿主无需直接触碰。
void installSystemPowerNotifications(void* nativeWindowHandle);

// 摘除 hook（主循环退出路径调用；未安装时为空操作）。若本 hook 之上又有
// 其他子类链（如 IME 桥），为不撕断链只做「停止上抛」而非恢复指针——
// hook 过程体留在链上透传，直至窗口销毁（进程退出即回收）。
void uninstallSystemPowerNotifications();

} // namespace core::platform
