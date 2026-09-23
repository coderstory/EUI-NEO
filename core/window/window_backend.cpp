#include "core/window/window_backend.h"
#include "core/platform/native_bridge.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#if defined(EUI_WINDOW_BACKEND_SDL2)

#include <SDL.h>
#if defined(__linux__) && !defined(__ANDROID__) && defined(SDL_VIDEO_DRIVER_X11)
#include <SDL_syswm.h>
#include <X11/Xresource.h>
#endif
#ifdef None
#undef None
#endif
#ifdef Bool
#undef Bool
#endif
#ifdef Status
#undef Status
#endif
#ifdef CursorShape
#undef CursorShape
#endif
#ifdef Success
#undef Success
#endif
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <imm.h>

#include <new>
#include <unordered_map>
#endif
#if defined(_WIN32) || defined(__APPLE__)
#include <SDL_syswm.h>
#endif
namespace core::window {

namespace {

void configureOpenGLWindowAttributes() {
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
}

#if defined(_WIN32)

struct SdlImeFilterState {
    WNDPROC previousProc = nullptr;
    SDL_Rect rect{};
    bool hasRect = false;
    bool applying = false;
    UINT_PTR reapplyTimer = 0;
};

std::unordered_map<HWND, SdlImeFilterState*> gSdlImeFilters;

SdlImeFilterState* sdlImeState(HWND hwnd) {
    const auto iterator = gSdlImeFilters.find(hwnd);
    return iterator != gSdlImeFilters.end() ? iterator->second : nullptr;
}

HWND hwndForSdlWindow(SDL_Window* window) {
    if (window == nullptr) {
        return nullptr;
    }

    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info) != SDL_TRUE ||
        info.subsystem != SDL_SYSWM_WINDOWS) {
        return nullptr;
    }
    return info.info.win.window;
}

void applySdlImeRect(HWND hwnd, const SDL_Rect& rect) {
    HIMC context = ImmGetContext(hwnd);
    if (context == nullptr) {
        return;
    }

    LOGFONTW font{};
    const HFONT defaultFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    if (defaultFont != nullptr &&
        GetObjectW(defaultFont, sizeof(font), &font) == sizeof(font)) {
        font.lfHeight = -std::max(12, rect.h);
        font.lfQuality = CLEARTYPE_QUALITY;
        ImmSetCompositionFontW(context, &font);
    }

    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_FORCE_POSITION;
    composition.ptCurrentPos.x = rect.x;
    composition.ptCurrentPos.y = rect.y;
    composition.rcArea.left = rect.x;
    composition.rcArea.top = rect.y;
    composition.rcArea.right = rect.x + rect.w;
    composition.rcArea.bottom = rect.y + rect.h;
    ImmSetCompositionWindow(context, &composition);

    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_EXCLUDE;
    candidate.ptCurrentPos = composition.ptCurrentPos;
    candidate.rcArea = composition.rcArea;
    ImmSetCandidateWindow(context, &candidate);

    ImmReleaseContext(hwnd, context);
}

void reapplySdlImeRect(HWND hwnd, SdlImeFilterState* state) {
    if (state == nullptr || !state->hasRect || state->applying) {
        return;
    }

    state->applying = true;
    applySdlImeRect(hwnd, state->rect);
    if (sdlImeState(hwnd) == state) {
        state->applying = false;
    }
}

LRESULT CALLBACK sdlImeWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = sdlImeState(hwnd);
    if (message == WM_TIMER && state != nullptr &&
        state->reapplyTimer != 0 && wParam == state->reapplyTimer) {
        KillTimer(hwnd, state->reapplyTimer);
        state->reapplyTimer = 0;
        reapplySdlImeRect(hwnd, state);
        return 0;
    }

    const bool placementChanged = message == WM_IME_STARTCOMPOSITION ||
        message == WM_IME_COMPOSITION ||
        (message == WM_IME_NOTIFY &&
         (wParam == IMN_OPENCANDIDATE || wParam == IMN_CHANGECANDIDATE));

    if (placementChanged) {
        reapplySdlImeRect(hwnd, state);
    }

    const LRESULT result = state != nullptr && state->previousProc != nullptr
        ? CallWindowProcW(state->previousProc, hwnd, message, wParam, lParam)
        : DefWindowProcW(hwnd, message, wParam, lParam);

    if (placementChanged) {
        state = sdlImeState(hwnd);
        reapplySdlImeRect(hwnd, state);
        if (state != nullptr) {
            if (state->reapplyTimer != 0) {
                KillTimer(hwnd, state->reapplyTimer);
            }
            state->reapplyTimer = SetTimer(hwnd, 0, USER_TIMER_MINIMUM, nullptr);
        }
    }
    return result;
}

void installSdlImeFilter(SDL_Window* window) {
    HWND hwnd = hwndForSdlWindow(window);
    if (hwnd == nullptr || sdlImeState(hwnd) != nullptr) {
        return;
    }

    auto* state = new (std::nothrow) SdlImeFilterState{};
    if (state == nullptr) {
        return;
    }
    state->previousProc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
    if (state->previousProc == nullptr) {
        delete state;
        return;
    }

    gSdlImeFilters.emplace(hwnd, state);
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(
        hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(sdlImeWindowProc));
    if (previous == 0 && GetLastError() != ERROR_SUCCESS) {
        gSdlImeFilters.erase(hwnd);
        delete state;
    }
}

void uninstallSdlImeFilter(SDL_Window* window) {
    HWND hwnd = hwndForSdlWindow(window);
    if (hwnd == nullptr) {
        return;
    }

    auto* state = sdlImeState(hwnd);
    if (state == nullptr) {
        return;
    }

    if (state->reapplyTimer != 0) {
        KillTimer(hwnd, state->reapplyTimer);
    }
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(state->previousProc));
    gSdlImeFilters.erase(hwnd);
    delete state;
}

#endif
#if defined(__linux__) && !defined(__ANDROID__) && defined(SDL_VIDEO_DRIVER_X11)
struct X11ResourceApi {
    using Initialize = void (*)();
    using ResourceManagerString = char* (*)(Display*);
    using GetStringDatabase = XrmDatabase (*)(const char*);
    using GetResource = int (*)(XrmDatabase, const char*, const char*, char**, XrmValue*);
    using DestroyDatabase = void (*)(XrmDatabase);

    void* library = nullptr;
    Initialize initialize = nullptr;
    ResourceManagerString resourceManagerString = nullptr;
    GetStringDatabase getStringDatabase = nullptr;
    GetResource getResource = nullptr;
    DestroyDatabase destroyDatabase = nullptr;

    bool available() const {
        return library != nullptr && initialize != nullptr &&
               resourceManagerString != nullptr && getStringDatabase != nullptr &&
               getResource != nullptr && destroyDatabase != nullptr;
    }
};

X11ResourceApi loadX11ResourceApi() {
    X11ResourceApi api;
    void* library = SDL_LoadObject("libX11.so.6");
    if (library == nullptr) {
        library = SDL_LoadObject("libX11.so");
    }
    if (library == nullptr) {
        return api;
    }

    api.library = library;
    api.initialize = reinterpret_cast<X11ResourceApi::Initialize>(
        SDL_LoadFunction(library, "XrmInitialize"));
    api.resourceManagerString = reinterpret_cast<X11ResourceApi::ResourceManagerString>(
        SDL_LoadFunction(library, "XResourceManagerString"));
    api.getStringDatabase = reinterpret_cast<X11ResourceApi::GetStringDatabase>(
        SDL_LoadFunction(library, "XrmGetStringDatabase"));
    api.getResource = reinterpret_cast<X11ResourceApi::GetResource>(
        SDL_LoadFunction(library, "XrmGetResource"));
    api.destroyDatabase = reinterpret_cast<X11ResourceApi::DestroyDatabase>(
        SDL_LoadFunction(library, "XrmDestroyDatabase"));
    if (!api.available()) {
        SDL_UnloadObject(library);
        return {};
    }
    return api;
}

const X11ResourceApi& x11ResourceApi() {
    static const X11ResourceApi api = loadX11ResourceApi();
    return api;
}
#endif


} // namespace


float x11ContentScale(Handle window) {
    SDL_Window* sdlWindow = static_cast<SDL_Window*>(window);
    if (sdlWindow == nullptr) {
        return 0.0f;
    }
#if defined(__linux__) && !defined(__ANDROID__) && defined(SDL_VIDEO_DRIVER_X11)
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(sdlWindow, &info) != SDL_TRUE ||
        info.subsystem != SDL_SYSWM_X11 ||
        info.info.x11.display == nullptr) {
        return 0.0f;
    }

    const X11ResourceApi& api = x11ResourceApi();
    if (!api.available()) {
        return 0.0f;
    }

    static Display* cachedDisplay = nullptr;
    static float cachedScale = 1.0f;
    if (cachedDisplay == info.info.x11.display) {
        return cachedScale;
    }
    cachedDisplay = info.info.x11.display;
    cachedScale = 1.0f;

    api.initialize();
    char* resources = api.resourceManagerString(info.info.x11.display);
    if (resources == nullptr) {
        return cachedScale;
    }
    XrmDatabase database = api.getStringDatabase(resources);
    if (database == nullptr) {
        return cachedScale;
    }

    char* type = nullptr;
    XrmValue value{};
    if (api.getResource(database, "Xft.dpi", "Xft.Dpi", &type, &value) &&
        value.addr != nullptr) {
        char* end = nullptr;
        const float dpi = std::strtof(value.addr, &end);
        if (end != value.addr && std::isfinite(dpi) && dpi > 0.0f) {
            cachedScale = dpi / 96.0f;
        }
    }
    api.destroyDatabase(database);
    return cachedScale;
#else
    (void)sdlWindow;
    return 0.0f;
#endif
}

Handle createWindow(const WindowCreateRequest& request) {
    if (request.renderApi == RenderApi::OpenGL) {
        configureOpenGLWindowAttributes();
    }

    Uint32 flags = 0;
    if (request.highDpi) {
        flags |= SDL_WINDOW_ALLOW_HIGHDPI;
    }
    if (!request.visible) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    if (request.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (!request.decorated) {
        flags |= SDL_WINDOW_BORDERLESS;
    }
    if (request.alwaysOnTop) {
        flags |= SDL_WINDOW_ALWAYS_ON_TOP;
    }
    if (request.maximized) {
        flags |= SDL_WINDOW_MAXIMIZED;
    }
    // 透明帧缓冲（磨砂设计 Phase B）：SDL2 无逐像素透明窗口 flag
    //（SDL_WINDOW_TRANSPARENT 是 SDL3 的；SDL2 仅整窗 SDL_SetWindowOpacity，
    // 语义不同）——设计允许降级，SDL2 后端对 windowEffect 按 None 处理。
    flags |= request.renderApi == RenderApi::Vulkan ? SDL_WINDOW_VULKAN : SDL_WINDOW_OPENGL;

    SDL_Window* window = SDL_CreateWindow(
        request.title != nullptr ? request.title : "",
        request.positionSet ? request.x : SDL_WINDOWPOS_CENTERED,
        request.positionSet ? request.y : SDL_WINDOWPOS_CENTERED,
        request.width,
        request.height,
        flags);
    if (window != nullptr) {
        if (request.minWidth > 0 || request.minHeight > 0) {
            SDL_SetWindowMinimumSize(window,
                                     request.minWidth > 0 ? request.minWidth : 1,
                                     request.minHeight > 0 ? request.minHeight : 1);
        }
        if (request.maxWidth > 0 || request.maxHeight > 0) {
            const int minimumWidth = request.minWidth > 0 ? request.minWidth : 1;
            const int minimumHeight = request.minHeight > 0 ? request.minHeight : 1;
            constexpr int unboundedSize = std::numeric_limits<int>::max() / 4;
            SDL_SetWindowMaximumSize(window,
                                     request.maxWidth > 0
                                         ? std::max(minimumWidth, request.maxWidth)
                                         : unboundedSize,
                                     request.maxHeight > 0
                                         ? std::max(minimumHeight, request.maxHeight)
                                         : unboundedSize);
        }
        // 鼠标穿透（G4）：SDL2 无创建期 flag，创建后立即按属性设置（2.26+）
        if (request.mousePassthrough) {
            setWindowMousePassthrough(window, true);
        }
    }
#if defined(__linux__) && !defined(__ANDROID__) && defined(SDL_VIDEO_DRIVER_X11)
    if (window != nullptr && request.highDpi) {
        const float scale = x11ContentScale(window);
        if (scale > 0.0f && scale != 1.0f) {
            SDL_SetWindowSize(
                window,
                static_cast<int>(std::lround(static_cast<float>(request.width) * scale)),
                static_cast<int>(std::lround(static_cast<float>(request.height) * scale)));
            SDL_SetWindowPosition(
                window,
                request.positionSet ? request.x : SDL_WINDOWPOS_CENTERED,
                request.positionSet ? request.y : SDL_WINDOWPOS_CENTERED);
        }
    }
#endif
#if defined(_WIN32)
    installSdlImeFilter(window);
#endif
    return window;
}

void destroyWindow(Handle window) {
    if (window != nullptr) {
        auto* sdlWindow = static_cast<SDL_Window*>(window);
#if defined(_WIN32)
        uninstallSdlImeFilter(sdlWindow);
#endif
        SDL_DestroyWindow(sdlWindow);
    }
}

NativeWindowInfo nativeWindowInfo(Handle window) {
    NativeWindowInfo result;
    result.handle = window;
#if defined(_WIN32) || defined(__APPLE__)
    if (window == nullptr) {
        return result;
    }
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(static_cast<SDL_Window*>(window), &info) != SDL_TRUE) {
        return result;
    }
#if defined(_WIN32)
    if (info.subsystem == SDL_SYSWM_WINDOWS) {
        result.platformWindow = info.info.win.window;
    }
#elif defined(__APPLE__)
    if (info.subsystem == SDL_SYSWM_COCOA) {
        result.platformWindow = info.info.cocoa.window;
    }
#endif
#endif
    return result;
}

ContextKey currentContextKey() {
    return SDL_GL_GetCurrentContext();
}

double timeSeconds() {
    const Uint64 frequency = SDL_GetPerformanceFrequency();
    return frequency > 0
        ? static_cast<double>(SDL_GetPerformanceCounter()) / static_cast<double>(frequency)
        : 0.0;
}

void postEmptyEvent() {
    SDL_Event event{};
    event.type = SDL_USEREVENT;
    SDL_PushEvent(&event);
}

std::string clipboardText(Handle) {
    char* text = SDL_GetClipboardText();
    if (text == nullptr) {
        return {};
    }
    std::string result(text);
    SDL_free(text);
    return result;
}

void setClipboardText(const std::string& text) {
    SDL_SetClipboardText(text.c_str());
}

CursorHandle createStandardCursor(CursorType type) {
    return SDL_CreateSystemCursor(type == CursorType::Hand ? SDL_SYSTEM_CURSOR_HAND : SDL_SYSTEM_CURSOR_ARROW);
}

void setCursor(Handle, CursorHandle cursor) {
    SDL_SetCursor(static_cast<SDL_Cursor*>(cursor));
}

void destroyCursor(CursorHandle cursor) {
    SDL_FreeCursor(static_cast<SDL_Cursor*>(cursor));
}

void setWindowIcon(Handle window, int width, int height, unsigned char* pixels) {
    if (window == nullptr || pixels == nullptr || width <= 0 || height <= 0) {
        return;
    }
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        pixels, width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA32);
    if (surface != nullptr) {
        SDL_SetWindowIcon(static_cast<SDL_Window*>(window), surface);
        SDL_FreeSurface(surface);
    }
    eui_set_application_icon_rgba(width, height, pixels);
}

void setImeCursorRect(Handle window, float x, float y, float width, float height) {
#if defined(_WIN32)
    SDL_Rect rect{
        static_cast<int>(x + 0.5f),
        static_cast<int>(y + 0.5f),
        static_cast<int>(width + 0.5f),
        static_cast<int>(height + 0.5f)
    };
    HWND hwnd = hwndForSdlWindow(static_cast<SDL_Window*>(window));
    if (hwnd != nullptr) {
        auto* state = sdlImeState(hwnd);
        if (state != nullptr) {
            state->rect = rect;
            state->hasRect = true;
        }
    }
    SDL_SetTextInputRect(&rect);
#else
    int windowWidth = 0;
    int windowHeight = 0;
    int drawableWidth = 0;
    int drawableHeight = 0;
    SDL_GetWindowSize(static_cast<SDL_Window*>(window), &windowWidth, &windowHeight);
    SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(window), &drawableWidth, &drawableHeight);
    const float scaleX = windowWidth > 0 && drawableWidth > 0
        ? static_cast<float>(drawableWidth) / static_cast<float>(windowWidth)
        : 1.0f;
    const float scaleY = windowHeight > 0 && drawableHeight > 0
        ? static_cast<float>(drawableHeight) / static_cast<float>(windowHeight)
        : 1.0f;
    SDL_Rect rect{
        static_cast<int>(x / scaleX),
        static_cast<int>(y / scaleY),
        static_cast<int>(width / scaleX),
        static_cast<int>(height / scaleY)
    };
    SDL_SetTextInputRect(&rect);
#endif
}

void setWindowPos(Handle window, int x, int y) {
    if (window != nullptr) {
        SDL_SetWindowPosition(static_cast<SDL_Window*>(window), x, y);
    }
}

void getWindowPos(Handle window, int& x, int& y) {
    x = 0;
    y = 0;
    if (window != nullptr) {
        SDL_GetWindowPosition(static_cast<SDL_Window*>(window), &x, &y);
    }
}

void setWindowMousePassthrough(Handle window, bool enabled) {
    // SDL2 2.26+ 才有 SDL_SetWindowMousePassthrough；老版本静默无操作
#if SDL_VERSION_ATLEAST(2, 26, 0)
    if (window != nullptr) {
        SDL_SetWindowMousePassthrough(static_cast<SDL_Window*>(window),
                                      enabled ? SDL_TRUE : SDL_FALSE);
    }
#else
    (void)window;
    (void)enabled;
#endif
}

void requestWindowClose(Handle window) {
    // SDL2 无 per-window should-close：走该窗口的 CLOSE 事件（与用户点 X 同路，
    // 主循环 processManagedEvent 置 closeRequested 后 prune 销毁）
    if (window != nullptr) {
        SDL_Event event{};
        event.type = SDL_WINDOWEVENT;
        event.window.event = SDL_WINDOWEVENT_CLOSE;
        event.window.windowID = SDL_GetWindowID(static_cast<SDL_Window*>(window));
        SDL_PushEvent(&event);
    }
}

void getPrimaryMonitorWorkArea(int& x, int& y, int& width, int& height) {
    x = 0;
    y = 0;
    SDL_Rect bounds{};
    // SDL_GetDisplayUsableBounds 已剔除任务栏/Dock 区域；失败时回退 0（调用方按无效处理）
    if (SDL_GetDisplayUsableBounds(0, &bounds) == 0) {
        x = bounds.x;
        y = bounds.y;
        width = bounds.w;
        height = bounds.h;
    } else {
        width = 0;
        height = 0;
    }
}

} // namespace core::window

#else

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include "core/platform/ime_bridge.h"

namespace core::window {

namespace {

void configureOpenGLWindowHints() {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_SAMPLES, 0);
    glfwWindowHint(GLFW_RED_BITS, 8);
    glfwWindowHint(GLFW_GREEN_BITS, 8);
    glfwWindowHint(GLFW_BLUE_BITS, 8);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    glfwWindowHint(GLFW_DEPTH_BITS, 16);
    glfwWindowHint(GLFW_STENCIL_BITS, 0);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
}

} // namespace

Handle createWindow(const WindowCreateRequest& request) {
    GLFWwindow* shareContext = nullptr;
    if (request.renderApi == RenderApi::Vulkan) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    } else {
        configureOpenGLWindowHints();
        // 透明帧缓冲（磨砂设计 Phase B / 桌宠 sprite 窗）：DWM 尊重重定向表面
        // alpha 的开关。两条来源：磨砂档位（windowEffect != None，应用全局）或
        // 窗口自身声明（transparentFramebuffer，桌宠等覆盖窗——不随全局档位
        // 回落 None 丢透明）。Vulkan 侧无对应能力（compositeAlpha 普遍
        // OPAQUE），直接不设。hint 会跨 glfwCreateWindow 残留，两态都显式设置。
        glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER,
                       request.windowEffect != platform::WindowEffect::None ||
                               request.transparentFramebuffer
                           ? GLFW_TRUE
                           : GLFW_FALSE);
        shareContext = static_cast<GLFWwindow*>(request.parent);
    }
    glfwWindowHint(GLFW_RESIZABLE, request.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, request.decorated ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, request.alwaysOnTop ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, request.maximized ? GLFW_TRUE : GLFW_FALSE);
    // 不抢焦点（桌宠设计 G1）：glfwShowWindow 是否把窗口带到前台。hint 跨
    // glfwCreateWindow 残留，两态都显式设置。
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, request.focusOnShow ? GLFW_TRUE : GLFW_FALSE);
    // 鼠标穿透（G4）：GLFW 3.4 创建期 hint（仅无边框窗口生效，有边框静默忽略）
    glfwWindowHint(GLFW_MOUSE_PASSTHROUGH,
                   request.mousePassthrough ? GLFW_TRUE : GLFW_FALSE);
    // 隐藏创建（G5 时序）：任务栏隐藏窗须先创建（GLFW 默认即显示并注册任务栏
    // 按钮）→ 应用 WS_EX_TOOLWINDOW → 再显示。hint 跨 glfwCreateWindow 残留，
    // 两态都显式设置
    glfwWindowHint(GLFW_VISIBLE, request.visible ? GLFW_TRUE : GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(
        request.width,
        request.height,
        request.title != nullptr ? request.title : "",
        nullptr,
        shareContext);
    if (window == nullptr) {
        return nullptr;
    }
    // 透明 hint 回查（设计 §3.1.2）：桌面合成被禁用/老平台时 GLFW 静默降级为
    // 不透明。渲染侧（GL 后端）按同一属性自检走 straight blit，故此处只诊断。
    if ((request.windowEffect != platform::WindowEffect::None ||
         request.transparentFramebuffer) &&
        glfwGetWindowAttrib(window, GLFW_TRANSPARENT_FRAMEBUFFER) != GLFW_TRUE) {
        std::fprintf(stderr,
                     "[eui] window: transparent framebuffer unavailable, "
                     "windowEffect degrades to None\n");
    }

    const int minimumWidth = request.minWidth > 0 ? request.minWidth : GLFW_DONT_CARE;
    const int minimumHeight = request.minHeight > 0 ? request.minHeight : GLFW_DONT_CARE;
    const int maximumWidth = request.maxWidth > 0
        ? std::max(request.maxWidth, request.minWidth > 0 ? request.minWidth : 1)
        : GLFW_DONT_CARE;
    const int maximumHeight = request.maxHeight > 0
        ? std::max(request.maxHeight, request.minHeight > 0 ? request.minHeight : 1)
        : GLFW_DONT_CARE;
    if (minimumWidth != GLFW_DONT_CARE || minimumHeight != GLFW_DONT_CARE ||
        maximumWidth != GLFW_DONT_CARE || maximumHeight != GLFW_DONT_CARE) {
        glfwSetWindowSizeLimits(window, minimumWidth, minimumHeight, maximumWidth, maximumHeight);
    }
    if (request.positionSet) {
        glfwSetWindowPos(window, request.x, request.y);
    }
    return window;
}

void destroyWindow(Handle window) {
    if (window != nullptr) {
        glfwDestroyWindow(static_cast<GLFWwindow*>(window));
    }
}

NativeWindowInfo nativeWindowInfo(Handle window) {
    NativeWindowInfo result;
    result.handle = window;
    return result;
}

ContextKey currentContextKey() {
    return glfwGetCurrentContext();
}

double timeSeconds() {
    return glfwGetTime();
}

void postEmptyEvent() {
    glfwPostEmptyEvent();
}

std::string clipboardText(Handle window) {
    const char* text = glfwGetClipboardString(static_cast<GLFWwindow*>(window));
    return text != nullptr ? text : "";
}

void setClipboardText(const std::string& text) {
    glfwSetClipboardString(glfwGetCurrentContext(), text.c_str());
}

CursorHandle createStandardCursor(CursorType type) {
    return glfwCreateStandardCursor(type == CursorType::Hand ? GLFW_HAND_CURSOR : GLFW_ARROW_CURSOR);
}

void setCursor(Handle window, CursorHandle cursor) {
    glfwSetCursor(static_cast<GLFWwindow*>(window), static_cast<GLFWcursor*>(cursor));
}

void destroyCursor(CursorHandle cursor) {
    glfwDestroyCursor(static_cast<GLFWcursor*>(cursor));
}

void setWindowIcon(Handle window, int width, int height, unsigned char* pixels) {
    if (window == nullptr || pixels == nullptr || width <= 0 || height <= 0) {
        return;
    }
    GLFWimage image{};
    image.width = width;
    image.height = height;
    image.pixels = pixels;
    glfwSetWindowIcon(static_cast<GLFWwindow*>(window), 1, &image);
    eui_set_application_icon_rgba(width, height, pixels);
}

void setImeCursorRect(Handle window, float x, float y, float width, float height) {
    eui_ime_set_cursor_rect_with_font(static_cast<GLFWwindow*>(window), x, y, width, height, height);
}

void setWindowPos(Handle window, int x, int y) {
    if (window != nullptr) {
        glfwSetWindowPos(static_cast<GLFWwindow*>(window), x, y);
    }
}

void getWindowPos(Handle window, int& x, int& y) {
    x = 0;
    y = 0;
    if (window != nullptr) {
        glfwGetWindowPos(static_cast<GLFWwindow*>(window), &x, &y);
    }
}

void setWindowMousePassthrough(Handle window, bool enabled) {
    if (window != nullptr) {
        glfwSetWindowAttrib(static_cast<GLFWwindow*>(window), GLFW_MOUSE_PASSTHROUGH,
                            enabled ? GLFW_TRUE : GLFW_FALSE);
    }
}

void requestWindowClose(Handle window) {
    if (window != nullptr) {
        // 与用户点标题栏关闭同一语义：子窗口由主循环 pruneClosedWindows 正常
        // 销毁；主窗口走 close callback（托盘模式 = 隐藏到托盘）
        glfwSetWindowShouldClose(static_cast<GLFWwindow*>(window), GLFW_TRUE);
        glfwPostEmptyEvent();
    }
}

void getPrimaryMonitorWorkArea(int& x, int& y, int& width, int& height) {
    x = 0;
    y = 0;
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor == nullptr) {
        width = 0;
        height = 0;
        return;
    }
    glfwGetMonitorWorkarea(monitor, &x, &y, &width, &height);
}

} // namespace core::window

#endif
