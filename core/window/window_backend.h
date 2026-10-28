#pragma once

#include "core/window/window_types.h"

#include <functional>
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
// U3（2026-09-28 DevDesk UI 现代化）：运行期改窗客户区尺寸（物理像素，
// 同 createWindow 的 request.width/height 口径）。styled 菜单组头折叠/
// 展开时按实际行数即时回收/补高窗高（此前窗口尺寸创建期定死）。
void setWindowSize(Handle window, int width, int height);
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
/**
 * @brief 显示器「逻辑单位 → 窗口（物理像素）单位」换算系数：包含屏幕点
 *        (x, y)（物理像素，同 getMonitorWorkAreas 坐标系）的显示器 DPI / 96。
 *
 * 为什么需要：窗口创建尺寸直达后端 glfwCreateWindow / SDL_CreateWindow 的
 * **客户区物理像素**，而 compose/render 侧按 framebuffer / (dpiScale ×
 * uiScale) 折算逻辑坐标空间——两侧口径不同。把逻辑单位量出来的尺寸（如
 * styledMenu 面板高：行高/inset/margin 全按逻辑单位定义）直接当窗尺寸传，
 * 高 DPI 显示器上窗会比内容小一个缩放比（125% 下菜单末行文字溢出到窗底
 * 之外、长菜单项被截）。乘本系数后两侧口径一致。
 *
 * Windows：GetDpiForMonitor（shcore.dll，运行期解析符号，不引链接期依赖）；
 * 查询失败（无显示器 / 系统不支持）返回 1.0f，按 1:1 降级。
 * 其他平台恒 1.0f——macOS 窗口单位是点（content scale=2 时乘会双倍放大）、
 * X11 无独立物理/逻辑分层，均不该换算（保持既有行为）。
 *
 * 纯查表，不依赖窗口/GL 初始化，可在无头环境调用（供单测）。
 */
float windowScaleForPoint(float x, float y);

// Reports whether the created framebuffer actually carries alpha (GLFW
// honors the transparent request; SDL2 and compositor-less X11 do not).
bool framebufferTransparent(Handle window);
// Hand an interactive move/resize to the window manager (EWMH
// _NET_WM_MOVERESIZE on X11). Per-frame programmatic moves arrive as
// ConfigureRequests and can be clamped back into the workarea, so undecorated
// windows should delegate title-bar drags this way. Returns false when the
// current platform/backend cannot delegate; callers keep their own fallback.
bool beginWindowMove(Handle window);
bool beginWindowResize(Handle window, WindowResizeEdge edge);
void installInputCallbacks(Handle window);
void uninstallInputCallbacks(Handle window);
bool queryImeComposition(Handle window, std::string& text, bool& composing);

// ---- 自绘窗口壳（undecorated 主窗口的自绘标题栏；DevDesk UAT3 D-R3-1）----
// 与 decorated=false 配套：去掉系统标题栏后，拖拽/双击最大化/边缘 resize 在
// Windows 由本层 WndProc 子类链接管，宿主只声明「哪块区域算标题栏拖拽区」。
// macOS 保持系统标题栏（GLFW borderless 无红绿灯/不可 resize/不可拖拽，
// customWindowChromeSupported() 恒 false，宿主据此不请求 undecorated）。

// 本平台是否支持自绘窗口壳。纯平台查询（无窗口/GL 依赖，可在任何时刻调用）：
// Windows（GLFW）= true；macOS / Linux / SDL2 = false。
bool customWindowChromeSupported();

// 窗口控制三键的运行时动作（自绘标题栏按钮消费；等价系统标题栏语义，两后端均可用）。
// restoreWindow 同时承担「还原最大化」与「解除最小化」两个语义（GLFW/SDL 同构）。
void maximizeWindow(Handle window);
void restoreWindow(Handle window);
void iconifyWindow(Handle window);
// 窗口当前是否最大化（自绘按钮切换 maximize/还原图标用；handle 空恒 false）。
bool windowIsMaximized(Handle window);

// 标题栏命中回调：坐标/尺寸均为客户区**物理像素**，原点=窗口左上；返回 true =
// 该点属于标题栏拖拽区。仅 Windows 实装——WM_NCHITTEST 按回调返回 HTCAPTION
// 后系统原生接管拖拽与 Aero Snap；WM_NCLBUTTONDOWN 掐表判双击切最大化/还原
//（GLFW 窗口类无 CS_DBLCLKS，系统不会派发 NC 双击消息）；八向边缘 resize
// 热区 + 方向光标由本层按系统度量自理，**热区命中优先于回调**（标题栏贴边处
// 应 resize 而非拖拽），仅在 resizable 且未最大化时生效。回调在主线程触发，
// 必须快速返回（每次鼠标移动都会进来）。resizable=false 时八向热区不生效。
// handler 为空函数 = 卸载；对未创建/有边框窗口调用为安全无操作。
using CaptionHitTestHandler = std::function<bool(int x, int y, int width, int height, bool maximized)>;
void setWindowCaptionHitTestHandler(Handle window, CaptionHitTestHandler handler, bool resizable);

// 模态拖拽循环的活渲染回调（真机反馈「窗体缩放很卡」修复）：SC_SIZE/SC_MOVE
// 模态循环阻塞在事件泵的消息派发里（glfwWaitEvents 不返回），主渲染循环回不
// 了环——期间零帧渲染、DWM 只能拉伸上一帧（卡顿/模糊根因）。壳子类在
// WM_ENTERSIZEMOVE..WM_EXITSIZEMOVE 区间内的每个 WM_SIZE/WM_MOVE（几何已
// 落定后）重入调用本回调渲染一帧；区间外（程序化 SetWindowPos）不触发，主循
// 环既有渲染路径零行为变化。回调在主线程 WndProc 内触发，必须快速返回（禁
// 等待/事件泵/长任务）。须在 setWindowCaptionHitTestHandler 之后注册（仅自绘
// 壳窗口存在模态拖拽循环，其余注册为无操作）；空函数 = 注销。非 Windows 平
// 台无该模态循环问题，恒无操作。
using LiveRenderHandler = std::function<void()>;
void setWindowLiveRenderHandler(Handle window, LiveRenderHandler handler);

} // namespace core::window
