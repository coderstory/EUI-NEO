#include "core/window/window_backend.h"
#include "core/platform/native_bridge.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <unordered_map>

// ---------------------------------------------------------------------------
// 显示器缩放系数（逻辑单位 → 窗口物理像素）：两条窗口后端分支（SDL2 / GLFW）
// 的窗口尺寸语义相同（客户区物理像素），故实现放在分支外，只写一份。
// ---------------------------------------------------------------------------
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace core::window {

namespace {

// GetDpiForMonitor（shcore.dll，Win8.1+）：运行期解析符号，不引入链接期依赖
//（单 exe / 静态链接约束；同 sdl2_app_main.cpp 的 GetDpiForWindow 写法）。
using GetDpiForMonitorFunction = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
constexpr int kMonitorDpiEffective = 0;  // MONITOR_DPI_TYPE::MDT_EFFECTIVE_DPI

float win32MonitorScaleForPoint(float x, float y) {
    const POINT point{static_cast<LONG>(std::lround(x)),
                      static_cast<LONG>(std::lround(y))};
    // 点落在显示器外/负坐标副屏/显示器间隙时取最近显示器（不会返回 nullptr）
    const HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    if (monitor == nullptr) {
        return 1.0f;
    }
    static const GetDpiForMonitorFunction getDpiForMonitor = [] {
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        return shcore != nullptr
            ? reinterpret_cast<GetDpiForMonitorFunction>(
                  GetProcAddress(shcore, "GetDpiForMonitor"))
            : nullptr;
    }();
    if (getDpiForMonitor == nullptr) {
        return 1.0f;
    }
    UINT dpiX = 0;
    UINT dpiY = 0;
    if (FAILED(getDpiForMonitor(monitor, kMonitorDpiEffective, &dpiX, &dpiY)) ||
        dpiX == 0) {
        return 1.0f;
    }
    return static_cast<float>(dpiX) / 96.0f;
}

} // namespace

float windowScaleForPoint(float x, float y) {
    return win32MonitorScaleForPoint(x, y);
}

} // namespace core::window

#else

namespace core::window {

float windowScaleForPoint(float x, float y) {
    // macOS：窗口单位是点，content scale（Retina=2）不是「逻辑→窗口」换算；
    // X11：无独立物理/逻辑分层。两平台均保持 1:1（既有行为）。
    (void)x;
    (void)y;
    return 1.0f;
}

} // namespace core::window

#endif

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

void setWindowSize(Handle window, int width, int height) {
    if (window != nullptr) {
        SDL_SetWindowSize(static_cast<SDL_Window*>(window), width, height);
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
    // SDL2 2.26+ 才有 SDL_SetWindowMousePassthrough；缺失时静默无操作。
    // 不能用 SDL_VERSION_ATLEAST(2,26,0) 守卫：brew 的 sdl2-compat 头谎报版本
    // （SDL_version.h 报 2.32.72 ≥ 2.26），但全部头文件里都没有声明该符号，
    // 编译期直接 use of undeclared identifier。改用 configure 期
    // check_cxx_symbol_exists 探测（根 CMakeLists.txt，2026-09-30）：
    // 探测通过才定义 EUI_SDL_HAS_MOUSE_PASSTHROUGH=1；探测失败时该宏
    // 不定义（或缓存残留为 0），两种情况都落到这里的无操作 fallback。
#if defined(EUI_SDL_HAS_MOUSE_PASSTHROUGH) && EUI_SDL_HAS_MOUSE_PASSTHROUGH
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

bool windowIsFocused(Handle window) {
#if defined(_WIN32)
    if (window == nullptr) {
        return false;
    }
    void* platformWindow = nativeWindowInfo(window).platformWindow;
    return platformWindow != nullptr &&
           GetForegroundWindow() == static_cast<HWND>(platformWindow);
#else
    // 非 Windows 暂无统一同步查询：恒视为有焦点（styledMenu 失焦自关降级）
    (void)window;
    return true;
#endif
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

std::vector<MonitorWorkArea> getMonitorWorkAreas() {
    std::vector<MonitorWorkArea> areas;
    const int count = SDL_GetNumVideoDisplays();
    if (count > 0) {
        areas.reserve(static_cast<size_t>(count));
    }
    for (int i = 0; i < count; ++i) {
        SDL_Rect bounds{};
        // 单个显示器查询失败 → 跳过该显示器（其余照常枚举）
        if (SDL_GetDisplayUsableBounds(i, &bounds) != 0) {
            continue;
        }
        areas.push_back(MonitorWorkArea{bounds.x, bounds.y, bounds.w, bounds.h});
    }
    return areas;
}

// ---- 自绘窗口壳（window_backend.h 声明区见契约）----
// SDL2（desktop Linux）：无 WM_NCHITTEST 等价钩子，自绘壳能力恒不可用；
// 窗口控制三键映射 SDL 原生 API（语义与 GLFW 分支一致，宿主代码零分叉）。
bool customWindowChromeSupported() {
    return false;
}

void maximizeWindow(Handle window) {
    if (window != nullptr) {
        SDL_MaximizeWindow(static_cast<SDL_Window*>(window));
    }
}

void restoreWindow(Handle window) {
    if (window != nullptr) {
        SDL_RestoreWindow(static_cast<SDL_Window*>(window));
    }
}

void iconifyWindow(Handle window) {
    if (window != nullptr) {
        SDL_MinimizeWindow(static_cast<SDL_Window*>(window));
    }
}

bool windowIsMaximized(Handle window) {
    return window != nullptr &&
           (SDL_GetWindowFlags(static_cast<SDL_Window*>(window)) &
            SDL_WINDOW_MAXIMIZED) != 0;
}

void setWindowCaptionHitTestHandler(Handle, CaptionHitTestHandler, bool) {
    // SDL2 后端无标题栏命中钩子（能力查询恒 false，正常宿主不会走到安装）
}

void setWindowLiveRenderHandler(Handle, LiveRenderHandler) {
    // SDL2 后端无自绘壳（能力查询恒 false），无模态拖拽循环可活渲染
}

} // namespace core::window

#else

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#if defined(_WIN32)
// GLFW 路径此前不含 windows.h：交给 glfw3native.h 自带包含（LEAN_AND_MEAN +
// NOMINMAX 防宏污染），暴露 glfwGetWin32Window（HWND 填 NativeWindowInfo）
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include <GLFW/glfw3native.h>
// GET_X_LPARAM / GET_Y_LPARAM（WM_NCHITTEST 的 lParam 拆分；LEAN_AND_MEAN
// 不自动带入 windowsx.h，负坐标多显示器场景手拆 LOWORD 会丢符号位）
#include <windowsx.h>
#elif defined(__APPLE__)
// macOS 路径（M0：macos-three-features 设计 §3）：glfwGetCocoaWindow
// （NSWindow*）填 NativeWindowInfo.platformWindow，M3 桌宠三项消费
#ifndef GLFW_EXPOSE_NATIVE_COCOA
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>
#endif

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

#if defined(_WIN32)
// 自绘窗口壳（Windows/GLFW）：undecorated 主窗口的拖拽/双击最大化/边缘
// resize。GLFW undecorated = 纯 WS_POPUP（无 WS_THICKFRAME/WS_MAXIMIZEBOX，
// 不处理 WM_NCHITTEST），原生 hit-test 恒 HTCLIENT——拖拽/resize/双击全无。
// 这里在 GLFW 的窗口 proc 之上再挂一层子类链（与 ime_bridge / power_events
// 的既有子类叠加兼容：每层 CallWindowProcW 透传上一链，安装顺序无关）。
struct ChromeHitTestState {
    WNDPROC previousProc = nullptr;
    // 宿主命中回调（客户区物理像素）；空 = 未启用（安装时保证非空）
    CaptionHitTestHandler captionHit;
    // false = 不启用八向边缘热区（不可 resize 的窗口）
    bool resizable = false;
    // 安装时给窗口补过 WS_THICKFRAME（卸载须还原）：GLFW undecorated = 纯
    // WS_POPUP，DefWindowProc 的 SC_SIZE 系统尺寸环只对带 WS_SIZEBOX 的窗口
    // 真正改几何——缺它时八向命中码全对也拖不动（系统 Size 命令同因灰置）
    bool thickFrameAdded = false;
    // 安装时补过的动画风格位掩码（卸载按位还原）：WS_MINIMIZEBOX/WS_MAXIMIZEBOX。
    // GLFW undecorated = 纯 WS_POPUP 不带这两位，ShowWindow(SW_MINIMIZE/
    // SW_MAXIMIZE) 虽仍改窗口状态，但 DWM 不播系统缩放动画（窗口直接消失/
    // 出现——真机反馈「缺少动画」根因）
    LONG_PTR addedAnimationStyle = 0;
    // 手动双击检测（GLFW 窗口类只注册 CS_OWNDC 无 CS_DBLCLKS，系统不会派发
    // WM_NCLBUTTONDBLCLK）：记录上一次 caption 区左键按下的时刻/位置，判据
    // 与系统双击一致——时限 GetDoubleClickTime、半径 SM_CX/CYDOUBLECLK
    DWORD lastCaptionDownTick = 0;
    POINT lastCaptionDownPos{};
    // 模态拖拽活渲染（「缩放很卡」修复）：宿主渲染入口（glfw_app_main 在渲染
    // 后端就绪后注册），在模态循环内的 WM_SIZE/WM_MOVE 重入调用
    LiveRenderHandler liveRender;
    // WM_ENTERSIZEMOVE..WM_EXITSIZEMOVE 区间标记：只有 DefWindowProc 的
    // SC_SIZE/SC_MOVE 模态循环会进这对消息，程序化 SetWindowPos 不进——
    // 活渲染严格限定在拖拽中，程序化移动零行为变化
    bool inSizeMoveLoop = false;
    // 重入护栏：活渲染自身级联出的 WM_SIZE/WM_MOVE 不得再入渲染（递归保护）
    bool inLiveRender = false;
};

std::unordered_map<HWND, ChromeHitTestState*>& chromeHitTestStates() {
    static std::unordered_map<HWND, ChromeHitTestState*> states;
    return states;
}

ChromeHitTestState* chromeStateFor(HWND hwnd) {
    const auto iterator = chromeHitTestStates().find(hwnd);
    return iterator != chromeHitTestStates().end() ? iterator->second : nullptr;
}

// WM_NCHITTEST 决策：边缘八向热区（优先）→ 宿主回调（HTCAPTION）→ HTCLIENT。
// lParam 为屏幕坐标；回调契约是客户区物理像素（ScreenToClient 转换）。
LRESULT chromeHitTestFor(HWND hwnd, ChromeHitTestState* state, LPARAM lParam) {
    POINT clientPt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (ScreenToClient(hwnd, &clientPt) == FALSE) {
        return HTCLIENT;
    }
    RECT client{};
    if (GetClientRect(hwnd, &client) == FALSE) {
        return HTCLIENT;
    }
    const int width = static_cast<int>(client.right);
    const int height = static_cast<int>(client.bottom);
    if (width <= 0 || height <= 0) {
        return HTCLIENT;
    }
    const bool maximized = IsZoomed(hwnd) != FALSE;
    // 边缘热区厚度用系统度量（SIZEFRAME+PADDEDBORDER，与装饰窗的命中带一致，
    // 高 DPI 下自动随缩放）；角区再扩一档 SIZEFRAME（系统惯例角区更好命中）。
    // 最大化时整体失效——系统最大化窗格不允许边缘 resize，这里对齐。
    if (!maximized && state->resizable) {
        const int borderX =
            GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
        const int borderY =
            GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
        if (borderX > 0 && borderY > 0) {
            const int corner = borderX + GetSystemMetrics(SM_CXSIZEFRAME);
            const int x = clientPt.x;
            const int y = clientPt.y;
            const bool left = x < borderX;
            const bool right = x >= width - borderX;
            const bool top = y < borderY;
            const bool bottom = y >= height - borderY;
            if (x < corner && y < corner) {
                return HTTOPLEFT;
            }
            if (x >= width - corner && y < corner) {
                return HTTOPRIGHT;
            }
            if (x < corner && y >= height - corner) {
                return HTBOTTOMLEFT;
            }
            if (x >= width - corner && y >= height - corner) {
                return HTBOTTOMRIGHT;
            }
            if (left) {
                return HTLEFT;
            }
            if (right) {
                return HTRIGHT;
            }
            if (top) {
                return HTTOP;
            }
            if (bottom) {
                return HTBOTTOM;
            }
        }
    }
    if (state->captionHit &&
        state->captionHit(clientPt.x, clientPt.y, width, height, maximized)) {
        return HTCAPTION;
    }
    return HTCLIENT;
}

LRESULT CALLBACK chromeHitTestWindowProc(HWND hwnd, UINT message, WPARAM wParam,
                                         LPARAM lParam) {
    ChromeHitTestState* state = chromeStateFor(hwnd);
    if (state == nullptr) {
        // 卸载竞态残余（正常卸载同步还原 proc，不可达）：走 DefWindowProc 保底
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    switch (message) {
    case WM_NCHITTEST:
        return chromeHitTestFor(hwnd, state, lParam);
    case WM_NCLBUTTONDOWN: {
        if (wParam != HTCAPTION) {
            break;  // 边缘 resize 的按下透传给系统起 resize 模态循环
        }
        const DWORD now = GetTickCount();
        const POINT down{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const bool withinTime =
            state->lastCaptionDownTick != 0 &&
            now - state->lastCaptionDownTick <= GetDoubleClickTime();
        const bool withinRect =
            std::abs(down.x - state->lastCaptionDownPos.x) <=
                GetSystemMetrics(SM_CXDOUBLECLK) &&
            std::abs(down.y - state->lastCaptionDownPos.y) <=
                GetSystemMetrics(SM_CYDOUBLECLK);
        state->lastCaptionDownTick = now;
        state->lastCaptionDownPos = down;
        if (withinTime && withinRect) {
            // 双击切换最大化/还原；计时清零防三连击把第二次双击立即翻回去。
            // WS_POPUP 无 WS_MAXIMIZEBOX，DefWindowProc 的 NC 双击路径不存在，
            // 必须自己 ShowWindow；最大化落位由 WM_GETMINMAXINFO 钳到工作区
            state->lastCaptionDownTick = 0;
            ShowWindow(hwnd, IsZoomed(hwnd) != FALSE ? SW_RESTORE : SW_MAXIMIZE);
            return 0;
        }
        break;  // 单击：透传 → DefWindowProc 起 SC_MOVE 拖拽模态循环
    }
    case WM_GETMINMAXINFO: {
        // WS_POPUP 最大化默认盖满整屏（含任务栏）——钳回所在显示器工作区。
        // 先透传让 GLFW 落 min/max track 尺寸，再只覆盖 ptMax 三元组。
        // 无条件钳（不设 IsZoomed/IsIconic 门卫）：zoomed 窗的再适配路径（托盘
        // hide→show 恢复、任务栏/显示器拓扑/DPI 变更后的重落位、iconic 态自
        // 任务栏最大化）同样派发 WM_GETMINMAXINFO，门卫跳过时 ptMax 落
        // DefWindowProc 默认值——该值以主显示器锚定，非主显示器上会得到错误
        // 尺寸的最大化窗（真机「最大化窗口比屏幕大」的候选机制）。钳制值本身
        // 幂等（最大化目标恒 = 所在显示器工作区），已 zoomed 时重派发也是同一
        // 结果（window_shell_probe ⑦/⑦c/⑦d 实证不变）。
        const LRESULT chained =
            CallWindowProcW(state->previousProc, hwnd, message, wParam, lParam);
        const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info) != FALSE) {
            auto* minMax = reinterpret_cast<MINMAXINFO*>(lParam);
            minMax->ptMaxSize.x = info.rcWork.right - info.rcWork.left;
            minMax->ptMaxSize.y = info.rcWork.bottom - info.rcWork.top;
            minMax->ptMaxPosition.x = info.rcWork.left;
            minMax->ptMaxPosition.y = info.rcWork.top;
        }
        return chained;
    }
    case WM_NCCALCSIZE: {
        // 安装期给 resizable 窗补了 WS_THICKFRAME（见 installChromeHitTest），
        // DefWindowProc 会从客户区削掉一圈 resize 带，把自绘壳打回系统边框观
        // 感；自绘壳的全部契约（客户区起点 = 窗口外框：ScreenToClient/
        // GetClientRect 命中几何、DWM backdrop/clearColor 铺满、宿主按客户区
        // 布局）要求客户区 = 整窗。wParam=TRUE 时入参 rgrc[0] 即本次操作的窗
        // 口矩形（屏幕坐标，window_shell_probe 实证），原样接受 + return 0 即
        // 客户区 = 整窗；禁止改写成 {0,0,w,h}（输出是屏幕坐标，会把客户区原
        // 点钉到屏幕原点，真鼠标事件全落空），也禁止 GetWindowRect 重入（样式
        // 变更中读到中间态矩形，形成偏移反馈环）。仅自己补过 THICKFRAME 才接
        // 管（decorated 窗自带系统框，交默认处理）。
        if (wParam == FALSE || !state->thickFrameAdded) {
            break;
        }
        if (IsZoomed(hwnd) != FALSE) {
            // 最大化态的客户区护栏（真机「最大化窗口比屏幕大、右上角关闭钮
            // 半截」的防护层）：本机实证（Win11 26200，window_shell_probe ⑦
            // 系）系统对 ptMax 逐字落位、最大化矩形 == 工作区、客户区已 == 工
            // 作区——本分支在此为恒等变换，零行为变化；但它兜住两类异常落位：
            // ①系统对 zoomed 矩形按不可见 resize 边框外扩的布置变体（其他
            // Windows 版本/arrange 路径），②zoomed 态下矩形异常出屏的混合
            // 态（如创建期带 WS_MAXIMIZE 的窗）。钳法取「rgrc[0] ∩ 工作区」
            // 而非按 SM_CXSIZEFRAME 定值内收：无论系统扩张多少，客户区都精确
            // 收在可见工作区内——可见内容（含自绘关闭钮）恒在屏内，且不会因
            // 扩张量与度量不吻合而欠收/过收（欠收 = 白边，过收 = 内容出屏）。
            const HMONITOR monitor =
                MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info) != FALSE) {
                RECT& client =
                    reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0];
                client.left = std::max(client.left, info.rcWork.left);
                client.top = std::max(client.top, info.rcWork.top);
                client.right = std::min(client.right, info.rcWork.right);
                client.bottom = std::min(client.bottom, info.rcWork.bottom);
            }
        }
        return 0;
    }
    case WM_ENTERSIZEMOVE:
        // DefWindowProc 的 SC_SIZE/SC_MOVE 模态循环起止标记：主循环此刻阻塞
        // 在事件泵的消息派发里回不了环，后续 WM_SIZE/WM_MOVE 由本层重入活渲染
        state->inSizeMoveLoop = true;
        break;
    case WM_EXITSIZEMOVE:
        state->inSizeMoveLoop = false;
        break;
    case WM_SIZE:
    case WM_MOVE: {
        // 模态循环内的活渲染（「缩放很卡」修复）：先透传让 GLFW 落内部几何 +
        // 派发 framebufferSizeCallback（→ 请求重绘），再重入宿主渲染一帧——
        // 不在这里渲染的话拖拽全程零帧，DWM 只能拉伸旧帧。双护栏：仅模态区间
        // 进（程序化 SetWindowPos 零行为变化）、渲染中级联消息不递归。
        const LRESULT chained =
            CallWindowProcW(state->previousProc, hwnd, message, wParam, lParam);
        if (state->inSizeMoveLoop && state->liveRender && !state->inLiveRender) {
            state->inLiveRender = true;
            state->liveRender();
            state->inLiveRender = false;
        }
        return chained;
    }
    default:
        break;
    }
    return CallWindowProcW(state->previousProc, hwnd, message, wParam, lParam);
}

void installChromeHitTest(HWND hwnd, CaptionHitTestHandler handler,
                          bool resizable) {
    auto& states = chromeHitTestStates();
    const auto existing = states.find(hwnd);
    if (existing != states.end()) {
        // 幂等：同窗重复安装 = 先还原上一链（同一 GLFW 窗整个生命周期最多
        // 一层壳子类，重装只发生在换回调的测试/宿主场景）
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(existing->second->previousProc));
        delete existing->second;
        states.erase(existing);
    }
    auto* state = new (std::nothrow) ChromeHitTestState{};
    if (state == nullptr) {
        return;
    }
    state->captionHit = std::move(handler);
    state->resizable = resizable;
    SetLastError(ERROR_SUCCESS);
    state->previousProc =
        reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
    const LONG_PTR previous = SetWindowLongPtrW(
        hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&chromeHitTestWindowProc));
    if (previous == 0 && GetLastError() != ERROR_SUCCESS) {
        delete state;
        return;
    }
    states.emplace(hwnd, state);
    // 样式补齐（两处真机缺陷的样式侧，一次 FRAMECHANGED 落完）：
    // ① 可 resize 窗补 WS_THICKFRAME——边缘命中码只是让系统起 SC_SIZE 尺寸
    //   环，该环对缺 WS_SIZEBOX 的窗口直接空转（真机 UAT4：八向光标对、拖不
    //   动；window_shell_probe 实证补上后 delta=拖拽距离）。非客户带由
    //   WM_NCCALCSIZE 钳回整窗，几何/观感与旧 undecorated 一致。
    // ② 补 WS_MINIMIZEBOX|WS_MAXIMIZEBOX——GLFW undecorated = 纯 WS_POPUP
    //   不带这两位，ShowWindow(SW_MINIMIZE/SW_MAXIMIZE) 仍改状态但 DWM 不播
    //   最小化/还原/最大化的系统动画（窗口直接消失/出现——真机反馈「缺少
    //   动画」根因）。popup 无 caption/系统菜单，两位不会引出系统按钮。
    // 先立标志再改样式：SWP_FRAMECHANGED 会同步触发 WM_NCCALCSIZE，晚置
    // thickFrameAdded 走默认削边。卸载按记录位还原（window_shell_probe 断言）。
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    LONG_PTR styleToAdd = 0;
    if (resizable && (style & WS_THICKFRAME) == 0) {
        state->thickFrameAdded = true;
        styleToAdd |= WS_THICKFRAME;
    }
    if ((style & WS_MINIMIZEBOX) == 0) {
        state->addedAnimationStyle |= WS_MINIMIZEBOX;
        styleToAdd |= WS_MINIMIZEBOX;
    }
    if ((style & WS_MAXIMIZEBOX) == 0) {
        state->addedAnimationStyle |= WS_MAXIMIZEBOX;
        styleToAdd |= WS_MAXIMIZEBOX;
    }
    if (styleToAdd != 0) {
        SetWindowLongPtrW(hwnd, GWL_STYLE, style | styleToAdd);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

void uninstallChromeHitTest(HWND hwnd) {
    auto& states = chromeHitTestStates();
    const auto iterator = states.find(hwnd);
    if (iterator == states.end()) {
        return;
    }
    ChromeHitTestState* state = iterator->second;
    states.erase(iterator);
    if (state->previousProc != nullptr) {
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(state->previousProc));
    }
    // 还原安装期补的样式位（测试/换回调场景反复装卸，样式必须可逆；纯
    // WS_POPUP 无 THICKFRAME 时 NCCALCSIZE 默认本就客户区=整窗，无需再补偿）
    const LONG_PTR styleToRemove =
        (state->thickFrameAdded ? WS_THICKFRAME : 0) | state->addedAnimationStyle;
    if (styleToRemove != 0) {
        const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        SetWindowLongPtrW(hwnd, GWL_STYLE, style & ~styleToRemove);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    delete state;
}
#endif

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
        const bool transparentHint =
            request.windowEffect != platform::WindowEffect::None ||
            request.transparentFramebuffer;
        glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER,
                       transparentHint ? GLFW_TRUE : GLFW_FALSE);
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
#if defined(__APPLE__)
    // 创建期边界诊断（EUI_FX_DEBUG=1；非 Apple 平台预处理为空，翻译单元不变）：
    // hint 请求值 / GLFW 回查值 / 尺寸与缩放折算链起点
    if (const char* fxDebugEnv = std::getenv("EUI_FX_DEBUG");
        fxDebugEnv != nullptr && *fxDebugEnv != '\0') {
        int winW = 0;
        int winH = 0;
        int fbW = 0;
        int fbH = 0;
        float csX = 0.0f;
        float csY = 0.0f;
        glfwGetWindowSize(window, &winW, &winH);
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glfwGetWindowContentScale(window, &csX, &csY);
        std::fprintf(stderr,
                     "[fxdiag] createWindow req.effect=%d req.transparentFb=%d hint=%d "
                     "readback=%d window=%p title=%s\n",
                     static_cast<int>(request.windowEffect),
                     request.transparentFramebuffer ? 1 : 0,
                     (request.windowEffect != platform::WindowEffect::None ||
                      request.transparentFramebuffer)
                         ? 1
                         : 0,
                     glfwGetWindowAttrib(window, GLFW_TRANSPARENT_FRAMEBUFFER),
                     static_cast<void*>(window),
                     request.title != nullptr ? request.title : "");
        std::fprintf(stderr,
                     "[layoutdiag] createWindow req.size=(%d,%d) req.min=(%d,%d) req.maximized=%d "
                     "req.highDpi=%d -> windowSize=(%d,%d) framebuffer=(%d,%d) contentScale=(%.3f,%.3f) "
                     "scaleToMonitor=%d\n",
                     request.width,
                     request.height,
                     request.minWidth,
                     request.minHeight,
                     request.maximized ? 1 : 0,
                     request.highDpi ? 1 : 0,
                     winW,
                     winH,
                     fbW,
                     fbH,
                     static_cast<double>(csX),
                     static_cast<double>(csY),
                     glfwGetWindowAttrib(window, GLFW_SCALE_TO_MONITOR));
        std::fflush(stderr);
    }
#endif
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
#if defined(_WIN32)
        // 自绘壳子类先卸载再销毁：HWND 数值销毁后可能被系统复用成新窗，
        // 状态表留着会串窗（命中回调打到别人的窗口上）
        if (HWND hwnd = glfwGetWin32Window(static_cast<GLFWwindow*>(window));
            hwnd != nullptr) {
            uninstallChromeHitTest(hwnd);
        }
#endif
        glfwDestroyWindow(static_cast<GLFWwindow*>(window));
    }
}

NativeWindowInfo nativeWindowInfo(Handle window) {
    NativeWindowInfo result;
    result.handle = window;
#if defined(_WIN32)
    // platformWindow = HWND（对齐 SDL2 路径的填充语义）：原生菜单/DWM 等
    // core::platform 调用需要原生句柄，此前 GLFW 路径只回 GLFW 句柄，调用
    // 方无从取 HWND
    if (window != nullptr) {
        result.platformWindow = glfwGetWin32Window(static_cast<GLFWwindow*>(window));
    }
#elif defined(__APPLE__)
    // platformWindow = NSWindow*（M0：macos-three-features 设计 §3）：M3 的
    // applyWindowStyleFlags / showContextMenu 经 nativeWindowInfo(handle)
    // .platformWindow 取原生窗口（桌宠 pet_window.cpp → app::openWindow 链路）
    if (window != nullptr) {
        result.platformWindow = glfwGetCocoaWindow(static_cast<GLFWwindow*>(window));
    }
#endif
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

void setWindowSize(Handle window, int width, int height) {
    if (window != nullptr) {
        glfwSetWindowSize(static_cast<GLFWwindow*>(window), width, height);
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
        // 与用户点标题栏关闭（系统 X / Alt+F4）同一语义：必须经 WM_CLOSE →
        // GLFW windowProc → close callback 这条链——托盘宿主的「隐藏到托盘」/
        // 模态子窗聚焦/真关闭三态决策全部住在 close callback 里（主循环安装）。
        // glfwSetWindowShouldClose 只置标志、不回调，主循环
        // while(!glfwWindowShouldClose) 顶判直接退出 = 托盘宿主的自绘关闭钮
        // 直接退出整个应用（UAT4 真机实证 + window_shell_probe 复现）。
        // 子窗口无 close callback：WM_CLOSE 同样落 shouldClose=true，由主循环
        // pruneClosedWindows 正常销毁，语义不变。非 Windows 无等价投递，保旧
        // 置位语义（macOS/Linux 宿主走系统标题栏，自绘关闭钮无调用方）。
#if defined(_WIN32)
        if (HWND hwnd = glfwGetWin32Window(static_cast<GLFWwindow*>(window));
            hwnd != nullptr) {
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            return;
        }
#endif
        glfwSetWindowShouldClose(static_cast<GLFWwindow*>(window), GLFW_TRUE);
        glfwPostEmptyEvent();
    }
}

bool windowIsFocused(Handle window) {
#if defined(_WIN32)
    // glfw3native.h 已带入 windows.h（LEAN_AND_MEAN + NOMINMAX）
    if (window == nullptr) {
        return false;
    }
    void* platformWindow = nativeWindowInfo(window).platformWindow;
    return platformWindow != nullptr &&
           GetForegroundWindow() == static_cast<HWND>(platformWindow);
#else
    (void)window;
    return true;
#endif
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

std::vector<MonitorWorkArea> getMonitorWorkAreas() {
    std::vector<MonitorWorkArea> areas;
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    // 未初始化（无头/早于 glfwInit）或无显示器：nullptr + count<=0 → 空 vector
    if (monitors == nullptr || count <= 0) {
        return areas;
    }
    areas.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        glfwGetMonitorWorkarea(monitors[i], &x, &y, &width, &height);
        // 宽高为 0 的显示器（查询失败的降级形态）不列入——调用方钳制时按
        // workW/workH<=0 原样返回，列了反而引入无意义候选
        if (width <= 0 || height <= 0) {
            continue;
        }
        areas.push_back(MonitorWorkArea{x, y, width, height});
    }
    return areas;
}

// ---- 自绘窗口壳（契约见 window_backend.h 声明区）----
bool customWindowChromeSupported() {
#if defined(_WIN32)
    return true;
#else
    // macOS：GLFW undecorated = NSWindowStyleMaskBorderless，无红绿灯、不可
    // resize、不可拖拽，且本层无等价命中钩子——宿主应保系统标题栏
    return false;
#endif
}

void maximizeWindow(Handle window) {
    if (window != nullptr) {
        glfwMaximizeWindow(static_cast<GLFWwindow*>(window));
    }
}

void restoreWindow(Handle window) {
    if (window != nullptr) {
        glfwRestoreWindow(static_cast<GLFWwindow*>(window));
    }
}

void iconifyWindow(Handle window) {
    if (window != nullptr) {
        glfwIconifyWindow(static_cast<GLFWwindow*>(window));
    }
}

bool windowIsMaximized(Handle window) {
    return window != nullptr &&
           glfwGetWindowAttrib(static_cast<GLFWwindow*>(window), GLFW_MAXIMIZED) ==
               GLFW_TRUE;
}

void setWindowCaptionHitTestHandler(Handle window, CaptionHitTestHandler handler,
                                    bool resizable) {
#if defined(_WIN32)
    if (window == nullptr) {
        return;
    }
    HWND hwnd = glfwGetWin32Window(static_cast<GLFWwindow*>(window));
    if (hwnd == nullptr) {
        return;
    }
    if (!handler) {
        uninstallChromeHitTest(hwnd);
        return;
    }
    installChromeHitTest(hwnd, std::move(handler), resizable);
#else
    // macOS 无等价钩子（能力查询恒 false，宿主不应请求自绘壳）
    (void)window;
    (void)handler;
    (void)resizable;
#endif
}

void setWindowLiveRenderHandler(Handle window, LiveRenderHandler handler) {
#if defined(_WIN32)
    if (window == nullptr) {
        return;
    }
    HWND hwnd = glfwGetWin32Window(static_cast<GLFWwindow*>(window));
    if (hwnd == nullptr) {
        return;
    }
    ChromeHitTestState* state = chromeStateFor(hwnd);
    if (state == nullptr) {
        // 未装自绘壳的窗口不存在模态拖拽循环（无 HTCAPTION/边缘热区，程序化
        // 移动不发 WM_ENTERSIZEMOVE），注册无操作即可
        return;
    }
    state->liveRender = std::move(handler);
#else
    // macOS 无自绘壳子类（能力查询恒 false），无模态循环可活渲染
    (void)window;
    (void)handler;
#endif
}

} // namespace core::window

#endif
