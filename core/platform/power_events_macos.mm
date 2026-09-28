// core/platform/power_events_macos.mm —— macOS 系统电源事件
//（2026-09-28 macos-three-features 设计 §6.3）
// NSWorkspace 通知（WillSleep / DidWake），不依赖窗口句柄（参数忽略）；
// 枚举映射对齐 Windows WM_POWERBROADCAST 语义。IOKit 电池/低电量不做（设计 §9）。
#import <Cocoa/Cocoa.h>

#include <functional>

#include "power_events.h"

namespace core::platform {

namespace {

std::function<void(SystemPowerEvent)> g_handler;

id g_sleep_observer = nil;
id g_wake_observer = nil;

} // namespace

void setSystemPowerHandler(std::function<void(SystemPowerEvent)> handler) {
    g_handler = std::move(handler);
}

void installSystemPowerNotifications(void* /*nativeWindowHandle*/) {
    @autoreleasepool {
        if (g_sleep_observer != nil) {
            return;   // 进程级幂等（对齐 win 语义：首个安装生效）
        }
        NSNotificationCenter* center =
            [[NSWorkspace sharedWorkspace] notificationCenter];
        g_sleep_observer =
            [center addObserverForName:NSWorkspaceWillSleepNotification
                                object:nil queue:nil
                            usingBlock:^(NSNotification*) {
                                if (g_handler) {
                                    g_handler(SystemPowerEvent::Suspending);
                                }
                            }];
        g_wake_observer =
            [center addObserverForName:NSWorkspaceDidWakeNotification
                                object:nil queue:nil
                            usingBlock:^(NSNotification*) {
                                if (g_handler) {
                                    g_handler(SystemPowerEvent::Resuming);
                                }
                            }];
    }
}

void uninstallSystemPowerNotifications() {
    @autoreleasepool {
        NSNotificationCenter* center =
            [[NSWorkspace sharedWorkspace] notificationCenter];
        if (g_sleep_observer != nil) {
            [center removeObserver:g_sleep_observer];
            g_sleep_observer = nil;
        }
        if (g_wake_observer != nil) {
            [center removeObserver:g_wake_observer];
            g_wake_observer = nil;
        }
    }
}

} // namespace core::platform