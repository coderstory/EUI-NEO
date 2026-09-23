// core/platform/power_events_stub.cpp
// macOS/Linux stub：系统电源事件通知当前只有 Windows（WM_POWERBROADCAST）
// 有实现需求（DevDesk 桌宠 M2）。macOS 对应 IOKit 通知、Linux 对应
// logind/DBus，待有真实宿主需求再补；现在注册与安装均为安全空操作——
// 宿主侧「回调永不触发」等价于功能不启用，编译与链接零差异。
#include "core/platform/power_events.h"

#include <utility>

namespace core::platform {

void setSystemPowerHandler(std::function<void(SystemPowerEvent)> handler) {
    // 存储但永不触发：保持与 Windows 路径相同的调用安全性
    (void)handler;
}

void installSystemPowerNotifications(void* /*nativeWindowHandle*/) {}

void uninstallSystemPowerNotifications() {}

} // namespace core::platform
