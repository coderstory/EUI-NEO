#pragma once

#include "eui/app.h"
#include "eui/async.h"
#include "eui/platform.h"

#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace app {

struct DslAppConfig {
    std::string titleValue = "App";
    std::string pageIdValue = "app";
    eui::Color clearColorValue = {0.16f, 0.18f, 0.20f, 1.0f};
    int windowWidthValue = 800;
    int windowHeightValue = 600;
    int windowXValue = 0;
    int windowYValue = 0;
    bool windowPositionSetValue = false;
    int minWindowWidthValue = 0;
    int minWindowHeightValue = 0;
    int maxWindowWidthValue = 0;
    int maxWindowHeightValue = 0;
    bool resizableValue = true;
    bool highDpiValue = true;
    bool decoratedValue = true;
    bool alwaysOnTopValue = false;
    bool maximizedValue = false;
    bool darkTitleBarValue = false;
    core::platform::WindowEffect windowEffectValue = core::platform::WindowEffect::None;
    float uiScaleValue = 1.0f;
#if defined(EUI_DEBUG_BUILD)
    bool showDebugStatsInTitleValue = true;
    bool showDebugOverlayValue = true;
#else
    bool showDebugStatsInTitleValue = false;
    bool showDebugOverlayValue = false;
#endif
    double debugTitleIntervalValue = 1.0;
    std::function<void(eui::Ui&, const eui::Screen&)> debugOverlayCompose;
    double fpsValue = 90.0;
    std::string iconPathValue = "assets/icon.png";
    std::string textFontFileValue;
    std::string iconFontFileValue;
    bool trayEnabledValue = false;
    std::string trayTitleValue;
    std::string trayIconPathValue;
    std::vector<core::platform::TrayMenuItem> trayMenuValue;
    bool trayKeepDefaultMenuValue = true;
    core::platform::TrayMenuRequestedHandler trayMenuRequestedHandlerValue;
    std::function<void(const eui::KeyEvent&)> keyEventHandler;
    std::function<void()> shutdownHandler;
    std::function<void(const std::string&)> openFileHandler;

    DslAppConfig& title(std::string value) { titleValue = std::move(value); return *this; }
    DslAppConfig& pageId(std::string value) { pageIdValue = std::move(value); return *this; }
    DslAppConfig& clearColor(const eui::Color& value) { clearColorValue = value; return *this; }
    DslAppConfig& background(const eui::Color& value) { return clearColor(value); }
    DslAppConfig& windowSize(int width, int height) {
        windowWidthValue = width;
        windowHeightValue = height;
        return *this;
    }
    DslAppConfig& windowWidth(int value) { windowWidthValue = value; return *this; }
    DslAppConfig& windowHeight(int value) { windowHeightValue = value; return *this; }
    DslAppConfig& windowPosition(int x, int y) {
        windowXValue = x;
        windowYValue = y;
        windowPositionSetValue = true;
        return *this;
    }
    DslAppConfig& centerWindow() {
        windowPositionSetValue = false;
        return *this;
    }
    DslAppConfig& minWindowSize(int width, int height) {
        minWindowWidthValue = width;
        minWindowHeightValue = height;
        return *this;
    }
    DslAppConfig& maxWindowSize(int width, int height) {
        maxWindowWidthValue = width;
        maxWindowHeightValue = height;
        return *this;
    }
    DslAppConfig& resizable(bool value = true) { resizableValue = value; return *this; }
    DslAppConfig& highDpi(bool value = true) { highDpiValue = value; return *this; }
    DslAppConfig& decorated(bool value = true) { decoratedValue = value; return *this; }
    DslAppConfig& alwaysOnTop(bool value = true) { alwaysOnTopValue = value; return *this; }
    DslAppConfig& maximized(bool value = true) { maximizedValue = value; return *this; }
    /**
     * @brief 标题栏深色模式（窗口非客户区随深浅主题联动，启动时快照）。
     *
     * 仅 Windows（DWMWA_USE_IMMERSIVE_DARK_MODE，Win11 22000+）生效，其他平台
     * 静默忽略。运行时切换请用 app::setTitleBarAppearance（即时生效，
     * 覆盖本启动快照并作用于主窗口 + 全部存活子窗口）。
     */
    DslAppConfig& darkTitleBar(bool value = true) { darkTitleBarValue = value; return *this; }
    /**
     * @brief 窗口效果（启动快照，磨砂设计 Phase B）。
     *
     * 非 None 时主窗口以透明帧缓冲创建（GLFW_TRANSPARENT_FRAMEBUFFER；
     * SDL2 无逐像素透明 flag，Vulkan 在 Windows 上 compositeAlpha 普遍
     * OPAQUE——两者降级 None），clearColor 的 alpha < 1 即整体半透；Acrylic/
     * Mica 的 DWM backdrop 应用在 Phase C。透明 hint 是创建期属性不可关闭，
     * 运行时「任何档 ↔ 关」通过把 clearColor alpha 拉回 1 视觉等价实现，
     * 不重建窗口。Vulkan 后端在 Windows 上降级为 None。半透像素的
     * premultiplied 合成修正由 GL 后端自动完成（对 alpha=1 恒等）。
     */
    DslAppConfig& windowEffect(core::platform::WindowEffect value) {
        windowEffectValue = value;
        return *this;
    }
    DslAppConfig& uiScale(float value) {
        uiScaleValue = value > 0.0f ? value : 1.0f;
        return *this;
    }
    DslAppConfig& showDebugStatsInTitle(bool value = true) {
        showDebugStatsInTitleValue = value;
        return *this;
    }
    DslAppConfig& debugTitleInterval(double value) {
        debugTitleIntervalValue = std::isfinite(value) && value > 0.0 ? value : 1.0;
        return *this;
    }
    DslAppConfig& showDebugOverlay(bool value = true) {
        showDebugOverlayValue = value;
        return *this;
    }
    DslAppConfig& onDebugOverlay(std::function<void(eui::Ui&, const eui::Screen&)> callback) {
        debugOverlayCompose = std::move(callback);
        return *this;
    }
    DslAppConfig& fps(double value) { fpsValue = value; return *this; }
    DslAppConfig& iconPath(std::string value) { iconPathValue = std::move(value); return *this; }
    DslAppConfig& textFont(std::string value) { textFontFileValue = std::move(value); return *this; }
    DslAppConfig& iconFont(std::string value) { iconFontFileValue = std::move(value); return *this; }
    DslAppConfig& fonts(std::string textFont, std::string iconFont = {}) {
        textFontFileValue = std::move(textFont);
        iconFontFileValue = std::move(iconFont);
        return *this;
    }
    DslAppConfig& tray(bool value = true) {
        trayEnabledValue = value;
        return *this;
    }
    DslAppConfig& trayTitle(std::string value) {
        trayTitleValue = std::move(value);
        return *this;
    }
    DslAppConfig& trayIcon(std::string value) {
        trayIconPathValue = std::move(value);
        return *this;
    }
    /**
     * @brief 自定义托盘菜单（text 为 "-" 表示分隔线）。
     *
     * 默认保留内置 Show/Exit 项；keepDefault=false 时仅显示自定义项
     * （此时需自行提供退出入口）。空列表等效于不调用（回退默认菜单）。
     * 菜单回调在主线程触发，可在回调里安全更新 UI 状态并 requestUpdate()。
     */
    DslAppConfig& trayMenu(std::vector<core::platform::TrayMenuItem> items, bool keepDefault = true) {
        trayMenuValue = std::move(items);
        trayKeepDefaultMenuValue = keepDefault;
        return *this;
    }
    /**
     * @brief 托盘菜单接管回调（当前仅 Windows 触发；见
     *        core::platform::setTrayMenuRequestedHandler 语义）。
     *
     * 托盘按下（左/右键）时先于原生菜单弹出调用 handler(x, y, leftButton)：
     * 返回 true = 上层已自行展示菜单（如 components::showStyledMenu），原生
     * 菜单跳过；返回 false = 照常弹 trayMenu() 注册的原生菜单（降级路径）。
     * 坐标为光标物理像素，主线程回调。未注册 = 行为与本参数不存在时一致。
     */
    DslAppConfig& onTrayMenu(std::function<bool(int x, int y, bool leftButton)> handler) {
        trayMenuRequestedHandlerValue = std::move(handler);
        return *this;
    }
    DslAppConfig& onKeyEvent(std::function<void(const eui::KeyEvent&)> handler) {
        keyEventHandler = std::move(handler);
        return *this;
    }
    /**
     * @brief 打开文件回调：Finder 双击 / 右键「打开方式」/ 拖到 Dock 图标 / 系统打开请求。
     *
     * 回调在主线程、每帧的输入处理之前触发（早于 compose），可以安全地切换页面或加载数据。
     * macOS 走 kAEOpenDocuments Apple Event；其他平台目前不会触发（队列 API 仍可用）。
     */
    DslAppConfig& onOpenFile(std::function<void(const std::string& path)> handler) {
        openFileHandler = std::move(handler);
        return *this;
    }
    /** @brief UI/渲染线程退出回调，在主窗口 GPU 设备销毁前释放应用资源引用。 */
    DslAppConfig& onShutdown(std::function<void()> handler) {
        shutdownHandler = std::move(handler);
        return *this;
    }
};

struct DslWindowConfig {
    std::string titleValue = "Window";
    std::string pageIdValue = "window";
    eui::Color clearColorValue = {0.16f, 0.18f, 0.20f, 1.0f};
    int windowWidthValue = 640;
    int windowHeightValue = 420;
    bool modalValue = false;
    // ---- 子窗口配置（桌宠设计 §2.6 G1/G2/G4/G5）----
    int windowXValue = 0;
    int windowYValue = 0;
    bool windowPositionSetValue = false;
    bool decoratedValue = true;
    bool alwaysOnTopValue = false;
    bool resizableValue = true;
    bool focusOnShowValue = true;
    bool clickThroughValue = false;
    bool hideFromTaskbarValue = false;
    bool transparentFramebufferValue = false;
    bool ignoreClearColorOverrideValue = false;
    // nullopt = 跟随全局档位；显式值 = 本窗口固定档位（见 windowEffect()）
    std::optional<core::platform::WindowEffect> windowEffectOverrideValue;
    std::function<void(core::window::Handle)> windowCreatedHandler;
    std::function<void(core::window::Handle)> windowDestroyedHandler;

    DslWindowConfig& title(std::string value) { titleValue = std::move(value); return *this; }
    DslWindowConfig& pageId(std::string value) { pageIdValue = std::move(value); return *this; }
    DslWindowConfig& clearColor(const eui::Color& value) { clearColorValue = value; return *this; }
    DslWindowConfig& background(const eui::Color& value) { return clearColor(value); }
    DslWindowConfig& windowSize(int width, int height) {
        windowWidthValue = width;
        windowHeightValue = height;
        return *this;
    }
    DslWindowConfig& windowWidth(int value) { windowWidthValue = value; return *this; }
    DslWindowConfig& windowHeight(int value) { windowHeightValue = value; return *this; }
    DslWindowConfig& windowPosition(int x, int y) {
        windowXValue = x;
        windowYValue = y;
        windowPositionSetValue = true;
        return *this;
    }
    DslWindowConfig& modal(bool value = true) { modalValue = value; return *this; }
    /** @brief 无边框（GLFW_DECORATED=0；桌宠/覆盖类窗口） */
    DslWindowConfig& decorated(bool value = true) { decoratedValue = value; return *this; }
    /** @brief 置顶（GLFW_FLOATING；失焦仍保持置顶） */
    DslWindowConfig& alwaysOnTop(bool value = true) { alwaysOnTopValue = value; return *this; }
    DslWindowConfig& resizable(bool value = true) { resizableValue = value; return *this; }
    /** @brief 显示时不抢前台焦点（GLFW_FOCUS_ON_SHOW=0；SDL2 后端忽略） */
    DslWindowConfig& focusOnShow(bool value = true) { focusOnShowValue = value; return *this; }
    /**
     * @brief 创建期整窗鼠标穿透（GLFW_MOUSE_PASSTHROUGH，仅无边框窗口生效）。
     * 运行时切换用 core::window::setWindowMousePassthrough。
     */
    DslWindowConfig& clickThrough(bool value = true) { clickThroughValue = value; return *this; }
    /**
     * @brief 任务栏/Alt+Tab 隐藏（Windows WS_EX_TOOLWINDOW；macOS/Linux
     * 静默降级——子窗口本就不进 Dock）。
     */
    DslWindowConfig& hideFromTaskbar(bool value = true) { hideFromTaskbarValue = value; return *this; }
    /**
     * @brief 逐像素透明帧缓冲（GLFW_TRANSPARENT_FRAMEBUFFER，创建期属性）。
     *
     * clearColor alpha=0 的 sprite 覆盖窗（桌宠）必设：不透明帧缓冲上透明底
     * 呈现为黑底。与 app 级窗口效果档位解耦（setWindowEffect(None) 不影响
     * 本窗口的透明能力）；Vulkan/SDL2 后端静默降级。
     */
    DslWindowConfig& transparentFramebuffer(bool value = true) {
        transparentFramebufferValue = value;
        return *this;
    }
    /**
     * @brief 不跟随 app::setClearColor 的全局广播（默认 false = 跟随）。
     * 桌宠这类自管背景色的覆盖窗口（clearColor alpha=0 的 sprite 窗）应设
     * true，否则全局主题/窗口效果切档会把透明底冲成主窗口底色。
     */
    DslWindowConfig& ignoreClearColorOverride(bool value = true) {
        ignoreClearColorOverrideValue = value;
        return *this;
    }
    /**
     * @brief 本窗口的窗口效果（DWM backdrop）档位，不跟随全局切换。
     *
     * 默认（不调用）= 跟随 app::setWindowEffect 的全局广播与启动档位。
     * 自管背景的覆盖窗（桌宠 sprite 窗：clearColor alpha=0 逐像素透出
     * 桌面）应显式传 None——否则全局磨砂/半透档会把系统 backdrop 材质
     * 画在窗口后面，透明像素透出的是材质色（灰）而非桌面。透明帧缓冲
     * hint 由 transparentFramebuffer(true) 单独保证，不受本档位影响。
     */
    DslWindowConfig& windowEffect(core::platform::WindowEffect value) {
        windowEffectOverrideValue = value;
        return *this;
    }
    /** @brief 子窗口创建成功回调（Handle 可用于 setWindowPos / 穿透切换等） */
    DslWindowConfig& onWindowCreated(std::function<void(core::window::Handle)> handler) {
        windowCreatedHandler = std::move(handler);
        return *this;
    }
    /**
     * @brief 子窗口销毁前回调（与 onWindowCreated 对称；传入句柄在回调
     * 期间仍有效，回调返回后窗口即被销毁）。覆盖所有销毁路径
     *（requestWindowClose / 窗口关闭事件（Alt+F4）/ 应用退出）。
     * 典型用途：宿主清空缓存的窗口句柄，避免悬垂引用。
     */
    DslWindowConfig& onWindowDestroyed(std::function<void(core::window::Handle)> handler) {
        windowDestroyedHandler = std::move(handler);
        return *this;
    }
    DslWindowConfig& onKeyEvent(std::function<void(const eui::KeyEvent&)> handler) {
        keyEventHandler = std::move(handler);
        return *this;
    }
    std::function<void(const eui::KeyEvent&)> keyEventHandler;
};

const DslAppConfig& dslAppConfig();
void compose(eui::Ui& ui, const eui::Screen& screen);

void openWindow(const DslWindowConfig& config, DslWindowCompose composeFn);
void openWindow(const char* title, int width, int height, DslWindowCompose composeFn);

} // namespace app
