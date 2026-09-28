// core/platform/window_style_macos.mm —— macOS 窗口样式
//（2026-09-28 macos-three-features 设计 §6.1：hideFromTaskbar 等价）
// macOS 窗口本就不进 Dock（无「任务栏」概念）；「任务栏隐藏」的等价语义 =
// 常驻全部 Space + 不参与 ⌘Tab 循环（mach 桌宠在换 Space / 应用循环时不可见）。
#import <Cocoa/Cocoa.h>
#include <GLFW/glfw3native.h>

#include "window_style.h"

namespace core::platform {

bool applyWindowStyleFlags(void* nativeHandle, const WindowStyleFlags& flags) {
    if (nativeHandle == nullptr) {
        return false;
    }
    NSWindow* nsWindow = glfwGetCocoaWindow(static_cast<GLFWwindow*>(nativeHandle));
    if (nsWindow == nil) {
        return false;
    }
    if (flags.toolWindow) {
        // 桌宠设计 G5（DevDesk pet_window.cpp:678 .hideFromTaskbar(true) → toolWindow）
        // ⚠️ 该组合是否满足「不进 Dock / ⌘Tab 不可见」预期待 Mac 实机复核
        //（设计 §13-4）
        [nsWindow setCollectionBehavior:
            (NSWindowCollectionBehaviorCanJoinAllSpaces |
             NSWindowCollectionBehaviorIgnoresCycle)];
    } else {
        [nsWindow setCollectionBehavior:NSWindowCollectionBehaviorDefault];
    }
    return true;
}

} // namespace core::platform