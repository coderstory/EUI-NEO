#pragma once

#include "eui/app.h"
#include "eui/async.h"
#include "eui/platform.h"

#include <cmath>
#include <functional>
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
    std::function<void(const eui::KeyEvent&)> keyEventHandler;

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
    DslWindowConfig& modal(bool value = true) { modalValue = value; return *this; }
    DslWindowConfig& onKeyEvent(std::function<void(const eui::KeyEvent&)> handler) {
        keyEventHandler = std::move(handler);
        return *this;
    }
};

const DslAppConfig& dslAppConfig();
void compose(eui::Ui& ui, const eui::Screen& screen);

void openWindow(const DslWindowConfig& config, DslWindowCompose composeFn);
void openWindow(const char* title, int width, int height, DslWindowCompose composeFn);

} // namespace app
