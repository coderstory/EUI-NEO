// tests/probes/styled_menu_focus_probe.cpp
// 托盘 styled 菜单「失焦自收」端到端真窗口探针（真机 UAT 缺陷：托盘右键菜单
// 失焦后不隐藏，像个独立悬浮窗赖着不走）。跑完整生产链：本探针自带
// app::dslAppConfig / app::compose + eui_app_run 主循环（CMake 把
// glfw_app_main.cpp 编进探针，EUI_APP_RUNNER_LIBRARY 抑制其内置 main），
// 所有窗口/回调/prune 都是生产代码。阶段：
//   A. compose 直开 styled 菜单（wechat 令牌，含图标行——同款生产观感）→
//      等真菜单窗创建 → 断言取得焦点（GLFW_FOCUSED=1；focusOnShow 的
//      SetFocus 线程内生效，与前台授予无关）→ SetFocus(主窗) 合成真实
//      WM_KILLFOCUS（点击别处的等价系统消息，同步投递）→ 断言 KILLFOCUS
//      当拍同步 requestDismiss（activeRuntime 立即清空——旧实现要等下一帧
//      compose 轮询，此断言在旧代码上红）→ 数帧后断言真窗销毁
//      （IsWindow=FALSE：requestWindowClose→WM_CLOSE→shouldClose→prune
//      全链，不留僵尸窗）；
//   B. 真托盘链：PostMessage 本进程 TRAY 窗（类名 TRAY，pid 匹配）
//      WM_TRAY_CALLBACK_MESSAGE(RBUTTONUP) → 生产 hook（onTrayMenu）→
//      styled 菜单再开——覆盖 tray.h 接管分支新加的 SetForegroundWindow
//      调用点（前台授予与否取决于会话权限，只打印观测值不断言）→ 再走一次
//      失焦关闭断言。
// 非 Windows / SDL2 后端：打印 skip 返回 0（直连链只在 Windows+GLFW 布线）。
// 输出全 ASCII（探针结果要进报告，不冒 GBK 控制台乱码风险）。
#include "components/styledmenu.h"
#include "eui_neo.h"

#if !defined(_WIN32) || defined(EUI_WINDOW_BACKEND_SDL2)

int main() {
    // 失焦直连链是 Windows(GLFW) 布线：其他平台无可测对象，跳过即绿
    return 0;
}

#else

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#ifndef GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include <GLFW/glfw3native.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// glfw_app_main.cpp 编进本探针（EUI_APP_RUNNER_LIBRARY 抑制其内置 main），
// 但没有公共头声明入口——补 extern 声明
int eui_app_run();

namespace {

constexpr int kMaxComposes = 80;  // 阶段机步数上限（防呆，正常 <15 步）

int g_failures = 0;
int g_composes = 0;

void check(bool ok, const char* what) {
    if (ok) {
        std::printf("PASS: %s\n", what);
    } else {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

enum class Stage {
    OpenDirect,       // 首帧：抓主窗句柄 + 直开菜单
    WaitDirectMenu,    // 菜单窗已建：断言焦点 → 合成失焦 → 同步断言 dismiss
    WaitDirectGone,   // 断言菜单窗真销毁
    PostTray,         // 投递托盘右键消息（真 TRAY 窗）
    WaitTrayMenu,      // 托盘链菜单已建：再走一次失焦关闭
    WaitTrayGone,     // 断言托盘链菜单窗真销毁
    Done,             // requestExit
};

struct ProbeState {
    Stage stage = Stage::OpenDirect;
    HWND mainWindow = nullptr;
    HWND menuWindow = nullptr;   // 阶段 A 菜单 HWND
    HWND trayWindow = nullptr;   // 本进程 TRAY 窗
    HWND trayMenuWindow = nullptr;  // 阶段 B 菜单 HWND
    int trayPolls = 0;          // 阶段 B 等菜单窗的 compose 拍数
    bool trayRetried = false;   // 消息投递一次性重试（容忍偶发丢失）
};

ProbeState& probe() {
    static ProbeState state;
    return state;
}

// DevDesk componentTokensWeChat 同款映射（wechat 令牌深浅同值）
components::theme::ThemeColorTokens wechatTokens() {
    components::theme::ThemeColorTokens tokens;
    tokens.background = core::Color("#F7F7F7");
    tokens.primary = core::Color("#07C160");
    tokens.surface = core::Color("#FFFFFF");
    tokens.surfaceHover = core::Color("#EDEDED");
    tokens.surfaceActive = core::Color("#E5E5E5");
    tokens.text = core::Color("#1A1A1A");
    tokens.border = core::Color("#E5E5E5");
    tokens.dark = false;
    return tokens;
}

std::vector<components::StyledMenuItem> probeItems() {
    std::vector<components::StyledMenuItem> items;
    components::StyledMenuItem show;
    show.text = "Probe Show";
    show.iconCodepoint = 0xF2D2;  // fa-window-restore（有图标行）
    items.push_back(show);
    items.push_back(components::StyledMenuItem{"-"});
    items.push_back(components::StyledMenuItem{"Probe Exit"});  // 无图标行（图标列对齐路径）
    return items;
}

components::styled_menu_detail::StyledMenuRuntime* probeActiveRuntime() {
    // styled_menu_detail 头文件内联，生产状态机直接读
    const std::shared_ptr<components::styled_menu_detail::StyledMenuRuntime>& runtime =
        components::styled_menu_detail::activeRuntime();
    return runtime ? runtime.get() : nullptr;
}

HWND menuHwndOf(components::styled_menu_detail::StyledMenuRuntime* runtime) {
    if (runtime == nullptr || runtime->handle == nullptr) {
        return nullptr;
    }
    return glfwGetWin32Window(static_cast<GLFWwindow*>(runtime->handle));
}

struct EnumCtx {
    DWORD pid = 0;
    const wchar_t* classPrefix = nullptr;
    HWND exclude = nullptr;
    HWND found = nullptr;
};

BOOL CALLBACK findWindowProc(HWND hwnd, LPARAM lParam) {
    EnumCtx* ctx = reinterpret_cast<EnumCtx*>(lParam);
    if (hwnd == ctx->exclude) {
        return TRUE;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid) {
        return TRUE;
    }
    wchar_t className[64] = {};
    const int length = GetClassNameW(hwnd, className, 64);
    const size_t prefixLength = std::wcslen(ctx->classPrefix);
    if (length > 0 && static_cast<size_t>(length) >= prefixLength &&
        std::wmemcmp(className, ctx->classPrefix, prefixLength) == 0) {
        ctx->found = hwnd;
        return FALSE;  // 找到即停
    }
    return TRUE;
}

// 本进程内按窗口类名前缀找窗口（TRAY 托盘窗 = "TRAY"；主窗 = GLFW 类）。
// 不用 FindWindow：类名不跨进程隔离（DevDesk 等并行实例同用 "TRAY"）。
HWND findOwnWindow(const wchar_t* classPrefix, HWND exclude = nullptr) {
    EnumCtx ctx;
    ctx.pid = GetCurrentProcessId();
    ctx.classPrefix = classPrefix;
    ctx.exclude = exclude;
    EnumWindows(findWindowProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

// 失焦 → 同步关闭断言（核心回归门）：SetFocus(主窗) 触发真实 WM_KILLFOCUS，
// 直连链（onWindowFocusChanged → requestDismiss）当拍清空 activeRuntime；
// 旧实现（compose 轮询）此刻 runtime 仍在，断言红。
void stealFocusAndAssertDismiss(const char* tag) {
    ProbeState& state = probe();
    check(state.mainWindow != nullptr, "main window handle located");
    if (state.mainWindow == nullptr) {
        return;
    }
    ::SetFocus(state.mainWindow);  // 真实系统消息：菜单窗 WM_KILLFOCUS 同步投递
    check(probeActiveRuntime() == nullptr,
          (std::string(tag) + ": KILLFOCUS dismisses menu synchronously").c_str());
}

// 数帧后的销毁断言：requestWindowClose → WM_CLOSE → shouldClose → prune
void assertMenuDestroyed(HWND menuWindow, const char* tag) {
    check(menuWindow == nullptr || !IsWindow(menuWindow),
          (std::string(tag) + ": menu window destroyed (no zombie)").c_str());
}

int parseHoldSeconds(int argc, char** argv) {
    int hold = 3;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            hold = std::max(1, std::atoi(argv[++i]));
        }
    }
    return hold;
}

} // namespace

namespace app {

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("styled_menu_focus_probe")
        .pageId("styled_menu_focus_probe")
        .clearColor({0.97f, 0.97f, 0.97f, 1.0f})
        .windowSize(320, 240)
        .showDebugStatsInTitle(false)
        .showDebugOverlay(false)
        // 真托盘（阶段 B）：TRAY 隐藏窗 + Shell_NotifyIcon，onTrayMenu 走生产
        // hook 链（左键不开原生菜单——TrackPopupMenu 是模态环，探针不进）
        .tray(true)
        .trayTitle("styled_menu_focus_probe")
        .trayIcon("assets/icon.png")
        .onTrayMenu([](int x, int y, bool leftButton) {
            if (leftButton) {
                return true;
            }
            return components::showStyledMenu(
                "probeTray", probeItems(),
                components::StyledMenuStyle(wechatTokens()),
                static_cast<float>(x), static_cast<float>(y),
                [](const std::vector<int>&) {});
        });
    return config;
}

void compose(eui::Ui& ui, const eui::Screen& screen) {
    ProbeState& state = probe();
    if (++g_composes > kMaxComposes) {
        check(false, "compose budget exceeded (stuck stage machine)");
        app::requestExit();
        return;
    }
    std::printf("probe compose %d, stage %d\n", g_composes, static_cast<int>(state.stage));

    switch (state.stage) {
    case Stage::OpenDirect:
        // 主窗句柄此刻唯一（菜单窗尚未创建）
        state.mainWindow = findOwnWindow(L"GLFW");
        check(state.mainWindow != nullptr, "main window located at first compose");
        check(components::showStyledMenu(
                  "probeDirect", probeItems(),
                  components::StyledMenuStyle(wechatTokens()),
                  screen.width * 0.5f, screen.height * 0.5f,
                  [](const std::vector<int>&) {}),
              "showStyledMenu accepted direct open");
        state.stage = Stage::WaitDirectMenu;
        app::requestUpdate();
        break;

    case Stage::WaitDirectMenu: {
        components::styled_menu_detail::StyledMenuRuntime* runtime = probeActiveRuntime();
        if (runtime == nullptr || runtime->handle == nullptr) {
            app::requestUpdate();  // 窗口未建好，下一拍再看
            break;
        }
        state.menuWindow = menuHwndOf(runtime);
        check(state.menuWindow != nullptr, "direct menu window has native handle");
        // 主窗排除菜单窗后重定位一次（防 OpenDirect 拍未捕获到）
        if (state.mainWindow == nullptr) {
            state.mainWindow = findOwnWindow(L"GLFW", state.menuWindow);
        }
        const int focused = glfwGetWindowAttrib(
            static_cast<GLFWwindow*>(runtime->handle), GLFW_FOCUSED);
        check(focused == GLFW_TRUE, "menu window has keyboard focus after show");
        stealFocusAndAssertDismiss("direct");
        state.stage = Stage::WaitDirectGone;
        app::requestUpdate();
        break;
    }

    case Stage::WaitDirectGone:
        assertMenuDestroyed(state.menuWindow, "direct");
        state.stage = Stage::PostTray;
        app::requestUpdate();
        break;

    case Stage::PostTray: {
        state.trayWindow = findOwnWindow(L"TRAY");
        if (state.trayWindow == nullptr) {
            // 无 shell 会话（CI）时托盘不可建：阶段 B 记 skip，不算失败
            std::printf("SKIP: tray window unavailable (headless shell?)\n");
            state.stage = Stage::Done;
        } else {
            // 真实托盘右键消息（本进程 TRAY 窗）：主循环消息泵里 WndProc → hook
            constexpr UINT kTrayCallbackMessage = WM_USER + 1;
            PostMessageW(state.trayWindow, kTrayCallbackMessage, 0, WM_RBUTTONUP);
            std::printf("observed: foreground==trayWindow %d\n",
                        ::GetForegroundWindow() == state.trayWindow ? 1 : 0);
            state.stage = Stage::WaitTrayMenu;
        }
        app::requestUpdate();
        break;
    }

    case Stage::WaitTrayMenu: {
        components::styled_menu_detail::StyledMenuRuntime* runtime = probeActiveRuntime();
        if (runtime == nullptr || runtime->handle == nullptr) {
            ++state.trayPolls;
            if (state.trayPolls == 15 && !state.trayRetried && state.trayWindow != nullptr) {
                // 偶发一次投递不达（首发运行实测过一次）：重投一次；两投都
                // 不达才判失败（hook 链坏 = 两投都不开菜单，仍然红）
                state.trayRetried = true;
                constexpr UINT kTrayCallbackMessage = WM_USER + 1;
                PostMessageW(state.trayWindow, kTrayCallbackMessage, 0, WM_RBUTTONUP);
                std::printf("retry: re-posted tray menu message\n");
            } else if (state.trayPolls > 30) {
                check(false, "tray hook chain opened menu (2 post attempts)");
                state.stage = Stage::Done;
            }
            app::requestUpdate();
            break;
        }
        state.trayMenuWindow = menuHwndOf(runtime);
        check(state.trayMenuWindow != nullptr, "tray-hook menu window created");
        if (state.mainWindow == nullptr) {
            state.mainWindow = findOwnWindow(L"GLFW", state.trayMenuWindow);
        }
        const int focused = glfwGetWindowAttrib(
            static_cast<GLFWwindow*>(runtime->handle), GLFW_FOCUSED);
        check(focused == GLFW_TRUE, "tray-hook menu window has keyboard focus");
        stealFocusAndAssertDismiss("trayHook");
        state.stage = Stage::WaitTrayGone;
        app::requestUpdate();
        break;
    }

    case Stage::WaitTrayGone:
        assertMenuDestroyed(state.trayMenuWindow, "trayHook");
        state.stage = Stage::Done;
        app::requestUpdate();
        break;

    case Stage::Done:
        app::requestExit();
        break;
    }

    // 占位 compose（阶段机驱动全在回调里，主窗只画个背景色）
    ui.stack("probe.root")
        .size(screen.width, screen.height)
        .content([&] {
            ui.rect("probe.background")
                .size(screen.width, screen.height)
                .color(core::Color(0.97f, 0.97f, 0.97f, 1.0f))
                .build();
        })
        .build();
}

} // namespace app

int main(int argc, char** argv) {
    (void)parseHoldSeconds(argc, argv);  // --frames N：ctest 统一传参，探针自管步数
    const int result = eui_app_run();
    if (g_failures != 0) {
        std::printf("styled_menu_focus_probe: %d failure(s) (app exit %d)\n",
                    g_failures, result);
        return 1;
    }
    std::printf("styled_menu_focus_probe: all stages passed (app exit %d)\n", result);
    return 0;
}

#endif  // _WIN32 && !SDL2
