#pragma once

#include "eui/dsl_app.h"
#include "eui/network.h"

#include "3rd/stb_image.h"
#include "core/dsl_runtime.h"
#include "core/platform/platform.h"
#include "core/render/text.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <optional>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace app {

namespace detail {

inline core::dsl::Runtime& dslRuntime() {
    static core::dsl::Runtime runtime;
    return runtime;
}

inline std::vector<DslWindowRequest>& dslWindowRequests() {
    static std::vector<DslWindowRequest> requests;
    return requests;
}

// 标题栏外观的运行时覆盖（std::nullopt = 未覆盖，走 dslAppConfig 启动快照）
inline std::optional<core::platform::TitleBarAppearance>& titleBarAppearanceOverride() {
    static std::optional<core::platform::TitleBarAppearance> override_;
    return override_;
}

// clearColor 的运行时覆盖（std::nullopt = 未覆盖，走各窗口启动快照）。
// 主窗口在 app::render 里每帧读取；子窗口由主循环检测变更后广播。
inline std::optional<eui::Color>& clearColorOverride() {
    static std::optional<eui::Color> override_;
    return override_;
}

// 窗口效果的运行时覆盖（std::nullopt = 未覆盖，走 dslAppConfig 启动快照）。
// 主循环帧内检测变更后应用到主窗口 + 全部存活子窗口。
inline std::optional<core::platform::WindowEffect>& windowEffectOverride() {
    static std::optional<core::platform::WindowEffect> override_;
    return override_;
}

// 实际生效的窗口效果（glfw_app_main 应用/降级后回写；nullopt = 尚未应用，
// activeWindowEffect() 此时回退到期望值）。
inline std::optional<core::platform::WindowEffect>& activeWindowEffectValue() {
    static std::optional<core::platform::WindowEffect> value;
    return value;
}

struct DslAppState {
    bool composed = false;
    bool iconApplied = false;
    float logicalWidth = 0.0f;
    float logicalHeight = 0.0f;
};

inline DslAppState& dslAppState() {
    static DslAppState state;
    return state;
}

inline std::string resolveIconPath(const std::string& iconPath) {
    if (iconPath.empty()) {
        return {};
    }

    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path requested(iconPath);
    const fs::path current = fs::current_path(error);
    std::vector<fs::path> candidates;
    candidates.push_back(requested);
    if (!error) {
        candidates.push_back(current / requested);
        candidates.push_back(current / "assets" / requested.filename());
    }

    fs::path executableDir;
#if defined(__APPLE__)
    char executablePath[4096];
    uint32_t executablePathSize = sizeof(executablePath);
    if (_NSGetExecutablePath(executablePath, &executablePathSize) == 0) {
        executableDir = fs::absolute(fs::path(executablePath), error).parent_path();
    }
#elif defined(_WIN32)
    char executablePath[MAX_PATH];
    const DWORD executablePathSize = GetModuleFileNameA(nullptr, executablePath, MAX_PATH);
    if (executablePathSize > 0 && executablePathSize < MAX_PATH) {
        executableDir = fs::absolute(fs::path(executablePath), error).parent_path();
    }
#elif defined(__linux__)
    char executablePath[4096];
    const ssize_t executablePathSize = readlink("/proc/self/exe", executablePath, sizeof(executablePath) - 1);
    if (executablePathSize > 0) {
        executablePath[executablePathSize] = '\0';
        executableDir = fs::absolute(fs::path(executablePath), error).parent_path();
    }
#endif
    if (!executableDir.empty()) {
        candidates.push_back(executableDir / requested);
        candidates.push_back(executableDir / "assets" / requested.filename());
    }

    for (const fs::path& candidate : candidates) {
        error.clear();
        if (fs::exists(candidate, error) && !error) {
            return fs::absolute(candidate, error).string();
        }
    }
    return {};
}

inline void applyWindowIcon(core::window::Handle window) {
    if (window == nullptr) {
        return;
    }

    const std::string iconPath = resolveIconPath(dslAppConfig().iconPathValue);
    if (iconPath.empty()) {
        return;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* pixels = stbi_load(iconPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        if (pixels != nullptr) {
            stbi_image_free(pixels);
        }
        return;
    }

    core::window::setWindowIcon(window, width, height, pixels);
    stbi_image_free(pixels);
}

} // namespace detail

void openWindow(const DslWindowConfig& config, DslWindowCompose composeFn) {
    if (!composeFn) {
        return;
    }

    DslWindowRequest request;
    request.title = config.titleValue.empty() ? "Window" : config.titleValue;
    request.pageId = config.pageIdValue.empty() ? request.title : config.pageIdValue;
    request.clearColor = config.clearColorValue;
    // 最小尺寸只防 0/负数（G2：桌宠 128×128 等小窗曾被 160×120 下限强行放大）
    request.width = std::max(1, config.windowWidthValue);
    request.height = std::max(1, config.windowHeightValue);
    request.modal = config.modalValue;
    request.x = config.windowXValue;
    request.y = config.windowYValue;
    request.positionSet = config.windowPositionSetValue;
    request.decorated = config.decoratedValue;
    request.alwaysOnTop = config.alwaysOnTopValue;
    request.resizable = config.resizableValue;
    request.focusOnShow = config.focusOnShowValue;
    request.mousePassthrough = config.clickThroughValue;
    request.hideFromTaskbar = config.hideFromTaskbarValue;
    request.transparentFramebuffer = config.transparentFramebufferValue;
    request.followClearColorOverride = !config.ignoreClearColorOverrideValue;
    request.onWindowCreated = config.windowCreatedHandler;
    request.onKeyEvent = config.keyEventHandler;
    request.compose = std::move(composeFn);
    detail::dslWindowRequests().push_back(std::move(request));
    requestUpdate();
}

void openWindow(const char* title, int width, int height, DslWindowCompose composeFn) {
    openWindow(DslWindowConfig{}
                   .title(title != nullptr ? title : "Window")
                   .pageId(title != nullptr ? title : "window")
                   .windowSize(width, height),
               std::move(composeFn));
}

std::vector<DslWindowRequest> consumeWindowRequests() {
    std::vector<DslWindowRequest> requests = std::move(detail::dslWindowRequests());
    detail::dslWindowRequests().clear();
    return requests;
}

const char* windowTitle() {
    return dslAppConfig().titleValue.c_str();
}

bool showDebugStatsInTitle() {
    return dslAppConfig().showDebugStatsInTitleValue;
}

double debugTitleUpdateInterval() {
    const double interval = dslAppConfig().debugTitleIntervalValue;
    return std::isfinite(interval) && interval > 0.0 ? interval : 1.0;
}

bool showDebugOverlay() {
    return dslAppConfig().showDebugOverlayValue &&
           static_cast<bool>(dslAppConfig().debugOverlayCompose);
}

double frameRateLimit() {
    return dslAppConfig().fpsValue;
}

int initialWindowWidth() {
    return dslAppConfig().windowWidthValue;
}

int initialWindowHeight() {
    return dslAppConfig().windowHeightValue;
}

int initialWindowX() {
    return dslAppConfig().windowXValue;
}

int initialWindowY() {
    return dslAppConfig().windowYValue;
}

bool initialWindowPositionSet() {
    return dslAppConfig().windowPositionSetValue;
}

core::platform::WindowEffect windowEffect() {
    return dslAppConfig().windowEffectValue;
}

int minimumWindowWidth() {
    return dslAppConfig().minWindowWidthValue;
}

int minimumWindowHeight() {
    return dslAppConfig().minWindowHeightValue;
}

int maximumWindowWidth() {
    return dslAppConfig().maxWindowWidthValue;
}

int maximumWindowHeight() {
    return dslAppConfig().maxWindowHeightValue;
}

bool windowResizable() {
    return dslAppConfig().resizableValue;
}

bool windowHighDpi() {
    return dslAppConfig().highDpiValue;
}

bool windowDecorated() {
    return dslAppConfig().decoratedValue;
}

bool windowAlwaysOnTop() {
    return dslAppConfig().alwaysOnTopValue;
}

bool windowMaximized() {
    return dslAppConfig().maximizedValue;
}

float uiScale() {
    const float configuredScale = dslAppConfig().uiScaleValue;
    return configuredScale > 0.0f ? configuredScale : 1.0f;
}

bool trayEnabled() {
    return dslAppConfig().trayEnabledValue;
}

const char* trayTitle() {
    const DslAppConfig& config = dslAppConfig();
    return (config.trayTitleValue.empty() ? config.titleValue : config.trayTitleValue).c_str();
}

const char* trayIconPath() {
    const DslAppConfig& config = dslAppConfig();
    return (config.trayIconPathValue.empty() ? config.iconPathValue : config.trayIconPathValue).c_str();
}

const std::vector<core::platform::TrayMenuItem>& trayMenuItems() {
    return dslAppConfig().trayMenuValue;
}

bool trayKeepDefaultMenuItems() {
    return dslAppConfig().trayKeepDefaultMenuValue;
}

void requestUpdate() {
    core::platform::requestUiUpdate();
}

// requestExit 的退出请求（主循环每帧经 detail::consumeExitRequest 取走）
inline std::atomic<bool>& exitRequestedFlag() {
    static std::atomic<bool> flag{false};
    return flag;
}

void requestExit() {
    exitRequestedFlag().store(true, std::memory_order_relaxed);
    // 唤醒可能 glfwWaitEvents / SDL_WaitEvent 中的主循环
    core::platform::requestUiUpdate();
}

namespace detail {
bool consumeExitRequest() {
    return exitRequestedFlag().exchange(false, std::memory_order_relaxed);
}
}

// requestShow 的显示请求（主循环每帧经 detail::consumeShowRequest 取走）。
// 与托盘 Show（tray bridge 的 consumeTrayShowRequested）不同：这是应用级
// API，任意窗口回调（子窗口菜单等）没有主窗口句柄也能请求显示/还原主窗。
inline std::atomic<bool>& showRequestedFlag() {
    static std::atomic<bool> flag{false};
    return flag;
}

void requestShow() {
    showRequestedFlag().store(true, std::memory_order_relaxed);
    // 唤醒可能 glfwWaitEvents / SDL_WaitEvent 中的主循环
    core::platform::requestUiUpdate();
}

namespace detail {
bool consumeShowRequest() {
    return showRequestedFlag().exchange(false, std::memory_order_relaxed);
}
}

void setTitleBarAppearance(const core::platform::TitleBarAppearance& appearance) {
    detail::titleBarAppearanceOverride() = appearance;
    // 唤醒可能 glfwWaitEvents 中的主循环，让外观在下一帧生效
    core::platform::requestUiUpdate();
}

core::platform::TitleBarAppearance currentTitleBarAppearance() {
    if (const std::optional<core::platform::TitleBarAppearance>& override_ = detail::titleBarAppearanceOverride()) {
        return *override_;
    }
    core::platform::TitleBarAppearance appearance;
    appearance.dark = dslAppConfig().darkTitleBarValue;
    return appearance;
}

void setClearColor(const eui::Color& color) {
    detail::clearColorOverride() = color;
    // 主窗口底色全量重绘（clearColor 参与 clear，脏区推导覆盖不到），
    // 并唤醒可能 glfwWaitEvents 中的主循环；子窗口由主循环广播。
    detail::requestFullPaint();
}

eui::Color currentClearColor() {
    if (const std::optional<eui::Color>& override_ = detail::clearColorOverride()) {
        return *override_;
    }
    return dslAppConfig().clearColorValue;
}

void setWindowEffect(core::platform::WindowEffect effect) {
    detail::windowEffectOverride() = effect;
    // 唤醒可能 glfwWaitEvents 中的主循环，让 backdrop 档位在下一帧应用
    core::platform::requestUiUpdate();
}

core::platform::WindowEffect currentWindowEffect() {
    if (const std::optional<core::platform::WindowEffect>& override_ = detail::windowEffectOverride()) {
        return *override_;
    }
    return dslAppConfig().windowEffectValue;
}

core::platform::WindowEffect activeWindowEffect() {
    if (const std::optional<core::platform::WindowEffect>& applied = detail::activeWindowEffectValue()) {
        return *applied;
    }
    // 主循环尚未应用（启动早期）：期望值是最佳已知值
    return currentWindowEffect();
}

namespace detail {

void requestFullPaint() {
    dslRuntime().requestFullPaint();
    core::platform::requestUiUpdate();
}

void setActiveWindowEffect(core::platform::WindowEffect effect) {
    activeWindowEffectValue() = effect;
}

} // namespace detail

bool initialize(core::window::Handle window) {
    const DslAppConfig& config = dslAppConfig();
    core::TextPrimitive::setDefaultFontFiles(config.textFontFileValue, config.iconFontFileValue);
    detail::dslRuntime().setKeyEventHandler(config.keyEventHandler);
    // 打开文件请求（Finder 双击 / 打开方式）：安装原生事件源并注册回调。
    // 主循环每帧调用 core::platform::pollOpenFiles() 派发。
    core::platform::installOpenFileHandler();
    core::platform::setOpenFileHandler(config.openFileHandler);

    detail::DslAppState& state = detail::dslAppState();
    if (!state.iconApplied) {
        detail::applyWindowIcon(window);
        state.iconApplied = true;
    }
    return detail::dslRuntime().initialize(window);
}

bool update(core::window::Handle window, float deltaSeconds, int windowWidth, int windowHeight, float dpiScale, float pointerScale) {
    const bool asyncReady = core::async::dispatchReady();
    const bool updateRequested = core::platform::consumeUiUpdate();
    return update(window, deltaSeconds, windowWidth, windowHeight, dpiScale, pointerScale, updateRequested || asyncReady);
}

bool update(core::window::Handle window, float deltaSeconds, int windowWidth, int windowHeight, float dpiScale, float pointerScale, bool updateRequested) {
    return update(window, deltaSeconds, windowWidth, windowHeight, dpiScale, pointerScale, updateRequested, true);
}

bool update(core::window::Handle window, float deltaSeconds, int windowWidth, int windowHeight, float dpiScale, float pointerScale, bool updateRequested, bool inputEnabled) {
    if (windowWidth <= 0 || windowHeight <= 0 || dpiScale <= 0.0f) {
        return false;
    }

    const DslAppConfig& config = dslAppConfig();
    const float effectiveScale = dpiScale * uiScale();
    const float logicalWidth = static_cast<float>(windowWidth) / effectiveScale;
    const float logicalHeight = static_cast<float>(windowHeight) / effectiveScale;
    detail::DslAppState& state = detail::dslAppState();

    const auto composeFrame = [&] {
        detail::dslRuntime().compose(config.pageIdValue, logicalWidth, logicalHeight, [](core::dsl::Ui& ui, const core::dsl::Screen& screen) {
            compose(ui, screen);
            const DslAppConfig& config = dslAppConfig();
            if (showDebugOverlay()) {
                config.debugOverlayCompose(ui, screen);
            }
        });
        state.composed = true;
        state.logicalWidth = logicalWidth;
        state.logicalHeight = logicalHeight;
    };

    if (!state.composed || state.logicalWidth != logicalWidth || state.logicalHeight != logicalHeight) {
        composeFrame();
    }

    bool changed = false;
    if (updateRequested) {
        composeFrame();
        changed = true;
    }

    changed = detail::dslRuntime().update(window, deltaSeconds, pointerScale, effectiveScale, inputEnabled) || changed;
    if (detail::dslRuntime().composeRequested()) {
        // A compose can change retained content without changing the element structure.
        // Rebuild the complete cache so state and release visuals update in this frame.
        detail::dslRuntime().requestFullPaint();
        composeFrame();
        changed = detail::dslRuntime().update(window, 0.0f, pointerScale, effectiveScale, inputEnabled) || changed;
        changed = true;
    }

    return changed;
}

bool isAnimating() {
    return detail::dslRuntime().isAnimating();
}

void render(int windowWidth, int windowHeight, float dpiScale) {
    if (windowWidth <= 0 || windowHeight <= 0 || dpiScale <= 0.0f) {
        return;
    }

    const float effectiveScale = dpiScale * uiScale();
    // clearColor 运行时覆盖优先（app::setClearColor，磨砂 Phase B）
    detail::dslRuntime().render(windowWidth, windowHeight, effectiveScale, currentClearColor());
}

void releaseGraphicsResources() {
    detail::dslRuntime().releaseGraphicsResources();
}

void shutdown() {
    core::async::shutdown();
    if (dslAppConfig().shutdownHandler) dslAppConfig().shutdownHandler();
    detail::dslRuntime().shutdown();
    eui::network::shutdown();
}

} // namespace app
