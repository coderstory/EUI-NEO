#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#endif

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#include <GLFW/glfw3native.h>
#elif defined(__APPLE__)
// macOS 路径（M0：macos-three-features 设计 §3）：GLFW 官方原生窗口查询 API
// glfwGetCocoaWindow（返回 NSWindow*）需先定义 GLFW_EXPOSE_NATIVE_COCOA
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#endif

#include "eui/app.h"
#include "eui/detail/dsl_app_impl.h"
#include "core/app/app_runner.h"
#include "core/app/dsl_window_manager.h"
#include "core/app/dsl_window_runtime.h"
#include "core/app/frame_pacing.h"
#include "core/app/main_window_runtime.h"
#include "core/input/input_state.h"
#include "core/platform/platform.h"
#include "core/platform/power_events.h"
#include "core/platform/window_effect.h"
#include "core/platform/window_style.h"
#include "core/window/window_backend.h"
#include "core/render/render_backend.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

struct WindowState : app::AppRunner {
    bool hideToTrayRequested = false;
    bool forceClose = false;
    bool iconified = false;
    GLFWwindow* modalChildWindow = nullptr;
};

struct ManagedWindow {
    GLFWwindow* window = nullptr;
    WindowState state;
    app::DslWindowRuntime content;
    std::unique_ptr<core::render::RenderBackend> renderBackend;
};

struct TimerResolutionGuard {
    TimerResolutionGuard() {
#ifdef _WIN32
        timeBeginPeriod(1);
#endif
    }

    ~TimerResolutionGuard() {
#ifdef _WIN32
        timeEndPeriod(1);
#endif
    }
};

float getDpiScale(GLFWwindow* window) {
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    glfwGetWindowContentScale(window, &scaleX, &scaleY);
    return (scaleX + scaleY) * 0.5f;
}

float getPointerScale(GLFWwindow* window) {
    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

    if (windowWidth <= 0 || windowHeight <= 0) {
        return 1.0f;
    }

    const float scaleX = static_cast<float>(framebufferWidth) / static_cast<float>(windowWidth);
    const float scaleY = static_cast<float>(framebufferHeight) / static_cast<float>(windowHeight);
    return (scaleX + scaleY) * 0.5f;
}

GLFWmonitor* getWindowMonitor(GLFWwindow* window) {
    if (GLFWmonitor* monitor = glfwGetWindowMonitor(window)) {
        return monitor;
    }

    int windowX = 0;
    int windowY = 0;
    int windowWidth = 0;
    int windowHeight = 0;
    glfwGetWindowPos(window, &windowX, &windowY);
    glfwGetWindowSize(window, &windowWidth, &windowHeight);

    int monitorCount = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    GLFWmonitor* bestMonitor = glfwGetPrimaryMonitor();
    int bestArea = 0;

    for (int i = 0; i < monitorCount; ++i) {
        GLFWmonitor* monitor = monitors[i];
        int monitorX = 0;
        int monitorY = 0;
        glfwGetMonitorPos(monitor, &monitorX, &monitorY);
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (!mode) {
            continue;
        }

        const int overlapLeft = std::max(windowX, monitorX);
        const int overlapTop = std::max(windowY, monitorY);
        const int overlapRight = std::min(windowX + windowWidth, monitorX + mode->width);
        const int overlapBottom = std::min(windowY + windowHeight, monitorY + mode->height);
        const int overlapWidth = std::max(0, overlapRight - overlapLeft);
        const int overlapHeight = std::max(0, overlapBottom - overlapTop);
        const int overlapArea = overlapWidth * overlapHeight;
        if (overlapArea > bestArea) {
            bestArea = overlapArea;
            bestMonitor = monitor;
        }
    }

    return bestMonitor;
}

double getWindowRefreshRate(GLFWwindow* window) {
#ifdef _WIN32
    HWND hwnd = glfwGetWin32Window(window);
    HMONITOR nativeMonitor = hwnd != nullptr ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;
    if (nativeMonitor != nullptr) {
        MONITORINFOEXW monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (GetMonitorInfoW(nativeMonitor, &monitorInfo)) {
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (EnumDisplaySettingsW(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &mode) &&
                mode.dmDisplayFrequency > 1) {
                return static_cast<double>(mode.dmDisplayFrequency);
            }
        }
    }
#endif
    GLFWmonitor* monitor = getWindowMonitor(window);
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    if (mode && mode->refreshRate > 0) {
        return static_cast<double>(mode->refreshRate);
    }
    return 60.0;
}

void updateFrameInterval(GLFWwindow* window, WindowState& windowState, double now, bool force = false) {
    windowState.updateFrameInterval(getWindowRefreshRate(window), now, force);
}

void waitForNextFrame(GLFWwindow* window, const WindowState& windowState) {
    while (!glfwWindowShouldClose(window)) {
        const double remaining = windowState.nextFrameTime - glfwGetTime();
        if (remaining <= 0.0) {
            break;
        }

        app::detail::waitForFrameDuration(remaining);
    }
}

void hideWindowToTray(GLFWwindow* window, WindowState& windowState, core::render::RenderBackend& renderBackend) {
    if (!windowState.trayAvailable || windowState.hiddenToTray) {
        return;
    }

    core::render::ScopedRenderBackend scopedRenderBackend(renderBackend);
    app::releaseGraphicsResources();
    core::cancelInput(window);
    glfwHideWindow(window);
    windowState.hiddenToTray = true;
    windowState.hideToTrayRequested = false;
    windowState.paintRequested = false;
    windowState.renderedFrames = 0;
    windowState.nextFrameTime = glfwGetTime();
}

void restoreWindowFromTray(GLFWwindow* window, WindowState& windowState) {
    if (!windowState.hiddenToTray) {
        return;
    }

    glfwRestoreWindow(window);
    glfwShowWindow(window);
    glfwFocusWindow(window);
    windowState.hiddenToTray = false;
    windowState.hideToTrayRequested = false;
    windowState.paintRequested = true;
    app::detail::requestFullPaint();
    windowState.nextFrameTime = glfwGetTime();
}

void installWindowCallbacks(GLFWwindow* window, WindowState& windowState) {
    glfwSetWindowUserPointer(window, &windowState);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* currentWindow, int w, int h) {
        static_cast<WindowState*>(glfwGetWindowUserPointer(currentWindow))->paintRequested = true;
        if (w > 0 && h > 0) {
            app::detail::requestFullPaint();
        }
    });
    glfwSetWindowRefreshCallback(window, [](GLFWwindow* currentWindow) {
        static_cast<WindowState*>(glfwGetWindowUserPointer(currentWindow))->paintRequested = true;
        app::detail::requestFullPaint();
    });
    glfwSetWindowContentScaleCallback(window, [](GLFWwindow* currentWindow, float, float) {
        static_cast<WindowState*>(glfwGetWindowUserPointer(currentWindow))->paintRequested = true;
        app::detail::requestFullPaint();
    });
    glfwSetWindowFocusCallback(window, [](GLFWwindow* currentWindow, int focused) {
        WindowState* state = static_cast<WindowState*>(glfwGetWindowUserPointer(currentWindow));
        if (!state) {
            return;
        }
        if (focused != GLFW_TRUE) {
            core::cancelInput(currentWindow);
            // 失焦 → 请求全局重绘：styledMenu 的「失焦自关」检查嵌在菜单窗
            // compose 闭包里，而 compose 只在全局更新请求时执行——不在此
            // 处请求，点菜单外（桌宠/桌面/其它窗口）后菜单窗不再重绘、
            // 检查被饿死，菜单永不消失（DevDesk 桌宠右键菜单 bug 根因）
            core::platform::requestUiUpdate();
        }
        state->paintRequested = true;
        if (focused && state->modalChildWindow != nullptr && !glfwWindowShouldClose(state->modalChildWindow)) {
            glfwFocusWindow(state->modalChildWindow);
        }
    });
    glfwSetWindowIconifyCallback(window, [](GLFWwindow* currentWindow, int iconified) {
        WindowState* state = static_cast<WindowState*>(glfwGetWindowUserPointer(currentWindow));
        if (!state) {
            return;
        }
        state->iconified = iconified == GLFW_TRUE;
        if (state->iconified) {
            core::cancelInput(currentWindow);
        } else {
            state->paintRequested = true;
        }
    });
}

// ============ 标题栏外观联动（磨砂设计文档 Phase A）============
// macOS 返回 NSWindow*（M0：macos-three-features 设计 §3）；
// 其余平台返回 nullptr，core::platform::applyTitleBarAppearance 内部静默降级。

// —— 窗口效果/布局诊断（仅 macOS 编译；EUI_FX_DEBUG=1 时输出 stderr）——
// macOS 的窗口效果与布局折算没有单元测试可覆盖，只能靠运行时边界值取证：
// 窗口创建 hint/回查、原生句柄、效果应用与降级真值、每秒一行的布局度量
// （窗口/帧缓冲/缩放/逻辑尺寸/clearColor/生效档位）。非 Apple 平台预处理为
// 空语句，翻译单元与加诊断前逐字一致（Windows 侧行为零变化）。
#if defined(__APPLE__)
static bool fxDebugEnabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("EUI_FX_DEBUG");
        return v != nullptr && *v != '\0';
    }();
    return enabled;
}

#define EUI_FX_DIAG(...)                        \
    do {                                        \
        if (fxDebugEnabled()) {                 \
            std::fprintf(stderr, __VA_ARGS__);  \
            std::fflush(stderr);                \
        }                                       \
    } while (0)
#else
#define EUI_FX_DIAG(...) \
    do {                 \
    } while (0)
#endif

void* nativeWindowHandle(GLFWwindow* window) {
#if defined(_WIN32)
    return glfwGetWin32Window(window);
#elif defined(__APPLE__)
    // GLFW 官方 macOS 原生窗口查询 API（返回 NSWindow*，见 3rd/glfw 的
    // glfwGetCocoaWindow）；M2 磨砂 applyWindowEffect 与标题栏外观消费此句柄
    void* handle = glfwGetCocoaWindow(window);
    EUI_FX_DIAG("[fxdiag] nativeWindowHandle glfwWindow=%p nsWindow=%p\n",
                static_cast<void*>(window), handle);
    return handle;
#else
    (void)window;
    return nullptr;
#endif
}

void applyTitleBarAppearanceToWindow(GLFWwindow* window) {
    core::platform::TitleBarAppearance appearance = app::currentTitleBarAppearance();
#if defined(__APPLE__)
    // macOS：标题栏底的来源是**窗口背景色**（平台层用 setBackgroundColor +
    // titlebarAppearsTransparent 让标题栏透出它），宿主没显式给 customColor 时
    // 这里用当前 clearColor（= 内容区底色，含窗口效果档的 alpha）兜底 ——
    // DevDesk 只调 app::setClearColor，不直接设 customColor，标题栏「跟随主题、
    // 与 content 融为一体」全靠这一步推导。Windows 侧不推导：DWMWA_CAPTION_COLOR
    // 与窗口背景色不是一回事（改了标题栏会与内容不同色），保持原语义不动。
    if (!appearance.customColor) {
        const eui::Color background = app::currentClearColor();
        const auto channel = [](float value) -> std::uint32_t {
            const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
            return static_cast<std::uint32_t>(clamped * 255.0f + 0.5f);
        };
        appearance.customColor = true;
        // TitleBarAppearance::colorAbgr 沿用 Windows 的 COLORREF 布局（0x00BBGGRR）
        appearance.colorAbgr = (channel(background.b) << 16) |
                               (channel(background.g) << 8) |
                               channel(background.r);
        appearance.colorAlpha = background.a;
    }
#endif
    core::platform::applyTitleBarAppearance(nativeWindowHandle(window), appearance);
}

// 窗口效果应用 + 降级（磨砂 Phase C）：backdrop 档位交给平台层，失败时按
// 透明帧缓冲是否实际开启（GLFW 创建期 hint 回查的真值）折到 Transparent/None。
// 返回该窗口实际生效的效果；主循环用它回写 app::activeWindowEffect()。
core::platform::WindowEffect applyWindowEffectToWindow(GLFWwindow* window, core::platform::WindowEffect desired) {
    const bool transparentFramebuffer =
        glfwGetWindowAttrib(window, GLFW_TRANSPARENT_FRAMEBUFFER) == GLFW_TRUE;
    if (core::platform::applyWindowEffect(nativeWindowHandle(window), desired)) {
        EUI_FX_DIAG("[fxdiag] applyWindowEffectToWindow window=%p desired=%d -> applied=%d\n",
                    static_cast<void*>(window), static_cast<int>(desired),
                    static_cast<int>(desired));
#if defined(__APPLE__)
        // None 档回写也要按「实际视觉状态」折（与下面失败路径的 degradedWindowEffect
        // 同一判据，两条路径对同一视觉状态必须给出同一个值）：透明帧缓冲生效的窗口
        // 即便关掉 backdrop 也不是实色窗——WindowEffect::None 的文档语义是「实色
        // 窗口」，半透档 = None backdrop + clearColor alpha<1。不折的话 active 会
        // 停在 None，宿主的半透档降级提示（判据 active == Transparent）恒误报
        //「当前系统不支持窗口半透，已回退为不透明」（真机缺陷：半透档常显该警告）。
        if (desired == core::platform::WindowEffect::None && transparentFramebuffer) {
            return core::platform::WindowEffect::Transparent;
        }
#else
        // ⚠️ Windows 侧存在**同一个不一致**（同一函数、同一判据缺口）：DWM backdrop
        // 关成功时回写 desired=None，而宿主的半透档提示判据要求 active==Transparent
        // → 理论上同样会误报降级。本批不改 Windows 分支（无实机可验证，盲改风险大），
        // 待有 Windows 实机时按上面的同一折法处理并回归验证。
#endif
        return desired;
    }
    const core::platform::WindowEffect degraded =
        core::platform::degradedWindowEffect(desired, transparentFramebuffer);
    EUI_FX_DIAG("[fxdiag] applyWindowEffectToWindow window=%p desired=%d platform=false "
                "transparentFbAttrib=%d -> degraded=%d\n",
                static_cast<void*>(window), static_cast<int>(desired),
                transparentFramebuffer ? 1 : 0, static_cast<int>(degraded));
    return degraded;
}

// 子窗口期望档位：显式覆盖（DslWindowConfig::windowEffect，桌宠 sprite 窗的
// None——透明像素直出桌面不叠 backdrop 材质）优先，否则跟随全局档位
core::platform::WindowEffect desiredChildWindowEffect(const app::DslWindowRequest& request) {
    if (request.windowEffectOverride.has_value()) {
        return *request.windowEffectOverride;
    }
    return app::currentWindowEffect();
}

std::unique_ptr<ManagedWindow> createManagedWindow(const app::DslWindowRequest& request,
                                                   GLFWwindow* parentWindow,
                                                   core::render::RenderBackend& shareBackend) {
    core::window::WindowCreateRequest windowRequest;
    windowRequest.width = request.width;
    windowRequest.height = request.height;
    windowRequest.title = request.title.c_str();
    windowRequest.parent = parentWindow;
    windowRequest.renderApi = core::render::windowRenderApi();
    // 磨砂 Phase C：子窗口跟主窗口同档——非 None 时带透明帧缓冲 hint 创建
    windowRequest.windowEffect = app::currentWindowEffect();
    // 窗口自身声明透明帧缓冲（桌宠 sprite 窗）：与全局效果档位解耦，全局
    // setWindowEffect(None) 不剥夺子窗的逐像素透明
    windowRequest.transparentFramebuffer = request.transparentFramebuffer;
    // 子窗口配置透传（桌宠设计 §2.6 G1/G4）：decorated/alwaysOnTop/resizable/
    // position/focusOnShow/mousePassthrough，后端 createWindow 已支持
    windowRequest.x = request.x;
    windowRequest.y = request.y;
    windowRequest.positionSet = request.positionSet;
    windowRequest.decorated = request.decorated;
    windowRequest.alwaysOnTop = request.alwaysOnTop;
    windowRequest.resizable = request.resizable;
    windowRequest.focusOnShow = request.focusOnShow;
    windowRequest.mousePassthrough = request.mousePassthrough;
    // 任务栏隐藏窗隐藏创建：GLFW 可见创建即注册任务栏按钮（WS_EX_APPWINDOW），
    // 样式（TOOLWINDOW）必须在首秀前就位（下方样式应用后 glfwShowWindow）
    windowRequest.visible = !request.hideFromTaskbar;
    GLFWwindow* childWindow = static_cast<GLFWwindow*>(core::window::createWindow(windowRequest));
    if (!childWindow) {
        return {};
    }

    auto managed = std::make_unique<ManagedWindow>();
    managed->window = childWindow;
    managed->renderBackend = core::render::createRenderBackend(childWindow, &shareBackend);
    if (!managed->renderBackend) {
        core::window::destroyWindow(childWindow);
        return {};
    }
    if (!managed->renderBackend->initialize()) {
        core::window::destroyWindow(childWindow);
        return {};
    }
    managed->state.lastTitleUpdate = glfwGetTime();
    managed->state.nextFrameTime = managed->state.lastTitleUpdate;
    installWindowCallbacks(childWindow, managed->state);

    if (!managed->content.initialize(childWindow, request)) {
        managed->renderBackend.reset();
        core::releaseInputQueue(childWindow);
        core::window::destroyWindow(childWindow);
        return {};
    }

    managed->state.paintRequested = true;
    // 新子窗口跟随当前标题栏外观（运行时联动，Phase A）
    applyTitleBarAppearanceToWindow(childWindow);
    // 新子窗口跟随当前窗口效果（磨砂 Phase C）：创建期透明 hint + backdrop 档位。
    // 透明 hint 来源另有 transparentFramebuffer 声明（sprite 窗），backdrop
    // 档位尊重逐窗覆盖（None = 不叠系统材质，透明像素直出桌面）
    applyWindowEffectToWindow(childWindow, desiredChildWindowEffect(request));
    EUI_FX_DIAG("[fxdiag] child window created glfwWindow=%p override=%d resolved=%d transparentFb=%d\n",
                static_cast<void*>(childWindow),
                request.windowEffectOverride.has_value()
                    ? static_cast<int>(*request.windowEffectOverride)
                    : -1,
                static_cast<int>(desiredChildWindowEffect(request)),
                request.transparentFramebuffer ? 1 : 0);
    // clearColor 运行时覆盖（app::setClearColor）对后续新开子窗口同样生效；
    // 自管背景的窗口（ignoreClearColorOverride，如桌宠 sprite 窗）除外
    if (request.followClearColorOverride) {
        if (const std::optional<eui::Color>& override_ = app::detail::clearColorOverride()) {
            managed->content.setClearColor(*override_);
        }
    }
    // 任务栏/Alt+Tab 隐藏（桌宠设计 G5，Windows WS_EX_TOOLWINDOW）
    if (request.hideFromTaskbar) {
        core::platform::applyWindowStyleFlags(
            nativeWindowHandle(childWindow), core::platform::WindowStyleFlags{true, false});
        // 隐藏创建的窗口在此首秀（样式已就位，任务栏按钮从未注册过）；
        // FOCUS_ON_SHOW hint 由 GLFW 显示路径遵守
        glfwShowWindow(childWindow);
    }
    if (request.onWindowCreated) {
        request.onWindowCreated(childWindow);
    }
    if (managed->content.request().modal) {
        glfwFocusWindow(childWindow);
    }
    return managed;
}

void destroyManagedWindow(std::unique_ptr<ManagedWindow>& managed) {
    if (!managed || managed->window == nullptr) {
        managed.reset();
        return;
    }

    GLFWwindow* windowToDestroy = managed->window;
    // 销毁回调须在 content.shutdown() 前取出（shutdown 会清空 request_ 快照）
    std::function<void(core::window::Handle)> onWindowDestroyed =
        managed->content.request().onWindowDestroyed;
    if (managed->renderBackend) {
        managed->renderBackend->makeCurrent();
        managed->renderBackend->releaseRenderCache();
    }
    core::releaseInputQueue(windowToDestroy);
    if (managed->renderBackend) {
        core::render::ScopedRenderBackend scopedRenderBackend(*managed->renderBackend);
        managed->content.shutdown(false);
    } else {
        managed->content.shutdown(false);
    }
    managed->renderBackend.reset();
    // 宿主销毁通知（onWindowCreated 对称）：窗口销毁前触发，句柄此刻仍有效；
    // 覆盖所有销毁路径（requestWindowClose / Alt+F4 / 应用退出 destroyAll）
    if (onWindowDestroyed) {
        onWindowDestroyed(windowToDestroy);
    }
    core::window::destroyWindow(windowToDestroy);
    managed.reset();
}

bool updateManagedWindow(ManagedWindow& managed, float deltaSeconds, bool updateRequested) {
    if (managed.window == nullptr || glfwWindowShouldClose(managed.window)) {
        return false;
    }

    managed.renderBackend->makeCurrent();

    managed.state.iconified = glfwGetWindowAttrib(managed.window, GLFW_ICONIFIED) == GLFW_TRUE;
    if (managed.state.iconified) {
        managed.renderBackend->releaseRenderCache();
        managed.state.paintRequested = true;
        managed.content.requestFullPaint();
        managed.state.resetTiming(glfwGetTime());
        return true;
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(managed.window, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth <= 0 || framebufferHeight <= 0) {
        managed.renderBackend->releaseRenderCache();
        managed.state.paintRequested = true;
        managed.content.requestFullPaint();
        managed.state.resetTiming(glfwGetTime());
        return true;
    }

    const float dpiScale = getDpiScale(managed.window);
    const float pointerScale = getPointerScale(managed.window);
    const float logicalWidth = static_cast<float>(framebufferWidth) / dpiScale;
    const float logicalHeight = static_cast<float>(framebufferHeight) / dpiScale;

    core::render::ScopedRenderBackend scopedRenderBackend(*managed.renderBackend);
    if (managed.content.update(managed.window, deltaSeconds, logicalWidth, logicalHeight, pointerScale, dpiScale, updateRequested)) {
        managed.state.paintRequested = true;
    }

    if (managed.state.paintRequested || managed.content.paintRequested()) {
        managed.renderBackend->beginFrame({
            managed.window,
            core::window::nativeWindowInfo(managed.window),
            framebufferWidth,
            framebufferHeight,
            dpiScale
        });
        managed.content.render(*managed.renderBackend, framebufferWidth, framebufferHeight, dpiScale);
        managed.renderBackend->present();
        managed.state.paintRequested = false;
        ++managed.state.renderedFrames;
    }
    return true;
}

bool isManagedWindowClosed(const ManagedWindow& managed) {
    return managed.window == nullptr || glfwWindowShouldClose(managed.window);
}

bool isManagedWindowRenderable(const ManagedWindow& managed) {
    if (managed.window == nullptr || glfwWindowShouldClose(managed.window)) {
        return false;
    }
    if (managed.state.iconified || glfwGetWindowAttrib(managed.window, GLFW_ICONIFIED) == GLFW_TRUE) {
        return false;
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(managed.window, &framebufferWidth, &framebufferHeight);
    return framebufferWidth > 0 && framebufferHeight > 0;
}

bool anyRenderableManagedWindowAnimating(const app::DslWindowManager<ManagedWindow>& windows) {
    return windows.anyAnimating(isManagedWindowRenderable);
}

void pruneClosedWindows(app::DslWindowManager<ManagedWindow>& windows) {
    windows.pruneClosed(isManagedWindowClosed, destroyManagedWindow);
}

void createRequestedWindows(app::DslWindowManager<ManagedWindow>& windows,
                            GLFWwindow* shareWindow,
                            core::render::RenderBackend& shareBackend,
                            const std::vector<app::DslWindowRequest>& requests) {
    windows.createPending(requests, [&](const app::DslWindowRequest& request) {
        return createManagedWindow(request, shareWindow, shareBackend);
    });
    shareBackend.makeCurrent();
}

GLFWwindow* findModalChildWindow(app::DslWindowManager<ManagedWindow>& windows) {
    ManagedWindow* managed = windows.modalWindow(isManagedWindowClosed);
    return managed != nullptr ? managed->window : nullptr;
}

int eui_app_run() {
    core::platform::repairCurrentWorkingDirectory();
    core::render::initializeRenderBackendLoader();
    // 必须在 glfwInit() 之前：glfwInit() 内部会跑一次 NSApp 启动循环，
    // 启动期的「Finder 双击打开」事件在那里就可能到达，晚装会丢事件。
    core::platform::installOpenFileHandler();
    if (!glfwInit()) {
        return -1;
    }
    TimerResolutionGuard timerResolution;

    core::window::WindowCreateRequest windowRequest;
    windowRequest.width = app::initialWindowWidth();
    windowRequest.height = app::initialWindowHeight();
    windowRequest.x = app::initialWindowX();
    windowRequest.y = app::initialWindowY();
    windowRequest.positionSet = app::initialWindowPositionSet();
    windowRequest.minWidth = app::minimumWindowWidth();
    windowRequest.minHeight = app::minimumWindowHeight();
    windowRequest.maxWidth = app::maximumWindowWidth();
    windowRequest.maxHeight = app::maximumWindowHeight();
    windowRequest.resizable = app::windowResizable();
    windowRequest.highDpi = app::windowHighDpi();
    windowRequest.decorated = app::windowDecorated();
    windowRequest.alwaysOnTop = app::windowAlwaysOnTop();
    windowRequest.maximized = app::windowMaximized();
    windowRequest.title = app::windowTitle();
    windowRequest.renderApi = core::render::windowRenderApi();
    windowRequest.windowEffect = app::windowEffect();
    GLFWwindow* window = static_cast<GLFWwindow*>(core::window::createWindow(windowRequest));
    if (!window) {
        glfwTerminate();
        return -1;
    }

    WindowState windowState;
    windowState.resetTiming(glfwGetTime());
    updateFrameInterval(window, windowState, windowState.lastTitleUpdate, true);
    if (app::showDebugStatsInTitle()) {
        char title[128];
        std::snprintf(title, sizeof(title), "%s - 0 FPS", app::windowTitle());
        glfwSetWindowTitle(window, title);
    }
    installWindowCallbacks(window, windowState);
    // 标题栏外观启动快照（DslAppConfig::darkTitleBar；之后变更走下方帧内联动）
    applyTitleBarAppearanceToWindow(window);
    core::platform::TitleBarAppearance appliedTitleBar = app::currentTitleBarAppearance();
    // 窗口效果启动应用（磨砂 Phase C）：透明 hint 已随 WindowCreateRequest 生效，
    // 这里补 backdrop 档位并回写实际生效值（降级真值，供 app::activeWindowEffect 回查）
    core::platform::WindowEffect appliedWindowEffectDesired = app::currentWindowEffect();
    const core::platform::WindowEffect appliedWindowEffectStartup =
        applyWindowEffectToWindow(window, appliedWindowEffectDesired);
    app::detail::setActiveWindowEffect(appliedWindowEffectStartup);
    EUI_FX_DIAG("[fxdiag] startup: main window desired=%d active=%d\n",
                static_cast<int>(appliedWindowEffectDesired),
                static_cast<int>(appliedWindowEffectStartup));
    // clearColor 联动基线（app::setClearColor 变更后广播到子窗口；主窗口在
    // app::render 里直接读覆盖值）。基线取当前生效值：未覆盖时不会误伤
    // 子窗口自己的 DslWindowConfig clearColor。
    eui::Color appliedClearColor = app::currentClearColor();

    const auto cleanupMainWindow = [&] {
        core::releaseInputQueue(window);
        core::window::destroyWindow(window);
        glfwTerminate();
    };

    auto renderBackend = core::render::createRenderBackend(window);
    if (!renderBackend) {
        cleanupMainWindow();
        return -1;
    }
    if (!renderBackend->initialize()) {
        cleanupMainWindow();
        return -1;
    }

    if (!app::initialize(window)) {
        app::shutdown();
        renderBackend.reset();
        cleanupMainWindow();
        return -1;
    }
    app::MainWindowRuntime mainWindowRuntime(windowState);
    windowState.initializeTray();
    // 系统电源事件（睡眠/唤醒，DevDesk 桌宠 M2 需求）：挂主窗口 WndProc 链
    // 截获 WM_POWERBROADCAST，转发宿主经 core::platform::setSystemPowerHandler
    // 注册的回调；非 Windows / 未注册 = 空操作。必须在此处（事件泵启动前）
    // 安装，唤醒瞬间的广播才不会漏
    core::platform::installSystemPowerNotifications(nativeWindowHandle(window));
    glfwSetWindowCloseCallback(window, [](GLFWwindow* currentWindow) {
        WindowState* state = static_cast<WindowState*>(glfwGetWindowUserPointer(currentWindow));
        if (state && state->modalChildWindow != nullptr && !glfwWindowShouldClose(state->modalChildWindow)) {
            glfwFocusWindow(state->modalChildWindow);
            glfwSetWindowShouldClose(currentWindow, GLFW_FALSE);
            return;
        }
        if (state && state->trayAvailable && !state->forceClose) {
            state->hideToTrayRequested = true;
            glfwSetWindowShouldClose(currentWindow, GLFW_FALSE);
        }
    });
    glfwSetWindowIconifyCallback(window, [](GLFWwindow* currentWindow, int iconified) {
        WindowState* state = static_cast<WindowState*>(glfwGetWindowUserPointer(currentWindow));
        if (!state) {
            return;
        }
        state->iconified = iconified == GLFW_TRUE;
        if (iconified) {
            core::cancelInput(currentWindow);
        } else {
            state->paintRequested = true;
            app::detail::requestFullPaint();
        }
    });

    app::DslWindowManager<ManagedWindow> childWindows;

    while (!glfwWindowShouldClose(window)) {
        renderBackend->makeCurrent();
        windowState.pollTray(false);
        // 「打开文件」请求（macOS kAEOpenDocuments Apple Event）在事件泵里入队，
        // 这里在帧开始前派发，回调方可以安全地切换页面 / 加载数据。
        core::platform::pollOpenFiles();
        if (windowState.consumeTrayExitRequested()) {
            windowState.forceClose = true;
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
        }
        // app::requestExit（子窗口「退出」菜单等场景）：与托盘 Exit 同一退出路径
        if (app::detail::consumeExitRequest()) {
            windowState.forceClose = true;
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
        }
        if (windowState.consumeTrayShowRequested()) {
            restoreWindowFromTray(window, windowState);
        }
        // app::requestShow（自定义托盘菜单「显示主窗口」项等）：与托盘 Show
        // 同一恢复路径；主窗可见但最小化/失焦时也要还原 + 聚焦
        if (app::detail::consumeShowRequest()) {
            if (windowState.hiddenToTray) {
                restoreWindowFromTray(window, windowState);
            } else {
                if (windowState.iconified) {
                    glfwRestoreWindow(window);
                }
                glfwShowWindow(window);
                glfwFocusWindow(window);
            }
        }
        pruneClosedWindows(childWindows);
        windowState.modalChildWindow = findModalChildWindow(childWindows);
        // 设计决策（2026-09-22-desktop-pet-design.md R5）：主窗隐藏到托盘时
        // 子窗口（桌宠）独立存活——不再因 childWindows 非空取消隐藏（旧守卫
        // 使有子窗时点 X 什么都不发生：close callback 已撤回关闭、隐藏又被
        // 吞掉）。模态子窗的保护在 close callback（重聚焦模态并撤回关闭）。
        if (windowState.hideToTrayRequested) {
            renderBackend->releaseRenderCache();
            hideWindowToTray(window, windowState, *renderBackend);
        }
        if (windowState.hiddenToTray) {
            glfwWaitEventsTimeout(0.10);
            // 托盘驻留期间子窗口（桌宠）照常运行（R5：宠物独立于主窗存活）：
            // 动画推进 / 输入派发 / 开窗关窗 / 异步完成回调不断流。帧节奏由
            // 0.10s 等待上限兜底（桌宠帧率 4-12fps 量级足够）
            const bool childUpdateRequested = windowState.consumeUpdateRequest();
            const float childDelta = windowState.consumeFrameDelta(glfwGetTime());
            childWindows.updateAll([&](ManagedWindow& managed) {
                updateManagedWindow(managed, childDelta, childUpdateRequested);
            });
            createRequestedWindows(childWindows, window, *renderBackend, app::consumeWindowRequests());
            pruneClosedWindows(childWindows);
            windowState.modalChildWindow = findModalChildWindow(childWindows);
            windowState.resetTiming(glfwGetTime());
            continue;
        }

        windowState.iconified = glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_TRUE;
        if (windowState.iconified) {
            renderBackend->releaseRenderCache();
            windowState.paintRequested = false;
            windowState.consumeFrameRequest();
            windowState.resetTiming(glfwGetTime());
            glfwWaitEventsTimeout(0.25);
            continue;
        }

        if (windowState.anyAnimating(anyRenderableManagedWindowAnimating(childWindows))) {
            waitForNextFrame(window, windowState);
        }

        // 标题栏外观联动：app::setTitleBarAppearance 的变更在下一帧应用到
        // 主窗口 + 全部存活子窗口（新子窗口在创建时已应用当前值）
        if (app::currentTitleBarAppearance() != appliedTitleBar) {
            appliedTitleBar = app::currentTitleBarAppearance();
            applyTitleBarAppearanceToWindow(window);
            childWindows.updateAll([](ManagedWindow& managed) {
                applyTitleBarAppearanceToWindow(managed.window);
            });
        }

        // clearColor 联动（磨砂 Phase B）：app::setClearColor 的变更广播到全部
        // 存活子窗口（各自 requestFullPaint）；主窗口已由 setClearColor 触发全量重绘。
        // 自管背景的窗口（ignoreClearColorOverride，如桌宠 sprite 窗）不跟随。
        const eui::Color effectiveClearColor = app::currentClearColor();
        if (effectiveClearColor.r != appliedClearColor.r || effectiveClearColor.g != appliedClearColor.g ||
            effectiveClearColor.b != appliedClearColor.b || effectiveClearColor.a != appliedClearColor.a) {
            appliedClearColor = effectiveClearColor;
            childWindows.updateAll([&managedClearColor = appliedClearColor](ManagedWindow& managed) {
                if (!managed.content.request().followClearColorOverride) {
                    return;
                }
                managed.content.setClearColor(managedClearColor);
            });
#if defined(__APPLE__)
            // macOS 标题栏底 = 内容区底色（见 applyTitleBarAppearanceToWindow）：clearColor
            // 变更（主题深浅切换 / 窗口效果切档改 alpha）**必须**重刷标题栏，否则标题栏停在
            // 上一档的颜色与透明度。上面那条 titleBarAppearance 变更检测看不出这种情况——
            // 宿主切窗口效果档只改 clearColor 的 alpha，TitleBarAppearance 的值一字未变。
            applyTitleBarAppearanceToWindow(window);
            childWindows.updateAll([](ManagedWindow& managed) {
                applyTitleBarAppearanceToWindow(managed.window);
            });
#endif
        }

        // 窗口效果联动（磨砂 Phase C）：app::setWindowEffect 的变更在下一帧应用到
        // 主窗口 + 全部存活子窗口（新子窗口在创建时已应用当前值）；实际生效值
        //（降级后）回写 app::activeWindowEffect()，供设置页回查降级并提示。
        // 逐窗覆盖（桌宠 sprite 窗的 None）不跟随全局广播。
        if (app::currentWindowEffect() != appliedWindowEffectDesired) {
            appliedWindowEffectDesired = app::currentWindowEffect();
            const core::platform::WindowEffect effective =
                applyWindowEffectToWindow(window, appliedWindowEffectDesired);
            childWindows.updateAll([](ManagedWindow& managed) {
                applyWindowEffectToWindow(
                    managed.window, desiredChildWindowEffect(managed.content.request()));
            });
            app::detail::setActiveWindowEffect(effective);
            EUI_FX_DIAG("[fxdiag] runtime change: desired=%d mainActive=%d (after children sync)\n",
                        static_cast<int>(appliedWindowEffectDesired), static_cast<int>(effective));
        }

        const double currentFrameTime = glfwGetTime();

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth <= 0 || framebufferHeight <= 0) {
            renderBackend->releaseRenderCache();
            windowState.paintRequested = true;
            app::detail::requestFullPaint();
            windowState.consumeFrameRequest();
            glfwWaitEvents();
            mainWindowRuntime.markUnavailableFrame(glfwGetTime());
            continue;
        }

        const float dpiScale = getDpiScale(window);
        const float pointerScale = getPointerScale(window);
        const bool mainInputEnabled = windowState.modalChildWindow == nullptr;

        // 布局度量（每秒一行，EUI_FX_DEBUG=1）：屏幕尺寸折算链的各环节真值
#if defined(__APPLE__)
        if (fxDebugEnabled()) {
            static double fxLastLayoutDump = -1.0;
            if (fxLastLayoutDump < 0.0 || currentFrameTime - fxLastLayoutDump >= 1.0) {
                fxLastLayoutDump = currentFrameTime;
                int winW = 0;
                int winH = 0;
                float csX = 1.0f;
                float csY = 1.0f;
                float monX = 1.0f;
                float monY = 1.0f;
                glfwGetWindowSize(window, &winW, &winH);
                glfwGetWindowContentScale(window, &csX, &csY);
                GLFWmonitor* monitor = glfwGetWindowMonitor(window);
                if (monitor == nullptr) {
                    monitor = glfwGetPrimaryMonitor();
                }
                if (monitor != nullptr) {
                    glfwGetMonitorContentScale(monitor, &monX, &monY);
                }
                const eui::Color fxClear = app::currentClearColor();
                std::fprintf(
                    stderr,
                    "[layoutdiag] frame windowSize=(%d,%d) framebuffer=(%d,%d) contentScale=(%.3f,%.3f) "
                    "monitorContentScale=(%.3f,%.3f) scaleToMonitor=%d dpiScale=%.4f pointerScale=%.4f "
                    "uiScale=%.4f logical=(%.2f,%.2f) clearColor=(%.3f,%.3f,%.3f,%.3f) "
                    "effectDesired=%d effectActive=%d transparentFbAttrib=%d\n",
                    winW,
                    winH,
                    framebufferWidth,
                    framebufferHeight,
                    static_cast<double>(csX),
                    static_cast<double>(csY),
                    static_cast<double>(monX),
                    static_cast<double>(monY),
                    glfwGetWindowAttrib(window, GLFW_SCALE_TO_MONITOR),
                    static_cast<double>(dpiScale),
                    static_cast<double>(pointerScale),
                    static_cast<double>(app::uiScale()),
                    static_cast<double>(framebufferWidth) / dpiScale,
                    static_cast<double>(framebufferHeight) / dpiScale,
                    static_cast<double>(fxClear.r),
                    static_cast<double>(fxClear.g),
                    static_cast<double>(fxClear.b),
                    static_cast<double>(fxClear.a),
                    static_cast<int>(app::currentWindowEffect()),
                    static_cast<int>(app::activeWindowEffect()),
                    glfwGetWindowAttrib(window, GLFW_TRANSPARENT_FRAMEBUFFER));
                std::fflush(stderr);
            }
        }
#endif

        mainWindowRuntime.runFrame(
            window,
            *renderBackend,
            {framebufferWidth, framebufferHeight, dpiScale, pointerScale},
            currentFrameTime,
            getWindowRefreshRate(window),
            mainInputEnabled,
            [&] {
                createRequestedWindows(childWindows, window, *renderBackend, app::consumeWindowRequests());
                pruneClosedWindows(childWindows);
                windowState.modalChildWindow = findModalChildWindow(childWindows);
            },
            [&](float frameDelta, bool updateRequested) {
                childWindows.updateAll([&](ManagedWindow& managed) {
                    updateManagedWindow(managed, frameDelta, updateRequested);
                });

                createRequestedWindows(childWindows, window, *renderBackend, app::consumeWindowRequests());
                pruneClosedWindows(childWindows);
                windowState.modalChildWindow = findModalChildWindow(childWindows);
            },
            [&](const char* title) {
                glfwSetWindowTitle(window, title);
            },
            [&] {
                return anyRenderableManagedWindowAnimating(childWindows);
            });

        const bool anyAnimating = windowState.anyAnimating(anyRenderableManagedWindowAnimating(childWindows));
        if (anyAnimating) {
            glfwPollEvents();
        } else {
            glfwWaitEvents();
        }
    }

    childWindows.destroyAll(destroyManagedWindow);
    // 电源 hook 先于输入回调拆除（IME 桥的卸载是盲恢复自己的前级，会连带
    // 摘掉压在其上的本 hook——顺序反过来时依赖 uninstall 内的链顶比对守卫）
    core::platform::uninstallSystemPowerNotifications();
    core::releaseInputQueue(window);
    renderBackend->makeCurrent();
    renderBackend->releaseRenderCache();
    core::platform::shutdownTray();
    {
        core::render::ScopedRenderBackend scopedRenderBackend(*renderBackend);
        app::shutdown();
    }
    renderBackend.reset();
    glfwTerminate();
    return 0;
}

#ifndef EUI_APP_RUNNER_LIBRARY
int main() {
    return eui_app_run();
}
#endif
