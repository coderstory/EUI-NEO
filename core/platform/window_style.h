// core/platform/window_style.h
// 平台窗口样式属性（任务栏/Dock 层面）的平台无关接口。
// 设计来源：docs/superpowers/specs/2026-09-22-desktop-pet-design.md §2.6 G5
//（DevDesk 桌宠需要：子窗口不进任务栏/Alt+Tab）。对齐 window_effect 先例：
// 头文件平台无关，实现按平台拆文件，macOS/Linux 走 stub。
#pragma once

namespace core::platform {

// 扩展窗口样式位（按需增成员；全部 false = 恢复默认样式）。
struct WindowStyleFlags {
    // Windows: WS_EX_TOOLWINDOW——任务栏和 Alt+Tab 均不出现。
    // macOS: 子窗口本就不进 Dock（无需处理），stub 返回 false。
    bool toolWindow = false;
    // Windows: WS_EX_NOACTIVATE——点击不成为前台窗口（桌宠可选补强，
    // 与 GLFW_FOCUS_ON_SHOW=0 叠加）。macOS 待 NSNonactivatingPanel 调研。
    bool noActivate = false;
};

// 把窗口样式位应用到平台原生窗口句柄（Windows: HWND）。幂等、可运行时反复
// 调用（按 flags 增/清位后触发 SWP_FRAMECHANGED 让非客户区立即更新）；
// 返回是否实际生效（非 Windows 平台/句柄无效 → false，静默降级不抛不刷日志）。
// 注意（Windows）：toolWindow 隐含清掉 WS_EX_APPWINDOW（GLFW 创建窗口自带、
// 强制进任务栏，与 TOOLWINDOW 互斥），恢复 false 时补回 APPWINDOW。
bool applyWindowStyleFlags(void* nativeWindowHandle, const WindowStyleFlags& flags);

} // namespace core::platform
