// core/platform/window_style_macos.mm —— macOS 窗口样式
//（2026-09-28 macos-three-features 设计 §6.1：hideFromTaskbar 等价）
// macOS 窗口本就不进 Dock（无「任务栏」概念）；「任务栏隐藏」的等价语义 =
// 常驻全部 Space + 不参与 ⌘Tab 循环（mach 桌宠在换 Space / 应用循环时不可见）。
#import <Cocoa/Cocoa.h>

#include "window_style.h"

namespace core::platform {

bool applyWindowStyleFlags(void* nativeHandle, const WindowStyleFlags& flags) {
    if (nativeHandle == nullptr) {
        return false;
    }
    // 入参句柄在 __APPLE__ 下已由上游解析为 NSWindow*（glfw_app_main.cpp
    // nativeWindowHandle / nativeWindowInfo().platformWindow），与
    // window_effect_macos.mm 同一约定——禁止再按 GLFWwindow* 二次
    // glfwGetCocoaWindow（会把 NSWindow 内存按 GLFW 窗口结构解引用）。
    NSWindow* nsWindow = (__bridge NSWindow*)nativeHandle;
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