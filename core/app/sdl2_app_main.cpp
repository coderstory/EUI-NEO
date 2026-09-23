#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#if defined(EUI_RENDER_BACKEND_VULKAN)
#include <SDL_vulkan.h>
#endif

#include "eui/app.h"
#include "eui/detail/dsl_app_impl.h"
#include "core/app/app_runner.h"
#include "core/app/dsl_window_manager.h"
#include "core/app/dsl_window_runtime.h"
#include "core/app/main_window_runtime.h"
#include "core/input/input_state.h"
#include "core/platform/platform.h"
#include "core/platform/native_bridge.h"
#include "core/platform/window_style.h"
#include "core/render/render_backend.h"
#include "core/window/window_backend.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace {

struct WindowState : app::AppRunner {
    bool running = true;
    bool hideToTrayRequested = false;
    core::render::RenderBackend* renderBackend = nullptr;
#if defined(EUI_RENDER_BACKEND_OPENGL) && (defined(_WIN32) || defined(__APPLE__))
    // SDL2 can expose the OpenGL window before the drawable/backbuffer settles.
    int startupFullPaintFrames = 4;
#endif
};

struct ManagedWindow {
    SDL_Window* window = nullptr;
    bool closeRequested = false;
    SDL_Window* parentWindow = nullptr;
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

void getDrawableSize(SDL_Window* window, int& width, int& height) {
#if defined(EUI_RENDER_BACKEND_VULKAN)
    SDL_Vulkan_GetDrawableSize(window, &width, &height);
#else
    SDL_GL_GetDrawableSize(window, &width, &height);
#endif
}

float pointerScale(SDL_Window* window) {
    int windowWidth = 0;
    int windowHeight = 0;
    int drawableWidth = 0;
    int drawableHeight = 0;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    getDrawableSize(window, drawableWidth, drawableHeight);
    if (windowWidth <= 0 || windowHeight <= 0) {
        return 1.0f;
    }
    const float scaleX = static_cast<float>(drawableWidth) / static_cast<float>(windowWidth);
    const float scaleY = static_cast<float>(drawableHeight) / static_cast<float>(windowHeight);
    return (scaleX + scaleY) * 0.5f;
}

double refreshRate(SDL_Window* window) {
    const int display = SDL_GetWindowDisplayIndex(window);
    SDL_DisplayMode mode{};
    if (display >= 0 && SDL_GetCurrentDisplayMode(display, &mode) == 0 && mode.refresh_rate > 0) {
        return static_cast<double>(mode.refresh_rate);
    }
    return 60.0;
}

float dpiScale(SDL_Window* window) {
#ifdef _WIN32
    HWND hwnd = static_cast<HWND>(core::window::nativeWindowInfo(window).platformWindow);
    if (hwnd != nullptr) {
        using GetDpiForWindowFunction = UINT(WINAPI*)(HWND);
        static const GetDpiForWindowFunction getDpiForWindow = [] {
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            return user32 != nullptr
                ? reinterpret_cast<GetDpiForWindowFunction>(
                      GetProcAddress(user32, "GetDpiForWindow"))
                : nullptr;
        }();
        if (getDpiForWindow != nullptr) {
            const UINT dpi = getDpiForWindow(hwnd);
            if (dpi > 0) {
                return static_cast<float>(dpi) / 96.0f;
            }
        }
    }
#endif
    const float drawableRatio = pointerScale(window);
    if (drawableRatio > 1.0f) {
        return drawableRatio;
    }
    const float x11Scale = core::window::x11ContentScale(window);
    return x11Scale > 0.0f ? x11Scale : drawableRatio;
}

void attachNativeChildWindow(SDL_Window* parentWindow, SDL_Window* childWindow) {
    eui_set_native_child_window(core::window::nativeWindowInfo(parentWindow).platformWindow,
                                core::window::nativeWindowInfo(childWindow).platformWindow,
                                1);
}

void detachNativeChildWindow(SDL_Window* parentWindow, SDL_Window* childWindow) {
    eui_set_native_child_window(core::window::nativeWindowInfo(parentWindow).platformWindow,
                                core::window::nativeWindowInfo(childWindow).platformWindow,
                                0);
}

void updateFrameInterval(SDL_Window* window, WindowState& state) {
    state.updateFrameInterval(refreshRate(window), core::window::timeSeconds());
}

Uint32 windowIdForEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_WINDOWEVENT: return event.window.windowID;
    case SDL_KEYDOWN:
    case SDL_KEYUP: return event.key.windowID;
    case SDL_TEXTINPUT: return event.text.windowID;
    case SDL_TEXTEDITING: return event.edit.windowID;
    case SDL_MOUSEWHEEL: return event.wheel.windowID;
    case SDL_MOUSEMOTION: return event.motion.windowID;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: return event.button.windowID;
    default: return 0;
    }
}

core::InputKey mapKey(SDL_Keycode key) {
    if (key >= SDLK_0 && key <= SDLK_9) {
        return static_cast<core::InputKey>(
            static_cast<int>(core::InputKey::Digit0) + key - SDLK_0);
    }
    if (key >= SDLK_a && key <= SDLK_z) {
        return static_cast<core::InputKey>(
            static_cast<int>(core::InputKey::A) + key - SDLK_a);
    }
    if (key >= SDLK_F1 && key <= SDLK_F24) {
        return static_cast<core::InputKey>(
            static_cast<int>(core::InputKey::F1) + key - SDLK_F1);
    }
    switch (key) {
    case SDLK_BACKSPACE: return core::InputKey::Backspace;
    case SDLK_TAB: return core::InputKey::Tab;
    case SDLK_RETURN: return core::InputKey::Enter;
    case SDLK_ESCAPE: return core::InputKey::Escape;
    case SDLK_SPACE: return core::InputKey::Space;
    case SDLK_INSERT: return core::InputKey::Insert;
    case SDLK_DELETE: return core::InputKey::Delete;
    case SDLK_HOME: return core::InputKey::Home;
    case SDLK_END: return core::InputKey::End;
    case SDLK_PAGEUP: return core::InputKey::PageUp;
    case SDLK_PAGEDOWN: return core::InputKey::PageDown;
    case SDLK_LEFT: return core::InputKey::Left;
    case SDLK_RIGHT: return core::InputKey::Right;
    case SDLK_UP: return core::InputKey::Up;
    case SDLK_DOWN: return core::InputKey::Down;
    case SDLK_PRINTSCREEN: return core::InputKey::PrintScreen;
    case SDLK_SCROLLLOCK: return core::InputKey::ScrollLock;
    case SDLK_PAUSE: return core::InputKey::Pause;
    case SDLK_CAPSLOCK: return core::InputKey::CapsLock;
    case SDLK_NUMLOCKCLEAR: return core::InputKey::NumLock;
    case SDLK_LSHIFT: return core::InputKey::LeftShift;
    case SDLK_RSHIFT: return core::InputKey::RightShift;
    case SDLK_LCTRL: return core::InputKey::LeftControl;
    case SDLK_RCTRL: return core::InputKey::RightControl;
    case SDLK_LALT: return core::InputKey::LeftAlt;
    case SDLK_RALT: return core::InputKey::RightAlt;
    case SDLK_LGUI: return core::InputKey::LeftSuper;
    case SDLK_RGUI: return core::InputKey::RightSuper;
    case SDLK_MENU: return core::InputKey::Menu;
    case SDLK_QUOTE: return core::InputKey::Apostrophe;
    case SDLK_COMMA: return core::InputKey::Comma;
    case SDLK_MINUS: return core::InputKey::Minus;
    case SDLK_PERIOD: return core::InputKey::Period;
    case SDLK_SLASH: return core::InputKey::Slash;
    case SDLK_SEMICOLON: return core::InputKey::Semicolon;
    case SDLK_EQUALS: return core::InputKey::Equal;
    case SDLK_LEFTBRACKET: return core::InputKey::LeftBracket;
    case SDLK_BACKSLASH: return core::InputKey::Backslash;
    case SDLK_RIGHTBRACKET: return core::InputKey::RightBracket;
    case SDLK_BACKQUOTE: return core::InputKey::GraveAccent;
    case SDLK_KP_0: return core::InputKey::Numpad0;
    case SDLK_KP_1: return core::InputKey::Numpad1;
    case SDLK_KP_2: return core::InputKey::Numpad2;
    case SDLK_KP_3: return core::InputKey::Numpad3;
    case SDLK_KP_4: return core::InputKey::Numpad4;
    case SDLK_KP_5: return core::InputKey::Numpad5;
    case SDLK_KP_6: return core::InputKey::Numpad6;
    case SDLK_KP_7: return core::InputKey::Numpad7;
    case SDLK_KP_8: return core::InputKey::Numpad8;
    case SDLK_KP_9: return core::InputKey::Numpad9;
    case SDLK_KP_DECIMAL: return core::InputKey::NumpadDecimal;
    case SDLK_KP_DIVIDE: return core::InputKey::NumpadDivide;
    case SDLK_KP_MULTIPLY: return core::InputKey::NumpadMultiply;
    case SDLK_KP_MINUS: return core::InputKey::NumpadSubtract;
    case SDLK_KP_PLUS: return core::InputKey::NumpadAdd;
    case SDLK_KP_ENTER: return core::InputKey::NumpadEnter;
    case SDLK_KP_EQUALS: return core::InputKey::NumpadEqual;
    default: return core::InputKey::Unknown;
    }
}

core::KeyModifiers keyModifiers(SDL_Keymod state) {
    core::KeyModifiers modifiers;
    modifiers.control = (state & KMOD_CTRL) != 0;
    modifiers.shift = (state & KMOD_SHIFT) != 0;
    modifiers.alt = (state & KMOD_ALT) != 0;
    modifiers.super = (state & KMOD_GUI) != 0;
    modifiers.capsLock = (state & KMOD_CAPS) != 0;
    modifiers.numLock = (state & KMOD_NUM) != 0;
    return modifiers;
}

core::PointerButtons pointerButtons(Uint32 state) {
    core::PointerButtons buttons;
    buttons.set(core::PointerButton::Left, (state & SDL_BUTTON_LMASK) != 0);
    buttons.set(core::PointerButton::Middle, (state & SDL_BUTTON_MMASK) != 0);
    buttons.set(core::PointerButton::Right, (state & SDL_BUTTON_RMASK) != 0);
    buttons.set(core::PointerButton::X1, (state & SDL_BUTTON_X1MASK) != 0);
    buttons.set(core::PointerButton::X2, (state & SDL_BUTTON_X2MASK) != 0);
    return buttons;
}

core::PointerButton pointerButton(Uint8 button) {
    switch (button) {
    case SDL_BUTTON_LEFT: return core::PointerButton::Left;
    case SDL_BUTTON_MIDDLE: return core::PointerButton::Middle;
    case SDL_BUTTON_RIGHT: return core::PointerButton::Right;
    case SDL_BUTTON_X1: return core::PointerButton::X1;
    case SDL_BUTTON_X2: return core::PointerButton::X2;
    default: return core::PointerButton::None;
    }
}

bool processInputEvent(SDL_Window* window,
                       const SDL_Event& event,
                       bool inputEnabled,
                       bool& repaintRequested) {
    repaintRequested = false;
    switch (event.type) {
    case SDL_MOUSEMOTION:
        core::queuePointerMotion(window,
                                 event.motion.x,
                                 event.motion.y,
                                 pointerButtons(event.motion.state),
                                 core::detail::currentModifiers(window));
        repaintRequested = inputEnabled;
        return true;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        if (const core::PointerButton button = pointerButton(event.button.button);
            button != core::PointerButton::None) {
            core::queuePointerButton(window,
                                     event.button.x,
                                     event.button.y,
                                     button,
                                     event.type == SDL_MOUSEBUTTONDOWN
                                          ? core::PointerAction::Press
                                          : core::PointerAction::Release,
                                     core::detail::currentModifiers(window));
        }
        repaintRequested = inputEnabled;
        return true;
    case SDL_TEXTINPUT:
        if (inputEnabled) {
            core::queueTextInput(window, event.text.text);
            repaintRequested = true;
        }
        return true;
    case SDL_TEXTEDITING:
        if (inputEnabled) {
            core::queueTextEditing(window, event.edit.text);
            repaintRequested = true;
        }
        return true;
    case SDL_MOUSEWHEEL:
        if (inputEnabled) {
            core::queueScrollInput(window, event.wheel.preciseX, event.wheel.preciseY);
            repaintRequested = true;
        }
        return true;
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        if (inputEnabled) {
            const core::KeyAction action = event.type == SDL_KEYUP
                ? core::KeyAction::Release
                : (event.key.repeat != 0 ? core::KeyAction::Repeat : core::KeyAction::Press);
            core::queueKeyInput(window, {
                mapKey(event.key.keysym.sym),
                action,
                keyModifiers(static_cast<SDL_Keymod>(event.key.keysym.mod)),
                static_cast<int>(event.key.keysym.scancode)
            });
            repaintRequested = true;
        }
        return true;
    }
    case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_ENTER) {
            core::queuePointerPresence(window, true);
            repaintRequested = inputEnabled;
            return true;
        }
        if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            core::detail::inputQueue(window).modifiers = keyModifiers(SDL_GetModState());
            return false;
        }
        if (event.window.event == SDL_WINDOWEVENT_LEAVE) {
            core::queuePointerPresence(window, false);
            repaintRequested = inputEnabled;
            return true;
        }
        if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
            event.window.event == SDL_WINDOWEVENT_HIDDEN ||
            event.window.event == SDL_WINDOWEVENT_MINIMIZED ||
            event.window.event == SDL_WINDOWEVENT_CLOSE) {
            core::cancelInput(window);
        }
        return false;
    default:
        return false;
    }
}

void hideToTray(SDL_Window* window, WindowState& state) {
    if (!state.trayAvailable || state.hiddenToTray) {
        return;
    }
    if (state.renderBackend != nullptr) {
        state.renderBackend->makeCurrent();
        state.renderBackend->releaseRenderCache();
        core::render::ScopedRenderBackend scopedRenderBackend(*state.renderBackend);
        app::releaseGraphicsResources();
    } else {
        app::releaseGraphicsResources();
    }
    SDL_HideWindow(window);
    state.hiddenToTray = true;
    state.hideToTrayRequested = false;
    state.paintRequested = false;
    state.resetTiming(core::window::timeSeconds());
}

void restoreFromTray(SDL_Window* window, WindowState& state) {
    if (!state.hiddenToTray) {
        return;
    }
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    state.hiddenToTray = false;
    state.hideToTrayRequested = false;
    state.paintRequested = true;
    app::detail::requestFullPaint();
    state.resetTiming(core::window::timeSeconds());
}

void requestClose(WindowState& state) {
    if (state.trayAvailable) {
        state.hideToTrayRequested = true;
    } else {
        state.running = false;
    }
}

void processMainEvent(SDL_Window* window, WindowState& state, const SDL_Event& event, bool inputEnabled) {
    if (event.type == SDL_QUIT) {
        requestClose(state);
        return;
    }
    bool repaintRequested = false;
    if (processInputEvent(window, event, inputEnabled, repaintRequested)) {
        if (repaintRequested) {
            state.paintRequested = true;
        }
        return;
    }
    if (event.type == SDL_WINDOWEVENT) {
        switch (event.window.event) {
        case SDL_WINDOWEVENT_CLOSE:
            if (inputEnabled) {
                requestClose(state);
            }
            break;
        case SDL_WINDOWEVENT_MINIMIZED:
            break;
        case SDL_WINDOWEVENT_EXPOSED:
        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        case SDL_WINDOWEVENT_SHOWN:
        case SDL_WINDOWEVENT_RESTORED:
            state.paintRequested = true;
            app::detail::requestFullPaint();
            break;
        default:
            break;
        }
    }
}

std::unique_ptr<ManagedWindow> createManagedWindow(const app::DslWindowRequest& request,
                                                   SDL_Window* parentWindow,
                                                   core::render::RenderBackend& shareBackend) {
    core::window::WindowCreateRequest windowRequest;
    windowRequest.width = request.width;
    windowRequest.height = request.height;
    windowRequest.title = request.title.c_str();
    windowRequest.parent = parentWindow;
    windowRequest.renderApi = core::render::windowRenderApi();
    windowRequest.windowEffect = app::windowEffect();
    // 子窗口配置透传（桌宠设计 §2.6 G1/G4；focusOnShow 在 SDL2 后端无对应能力）
    windowRequest.x = request.x;
    windowRequest.y = request.y;
    windowRequest.positionSet = request.positionSet;
    windowRequest.decorated = request.decorated;
    windowRequest.alwaysOnTop = request.alwaysOnTop;
    windowRequest.resizable = request.resizable;
    windowRequest.mousePassthrough = request.mousePassthrough;
    SDL_Window* window = static_cast<SDL_Window*>(core::window::createWindow(windowRequest));
    if (window == nullptr) {
        return {};
    }

    auto managed = std::make_unique<ManagedWindow>();
    managed->window = window;
    managed->parentWindow = parentWindow;
    managed->renderBackend = core::render::createRenderBackend(window, &shareBackend);
    if (!managed->renderBackend) {
        core::window::destroyWindow(window);
        return {};
    }
    if (!managed->renderBackend->initialize()) {
        core::window::destroyWindow(window);
        return {};
    }
    if (!managed->content.initialize(window, request)) {
        managed->renderBackend.reset();
        core::window::destroyWindow(window);
        return {};
    }
    // clearColor 运行时覆盖（app::setClearColor）对后续新开子窗口同样生效；
    // 自管背景的窗口（ignoreClearColorOverride，如桌宠 sprite 窗）除外
    if (request.followClearColorOverride) {
        if (const std::optional<eui::Color>& override_ = app::detail::clearColorOverride()) {
            managed->content.setClearColor(*override_);
        }
    }
    // 任务栏隐藏（桌宠设计 G5）：nativeWindowInfo 给出平台原生句柄
    if (request.hideFromTaskbar) {
        core::platform::applyWindowStyleFlags(
            core::window::nativeWindowInfo(window).platformWindow,
            core::platform::WindowStyleFlags{true, false});
    }
    if (request.onWindowCreated) {
        request.onWindowCreated(window);
    }
    if (request.modal) {
        SDL_SetWindowModalFor(window, parentWindow);
        attachNativeChildWindow(parentWindow, window);
        SDL_RaiseWindow(window);
    }
    return managed;
}

void destroyManagedWindow(std::unique_ptr<ManagedWindow>& managed) {
    if (!managed) {
        return;
    }
    if (managed->window != nullptr && managed->renderBackend != nullptr) {
        // 销毁回调须在 content.shutdown() 前取出（shutdown 会清空 request_ 快照）
        std::function<void(core::window::Handle)> onWindowDestroyed =
            managed->content.request().onWindowDestroyed;
        if (managed->content.request().modal && managed->parentWindow != nullptr) {
            detachNativeChildWindow(managed->parentWindow, managed->window);
        }
        managed->renderBackend->makeCurrent();
        if (managed->renderBackend) {
            managed->renderBackend->releaseRenderCache();
        }
        core::releaseInputQueue(managed->window);
        if (managed->renderBackend) {
            core::render::ScopedRenderBackend scopedRenderBackend(*managed->renderBackend);
            managed->content.shutdown(false);
        } else {
            managed->content.shutdown(false);
        }
        managed->renderBackend.reset();
        // 宿主销毁通知（onWindowCreated 对称）：窗口销毁前触发，句柄此刻仍有效
        if (onWindowDestroyed) {
            onWindowDestroyed(managed->window);
        }
        core::window::destroyWindow(managed->window);
    }
    managed.reset();
}

bool isManagedWindowClosed(const ManagedWindow& managed) {
    return managed.closeRequested || managed.window == nullptr || managed.renderBackend == nullptr;
}

void pruneClosedWindows(app::DslWindowManager<ManagedWindow>& windows) {
    windows.pruneClosed(isManagedWindowClosed, destroyManagedWindow);
}

void createRequestedWindows(app::DslWindowManager<ManagedWindow>& windows,
                            SDL_Window* mainWindow,
                            core::render::RenderBackend& mainBackend,
                            const std::vector<app::DslWindowRequest>& requests) {
    mainBackend.makeCurrent();
    windows.createPending(requests, [&](const app::DslWindowRequest& request) {
        std::unique_ptr<ManagedWindow> managed = createManagedWindow(request, mainWindow, mainBackend);
        mainBackend.makeCurrent();
        return managed;
    });
}

ManagedWindow* findWindow(app::DslWindowManager<ManagedWindow>& windows, Uint32 windowId) {
    return windows.find([&](const ManagedWindow& managed) {
        return managed.window != nullptr && SDL_GetWindowID(managed.window) == windowId;
    });
}

ManagedWindow* findModalWindow(app::DslWindowManager<ManagedWindow>& windows) {
    return windows.modalWindow(isManagedWindowClosed);
}

void processManagedEvent(ManagedWindow& managed, const SDL_Event& event) {
    bool repaintRequested = false;
    if (processInputEvent(managed.window, event, true, repaintRequested)) {
        if (repaintRequested) {
            managed.content.requestPaint();
        }
        return;
    }
    if (event.type == SDL_WINDOWEVENT) {
        if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
            managed.closeRequested = true;
        } else if (event.window.event == SDL_WINDOWEVENT_EXPOSED ||
                   event.window.event == SDL_WINDOWEVENT_RESIZED ||
                   event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                   event.window.event == SDL_WINDOWEVENT_SHOWN ||
                   event.window.event == SDL_WINDOWEVENT_RESTORED) {
            managed.content.requestFullPaint();
        }
    }
}

bool updateManagedWindow(ManagedWindow& managed, float deltaSeconds, bool updateRequested) {
    if (managed.closeRequested || managed.window == nullptr || managed.renderBackend == nullptr) {
        return false;
    }

    managed.renderBackend->makeCurrent();
    int drawableWidth = 0;
    int drawableHeight = 0;
    getDrawableSize(managed.window, drawableWidth, drawableHeight);
    if (drawableWidth <= 0 || drawableHeight <= 0) {
        managed.renderBackend->releaseRenderCache();
        managed.content.requestFullPaint();
        return true;
    }
    const float dpi = dpiScale(managed.window);
    const float pointer = pointerScale(managed.window);
    const float logicalWidth = static_cast<float>(drawableWidth) / dpi;
    const float logicalHeight = static_cast<float>(drawableHeight) / dpi;

    core::render::ScopedRenderBackend scopedRenderBackend(*managed.renderBackend);
    managed.content.update(managed.window, deltaSeconds, logicalWidth, logicalHeight, pointer, dpi, updateRequested);
    if (managed.content.paintRequested()) {
        managed.renderBackend->beginFrame({
            managed.window,
            core::window::nativeWindowInfo(managed.window),
            drawableWidth,
            drawableHeight,
            dpi
        });
        managed.content.render(*managed.renderBackend, drawableWidth, drawableHeight, dpi);
        managed.renderBackend->present();
    }
    return true;
}

} // namespace

int eui_app_run() {
    core::platform::repairCurrentWorkingDirectory();
    // 必须在 SDL_Init() 之前：macOS 上启动期的「Finder 双击打开」事件
    // 可能早于事件循环到达，晚装会丢事件（其他平台为空操作）。
    core::platform::installOpenFileHandler();
    SDL_SetMainReady();
#if defined(__linux__) && !defined(__ANDROID__)
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "wayland,x11");
#endif
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
    SDL_SetHint(SDL_HINT_IME_INTERNAL_EDITING, "1");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
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
    SDL_Window* window = static_cast<SDL_Window*>(core::window::createWindow(windowRequest));
    if (window == nullptr) {
        SDL_Quit();
        return -1;
    }
    // clearColor 联动基线（app::setClearColor 变更后广播到子窗口；主窗口在
    // app::render 里直接读覆盖值）
    eui::Color appliedClearColor = app::currentClearColor();
    // 窗口效果（磨砂 Phase C）：SDL2 后端无逐像素透明 flag，window_backend 已把
    // windowEffect 按 None 降级——如实回写，activeWindowEffect() 不虚报档位。
    app::detail::setActiveWindowEffect(core::platform::WindowEffect::None);

    auto renderBackend = core::render::createRenderBackend(window);
    if (!renderBackend) {
        core::window::destroyWindow(window);
        SDL_Quit();
        return -1;
    }
    if (!renderBackend->initialize()) {
        core::window::destroyWindow(window);
        SDL_Quit();
        return -1;
    }

    if (!app::initialize(window)) {
        app::shutdown();
        renderBackend.reset();
        core::window::destroyWindow(window);
        SDL_Quit();
        return -1;
    }
    SDL_StartTextInput();

    WindowState state;
    state.resetTiming(core::window::timeSeconds());
    updateFrameInterval(window, state);
    state.initializeTray();
    state.renderBackend = renderBackend.get();
    app::MainWindowRuntime mainWindowRuntime(state);

    app::DslWindowManager<ManagedWindow> childWindows;
    while (state.running) {
        state.pollTray(false);
        // 「打开文件」请求（macOS kAEOpenDocuments Apple Event）在事件泵里入队，
        // 这里在帧开始前派发。
        core::platform::pollOpenFiles();
        if (state.consumeTrayExitRequested()) {
            break;
        }
        // app::requestExit（子窗口「退出」菜单等场景）：与托盘 Exit 同一退出路径
        if (app::detail::consumeExitRequest()) {
            break;
        }
        if (state.consumeTrayShowRequested()) {
            restoreFromTray(window, state);
        }
        if (state.hiddenToTray) {
            SDL_Event event{};
            if (SDL_WaitEventTimeout(&event, 100)) {
                processMainEvent(window, state, event, true);
            }
            state.resetTiming(core::window::timeSeconds());
            continue;
        }

        ManagedWindow* modalWindow = findModalWindow(childWindows);
        const bool animating = state.anyAnimating(childWindows.anyAnimating());
        if (!animating) {
            SDL_Event event{};
            if (SDL_WaitEventTimeout(&event, 100)) {
                const Uint32 eventWindowId = windowIdForEvent(event);
                if (eventWindowId != 0) {
                    if (ManagedWindow* managed = findWindow(childWindows, eventWindowId)) {
                        processManagedEvent(*managed, event);
                    } else {
                        processMainEvent(window, state, event, modalWindow == nullptr);
                    }
                } else {
                    processMainEvent(window, state, event, modalWindow == nullptr);
                }
            }
        } else {
            const double remaining = state.nextFrameTime - core::window::timeSeconds();
            if (remaining > 0.001) {
                std::this_thread::sleep_for(std::chrono::duration<double>(remaining * 0.75));
            }
        }

        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            const Uint32 eventWindowId = windowIdForEvent(event);
            if (eventWindowId != 0) {
                if (ManagedWindow* managed = findWindow(childWindows, eventWindowId)) {
                    processManagedEvent(*managed, event);
                } else {
                    processMainEvent(window, state, event, findModalWindow(childWindows) == nullptr);
                }
            } else {
                processMainEvent(window, state, event, findModalWindow(childWindows) == nullptr);
            }
        }
        pruneClosedWindows(childWindows);
        // clearColor 联动（磨砂 Phase B）：app::setClearColor 的变更广播到全部
        // 存活子窗口（各自 requestFullPaint）；主窗口已由 setClearColor 触发全量重绘。
        const eui::Color effectiveClearColor = app::currentClearColor();
        if (effectiveClearColor.r != appliedClearColor.r || effectiveClearColor.g != appliedClearColor.g ||
            effectiveClearColor.b != appliedClearColor.b || effectiveClearColor.a != appliedClearColor.a) {
            appliedClearColor = effectiveClearColor;
            childWindows.updateAll([&effectiveClearColor](ManagedWindow& managed) {
                if (!managed.content.request().followClearColorOverride) {
                    return;
                }
                managed.content.setClearColor(effectiveClearColor);
            });
        }
        if (state.hideToTrayRequested && !childWindows.empty()) {
            state.hideToTrayRequested = false;
        }
        if (state.hideToTrayRequested) {
            hideToTray(window, state);
        }
        if (!state.running || state.hiddenToTray) {
            continue;
        }

        const double now = core::window::timeSeconds();

        int drawableWidth = 0;
        int drawableHeight = 0;
        getDrawableSize(window, drawableWidth, drawableHeight);
        if (drawableWidth <= 0 || drawableHeight <= 0) {
            renderBackend->releaseRenderCache();
            app::detail::requestFullPaint();
            mainWindowRuntime.markUnavailableFrame(core::window::timeSeconds());
            continue;
        }
        const float dpi = dpiScale(window);
        const float pointer = pointerScale(window);
#if defined(EUI_RENDER_BACKEND_OPENGL) && (defined(_WIN32) || defined(__APPLE__))
        if (state.startupFullPaintFrames > 0) {
            state.paintRequested = true;
            app::detail::requestFullPaint();
            --state.startupFullPaintFrames;
        }
#endif
        mainWindowRuntime.runFrame(
            window,
            *renderBackend,
            {drawableWidth, drawableHeight, dpi, pointer},
            now,
            refreshRate(window),
            findModalWindow(childWindows) == nullptr,
            [&] {
                createRequestedWindows(childWindows, window, *renderBackend, app::consumeWindowRequests());
            },
            [&](float frameDelta, bool updateRequested) {
                childWindows.updateAll([&](ManagedWindow& managed) {
                    updateManagedWindow(managed, frameDelta, updateRequested);
                });
                createRequestedWindows(childWindows, window, *renderBackend, app::consumeWindowRequests());
            },
            [&](const char* title) {
                SDL_SetWindowTitle(window, title);
            },
            [&] {
                return childWindows.anyAnimating();
            });
    }

    childWindows.destroyAll(destroyManagedWindow);
    core::releaseInputQueue(window);
    core::platform::shutdownTray();
    renderBackend->makeCurrent();
    renderBackend->releaseRenderCache();
    {
        core::render::ScopedRenderBackend scopedRenderBackend(*renderBackend);
        app::shutdown();
    }
    renderBackend.reset();
    SDL_StopTextInput();
    core::window::destroyWindow(window);
    SDL_Quit();
    return 0;
}

#ifndef EUI_APP_RUNNER_LIBRARY
int main() {
    return eui_app_run();
}
#endif
