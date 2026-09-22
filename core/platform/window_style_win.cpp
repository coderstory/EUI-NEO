// core/platform/window_style_win.cpp
// Windows 实现：GWL_EXSTYLE 增/清 WS_EX_TOOLWINDOW / WS_EX_NOACTIVATE +
// SetWindowPos(SWP_FRAMECHANGED) 让样式立即生效（不重画客户区）。
// 设计来源：桌宠设计文档 §2.6 G5（2026-09-22-desktop-pet-design.md）。
#include "core/platform/window_style.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace core::platform {

bool applyWindowStyleFlags(void* nativeWindowHandle, const WindowStyleFlags& flags) {
    if (nativeWindowHandle == nullptr) {
        return false;
    }
    HWND hwnd = static_cast<HWND>(nativeWindowHandle);

    const LONG_PTR extra = ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (extra == 0 && ::GetLastError() != 0) {
        return false;
    }
    LONG_PTR updated = extra;
    if (flags.toolWindow) {
        updated |= WS_EX_TOOLWINDOW;
    } else {
        updated &= ~static_cast<LONG_PTR>(WS_EX_TOOLWINDOW);
    }
    if (flags.noActivate) {
        updated |= WS_EX_NOACTIVATE;
    } else {
        updated &= ~static_cast<LONG_PTR>(WS_EX_NOACTIVATE);
    }
    if (updated != extra &&
        ::SetWindowLongPtrW(hwnd, GWL_EXSTYLE, updated) == 0 && ::GetLastError() != 0) {
        return false;
    }
    // 样式变更后让窗口按新扩展样式重算边框（不动位置/尺寸/Z 序/激活态）
    ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return true;
}

} // namespace core::platform
