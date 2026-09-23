#pragma once

#include "core/window/window_types.h"

#include <string>
#include <vector>

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
/**
 * @brief 窗口当前是否持有系统输入焦点（styledMenu 等弹出窗失焦自关需要）。
 *
 * Windows（GLFW/SDL2 两后端）：GetForegroundWindow() 与窗口 HWND 比对，
 * 同步查询、无回调依赖。非 Windows 平台暂无统一查询，恒返回 true——
 * 调用方按"永不失焦"降级（弹出窗只靠选中/Esc/再次打开关闭）。
 * handle 为空返回 false（窗口已销毁语义）。
 */
bool windowIsFocused(Handle window);
// 运行时切换鼠标穿透（整窗点击透到下层；仅无边框窗口有意义）
void setWindowMousePassthrough(Handle window, bool enabled);
// 主显示器工作区（不含任务栏/Dock），供应用把持久化位置钳制回屏幕内
void getPrimaryMonitorWorkArea(int& x, int& y, int& width, int& height);
// 单个显示器的工作区矩形（屏幕坐标，与 getPrimaryMonitorWorkArea 同一坐标系）
struct MonitorWorkArea {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};
// 枚举全部显示器的工作区（不含任务栏/Dock），主显示器必在结果内。
// 查询失败（无头 / 未初始化 / 无显示器）返回空 vector——调用方按「无工作区
// 信息」原样处理，语义与 getPrimaryMonitorWorkArea 失败返回 0 宽高一致。
// 供宿主按位置选对应显示器钳制（多显示器副屏坐标可能为负/超出主屏边界，
// 只按主屏钳会吞掉副屏位置——DevDesk 桌宠 M2 需求）
std::vector<MonitorWorkArea> getMonitorWorkAreas();
void installInputCallbacks(Handle window);
void uninstallInputCallbacks(Handle window);
bool queryImeComposition(Handle window, std::string& text, bool& composing);

} // namespace core::window
