#pragma once

#include "eui/dsl.h"
#include "eui/types.h"
#include "eui/window.h"
#include "core/platform/platform.h"
#include "core/platform/window_effect.h"

#include <functional>
#include <string>
#include <vector>

namespace app {

using DslWindowCompose = std::function<void(eui::Ui&, const eui::Screen&)>;

struct DslWindowRequest {
    std::string title = "Window";
    std::string pageId = "window";
    eui::Color clearColor = {0.16f, 0.18f, 0.20f, 1.0f};
    int width = 640;
    int height = 420;
    bool modal = false;
    std::function<void(const eui::KeyEvent&)> onKeyEvent;
    DslWindowCompose compose;
};

const char* windowTitle();
bool showDebugStatsInTitle();
double debugTitleUpdateInterval();
bool showDebugOverlay();
double frameRateLimit();
int initialWindowWidth();
int initialWindowHeight();
int initialWindowX();
int initialWindowY();
bool initialWindowPositionSet();
/** @brief 窗口效果启动快照（DslAppConfig::windowEffect）。 */
core::platform::WindowEffect windowEffect();
int minimumWindowWidth();
int minimumWindowHeight();
int maximumWindowWidth();
int maximumWindowHeight();
bool windowResizable();
bool windowHighDpi();
bool windowDecorated();
bool windowAlwaysOnTop();
bool windowMaximized();
float uiScale();
bool trayEnabled();
const char* trayTitle();
const char* trayIconPath();
/** @brief 自定义托盘菜单项（DslAppConfig::trayMenu 注册），可能为空。 */
const std::vector<core::platform::TrayMenuItem>& trayMenuItems();
/** @brief 是否在内置 Show/Exit 之外保留默认项（见 DslAppConfig::trayMenu）。 */
bool trayKeepDefaultMenuItems();
void requestUpdate();
/**
 * @brief 运行时切换标题栏（非客户区）外观，即时生效，无需重启。
 *
 * 覆盖主窗口 + 全部存活子窗口，此后新开的子窗口同样跟随；优先于
 * DslAppConfig::darkTitleBar 的启动快照。仅 Windows 生效（其他平台静默
 * 忽略）。可在任意回调里调用（主线程），实际应用发生在下一帧主循环。
 */
void setTitleBarAppearance(const core::platform::TitleBarAppearance& appearance);
/** @brief 当前生效的标题栏外观：运行时覆盖优先，否则取 dslAppConfig 启动快照。 */
core::platform::TitleBarAppearance currentTitleBarAppearance();
/**
 * @brief 运行时覆盖 clearColor（主窗口 + 全部存活子窗口），下一帧生效。
 *
 * 优先于 DslAppConfig / DslWindowConfig 的启动快照；此后新开的子窗口同样
 * 跟随。传 alpha=1 即回到不透明视觉。内部触发 requestFullPaint，可在任意
 * 回调里调用（主线程）。根治 DevDesk KNOWN_LIMITATIONS UI-2（主题切换底色
 * 不跟随，需重启）。
 */
void setClearColor(const eui::Color& color);
/** @brief 当前生效的 clearColor：运行时覆盖优先，否则取 dslAppConfig 启动快照。 */
eui::Color currentClearColor();
bool initialize(eui::window::Handle window);
bool update(eui::window::Handle window, float deltaSeconds, int windowWidth, int windowHeight, float dpiScale, float pointerScale);
bool update(eui::window::Handle window, float deltaSeconds, int windowWidth, int windowHeight, float dpiScale, float pointerScale, bool updateRequested);
bool update(eui::window::Handle window, float deltaSeconds, int windowWidth, int windowHeight, float dpiScale, float pointerScale, bool updateRequested, bool inputEnabled);
bool isAnimating();
void render(int windowWidth, int windowHeight, float dpiScale);
void releaseGraphicsResources();
void shutdown();
std::vector<DslWindowRequest> consumeWindowRequests();

namespace detail {
void requestFullPaint();
}

} // namespace app
