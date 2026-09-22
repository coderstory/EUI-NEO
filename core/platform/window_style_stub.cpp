// core/platform/window_style_stub.cpp
// macOS/Linux stub：桌宠设计 G5 的任务栏隐藏是 Windows 专属需求
//（macOS 子窗口本就不进 Dock；Linux 待桌面环境调研）。静默降级返回 false。
#include "core/platform/window_style.h"

namespace core::platform {

bool applyWindowStyleFlags(void* /*nativeWindowHandle*/, const WindowStyleFlags& /*flags*/) {
    return false;
}

} // namespace core::platform
