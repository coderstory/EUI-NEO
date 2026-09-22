#pragma once

#include "core/window/window_types.h"

#include <string>

namespace core::window {

Handle createWindow(const WindowCreateRequest& request);
void destroyWindow(Handle window);
NativeWindowInfo nativeWindowInfo(Handle window);
#if defined(EUI_WINDOW_BACKEND_SDL2)
// SDL2 desktop Linux only: returns Xft.dpi / 96 for an X11 window,
// or 0.0f when SDL selected another video backend.
float x11ContentScale(Handle window);
#endif

ContextKey currentContextKey();
double timeSeconds();
void postEmptyEvent();

void getCursorPosition(Handle window, double& x, double& y);
std::string clipboardText(Handle window);
void setClipboardText(const std::string& text);

CursorHandle createStandardCursor(CursorType type);
void setCursor(Handle window, CursorHandle cursor);
void destroyCursor(CursorHandle cursor);

void setWindowIcon(Handle window, int width, int height, unsigned char* pixels);
void setImeCursorRect(Handle window, float x, float y, float width, float height);

// ---- 桌宠/子窗口扩展（桌宠设计 §2.6 G3/G4）----
// 运行时移动/读取窗口位置（屏幕坐标，GLFW 语义两平台一致为左上原点）
void setWindowPos(Handle window, int x, int y);
void getWindowPos(Handle window, int& x, int& y);
// 请求关闭窗口（子窗口：主循环下一帧走正常 prune/destroy 路径；
// 主窗口：等同用户点关闭按钮——托盘模式下走隐藏到托盘语义）
void requestWindowClose(Handle window);
// 运行时切换鼠标穿透（整窗点击透到下层；仅无边框窗口有意义）
void setWindowMousePassthrough(Handle window, bool enabled);
// 主显示器工作区（不含任务栏/Dock），供应用把持久化位置钳制回屏幕内
void getPrimaryMonitorWorkArea(int& x, int& y, int& width, int& height);
void installInputCallbacks(Handle window);
void uninstallInputCallbacks(Handle window);
bool queryImeComposition(Handle window, std::string& text, bool& composing);

} // namespace core::window
