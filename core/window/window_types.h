#pragma once

#include "core/platform/window_effect.h"

namespace core::window {

using Handle = void*;
using ContextKey = void*;
using CursorHandle = void*;

enum class CursorType {
    Arrow,
    Hand
};

enum class RenderApi {
    OpenGL,
    Vulkan
};

struct WindowCreateRequest {
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    bool positionSet = false;
    int minWidth = 0;
    int minHeight = 0;
    int maxWidth = 0;
    int maxHeight = 0;
    const char* title = "";
    bool resizable = true;
    bool highDpi = true;
    bool decorated = true;
    bool alwaysOnTop = false;
    bool maximized = false;
    bool modal = false;
    Handle parent = nullptr;
    RenderApi renderApi = RenderApi::OpenGL;
    // 窗口效果（磨砂设计文档 Phase B）：非 None 时窗口以透明帧缓冲创建
    //（GLFW_TRANSPARENT_FRAMEBUFFER，创建期 hint，不可运行时关闭；「任何↔关」
    // 切换靠 clearColor alpha=1 视觉等价）。Vulkan 后端在 Windows 上
    // compositeAlpha 普遍只报 OPAQUE，SDL2 无逐像素透明 flag——两者对本
    // 字段降级 None。
    platform::WindowEffect windowEffect = platform::WindowEffect::None;
    // 显示时不抢前台焦点（桌宠设计 §2.6 G1；GLFW_FOCUS_ON_SHOW 创建期 hint，
    // SDL2 后端无对应能力、忽略）
    bool focusOnShow = true;
    // 鼠标穿透：整窗点击透到下层窗口（G4；GLFW_MOUSE_PASSTHROUGH——GLFW 3.4
    // 仅无边框窗口生效，Win32 即 WS_EX_TRANSPARENT；SDL2 2.26+ 运行期设置）
    bool mousePassthrough = false;
    // 逐像素透明帧缓冲，与 windowEffect（磨砂档位）解耦：sprite 类覆盖窗
    //（桌宠，clearColor alpha=0）自身要透明，不应随全局效果档位（如应用
    // setWindowEffect(None)）拿不到 GLFW_TRANSPARENT_FRAMEBUFFER 而黑底。
    // Vulkan compositeAlpha / SDL2 无逐像素透明——同 windowEffect 降级。
    bool transparentFramebuffer = false;
};

struct NativeWindowInfo {
    Handle handle = nullptr;
    void* platformWindow = nullptr;
    void* platformDisplay = nullptr;
    void* platformView = nullptr;
};

} // namespace core::window
