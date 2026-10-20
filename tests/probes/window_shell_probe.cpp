// tests/probes/window_shell_probe.cpp
// 自绘窗口壳（Windows/GLFW）真窗口探针。window_chrome 单测是无头层；真窗口
// 部分此前无覆盖，靠消费方真机 UAT 兜底——UAT4 两项反馈正落在这条缝上，本探针
// 把它补成可跑的实证层：
//   ① WM_NCHITTEST 命中码：生产同款安装链（undecorated+resizable 窗 +
//      setWindowCaptionHitTestHandler）上逐点打消息，断言 八向边缘 / caption /
//      client 三类命中码——resizable 布值、IsZoomed 门卫、系统边缘带厚度全在这
//      一张表里现形；
//   ② requestWindowClose 必须触发 GLFW close callback：托盘宿主的「关闭钮 =
//      隐藏到托盘」决策住在 close callback 里（glfw_app_main 安装），而
//      glfwSetWindowShouldClose 只置标志不回调——绕过 callback 的实现在真机上
//      表现为自绘关闭钮直接退出整个应用（UAT4 实证），本断言是这条链的回归门；
//   ③ --drag 才跑的真指针拖边缘（会动真实鼠标，ctest 默认不开）：SendInput/
//      SetCursorPos 走系统真实输入流，端到端验证 resize 模态环真的改窗宽；
//      同时统计模态环内的活渲染帧数（>0 = 修复在位），并加一段 caption 拖拽
//      移动（SC_MOVE 模态环）的位移 + 活渲染断言；
//   ④ maximized=true 启动窗对照打印（不断言）：边缘热区被 IsZoomed 门卫关闭是
//      设计行为，但真机上「贴工作区放不下 → 启动即最大化」的窗口看起来只是
//      "铺满屏幕、边缘不可拖"——打印出来供诊断，别再当 resize 失灵排查；
//   ⑤ 安装链样式位断言（「缺少动画」修复的回归门）：undecorated 纯 WS_POPUP
//      被shell 补上 WS_MINIMIZEBOX|WS_MAXIMIZEBOX（DWM 最小化/还原/最大化
//      动画的必要风格位）+ WS_THICKFRAME；卸载后逐位还原（与安装前样式全等）；
//   ⑥ 模态区间活渲染门（「缩放很卡」修复的机制层，无需真鼠标）：合成
//      WM_ENTERSIZEMOVE..WM_EXITSIZEMOVE 包住程序化 SetWindowPos，区间内
//      WM_SIZE 重入 live render 回调（真实 GL present），区间外 0 帧——主循环
//      既有渲染路径零行为变化的门卫证据。
// 输出全 ASCII（测量值要进报告，不冒 GBK 控制台乱码风险）。
#include "core/window/window_backend.h"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#if !defined(_WIN32)
int main() {
    // 自绘窗口壳是 Windows(GLFW) 专属能力（customWindowChromeSupported 恒
    // false），其他平台无可测对象，跳过即绿
    std::printf("window_shell_probe: skip (Win32-only shell probe)\n");
    return 0;
}
#else

namespace {

constexpr int kHoldSecondsDefault = 3;  // 真窗口驻留秒数（--frames 覆盖，ctest 传 3）

struct Args {
    int holdSeconds = kHoldSecondsDefault;
    bool drag = false;
};

Args parseArgs(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--drag") == 0) {
            args.drag = true;
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            args.holdSeconds = std::max(1, std::atoi(argv[++i]));
        }
    }
    return args;
}

const char* htName(LRESULT code) {
    switch (code) {
    case HTCLIENT: return "HTCLIENT";
    case HTCAPTION: return "HTCAPTION";
    case HTLEFT: return "HTLEFT";
    case HTRIGHT: return "HTRIGHT";
    case HTTOP: return "HTTOP";
    case HTTOPLEFT: return "HTTOPLEFT";
    case HTTOPRIGHT: return "HTTOPRIGHT";
    case HTBOTTOM: return "HTBOTTOM";
    case HTBOTTOMLEFT: return "HTBOTTOMLEFT";
    case HTBOTTOMRIGHT: return "HTBOTTOMRIGHT";
    default: return "OTHER";
    }
}

// 客户区物理点 → 屏幕 lParam（WM_NCHITTEST 契约：lParam 为屏幕坐标）
LPARAM screenLParamOf(HWND hwnd, int clientX, int clientY) {
    POINT pt{clientX, clientY};
    ClientToScreen(hwnd, &pt);
    return MAKELPARAM(pt.x, pt.y);
}

int g_closeCallbackCount = 0;
int g_liveRenderFrames = 0;       // 模态区间活渲染回调触发次数（live render 证据）
GLFWwindow* g_probeGlfwWindow = nullptr;  // live render 回调里做真实 GL present

// 诊断用日志子类（叠在 chrome 命中链之上，只看不拦）：drag 段临时挂上，
// 打印 WM_NCLBUTTONDOWN / WM_SYSCOMMAND / WM_SIZING / WM_NCCALCSIZE 的实际
// 流向——resize 断在哪一环在这里现形
WNDPROC g_logPreviousProc = nullptr;
int g_logEnabled = 0;
LRESULT CALLBACK logProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_logEnabled != 0) {
        if (message == WM_NCLBUTTONDOWN) {
            std::printf("trace: WM_NCLBUTTONDOWN wParam=%s\n",
                        htName(static_cast<LRESULT>(wParam)));
        } else if (message == WM_SYSCOMMAND) {
            std::printf("trace: WM_SYSCOMMAND wParam&0xfff0=0x%zx\n",
                        static_cast<size_t>(wParam & 0xfff0));
        } else if (message == WM_SIZING || message == WM_SIZE) {
            std::printf("trace: %s\n",
                        message == WM_SIZING ? "WM_SIZING" : "WM_SIZE");
        } else if (message == WM_ENTERSIZEMOVE || message == WM_EXITSIZEMOVE) {
            std::printf("trace: %s\n",
                        message == WM_ENTERSIZEMOVE ? "WM_ENTERSIZEMOVE"
                                                    : "WM_EXITSIZEMOVE");
        } else if (message == WM_MOVE) {
            std::printf("trace: WM_MOVE (%ld,%ld)\n",
                        static_cast<long>(static_cast<short>(LOWORD(lParam))),
                        static_cast<long>(static_cast<short>(HIWORD(lParam))));
        }
    }
    return CallWindowProcW(g_logPreviousProc, hwnd, message, wParam, lParam);
}

int probeWindowShell(bool drag, int holdSeconds) {
    int failures = 0;
    int checks = 0;
    const auto check = [&](const char* what, bool ok) {
        ++checks;
        if (!ok) ++failures;
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    };

    // 生产同款窗体请求：undecorated + resizable（DevDesk 主窗形态）
    core::window::WindowCreateRequest request;
    request.width = 1000;
    request.height = 700;
    request.title = "window_shell_probe";
    // 显式定位（240,160）：GLFW 无位置 hint 时窗口可能落在屏幕 (0,0)——左缘
    // 贴屏时「拖左缘向外」是往负坐标拖，物理光标被屏幕边缘钳住 = 没在拖
    request.x = 240;
    request.y = 160;
    request.positionSet = true;
    request.decorated = false;
    request.resizable = true;
    request.renderApi = core::window::RenderApi::OpenGL;
    core::window::Handle handle = core::window::createWindow(request);
    if (handle == nullptr) {
        std::printf("FAIL createWindow (undecorated+resizable) returned null\n");
        return 1;
    }
    glfwPollEvents();

    HWND hwnd = static_cast<HWND>(
        core::window::nativeWindowInfo(handle).platformWindow);
    if (hwnd == nullptr) {
        std::printf("FAIL nativeWindowInfo().platformWindow is null\n");
        core::window::destroyWindow(handle);
        return 1;
    }

    // 生产同款安装链：caption 回调 + resizable=true（glfw_app_main 主窗口路径）
    // 安装前样式快照：undecorated = 纯 WS_POPUP（无 THICKFRAME/MIN/MAXBOX），
    // ⑤ 的卸载还原断言以它为基线
    const LONG_PTR styleBeforeInstall = GetWindowLongPtrW(hwnd, GWL_STYLE);
    core::window::setWindowCaptionHitTestHandler(
        handle, [](int x, int y, int width, int height, bool maximized) {
            (void)x; (void)width; (void)maximized;
            return y >= 0 && y < 40 && height > 0;  // 40 物理像素标题带（探针自定档）
        }, /*resizable=*/true);

    // ---- ⑤ 安装链样式位（「缺少动画」修复的回归门，无需真鼠标）----
    // 缺 WS_MINIMIZEBOX/WS_MAXIMIZEBOX 时 ShowWindow(SW_MINIMIZE/SW_MAXIMIZE)
    // 仍改状态但 DWM 不播系统缩放动画（窗口直接消失/出现）。安装后三位必须在。
    {
        const LONG_PTR styleInstalled = GetWindowLongPtrW(hwnd, GWL_STYLE);
        check("style has WS_THICKFRAME after install (resize size loop)",
              (styleInstalled & WS_THICKFRAME) != 0);
        check("style has WS_MINIMIZEBOX after install (minimize animation)",
              (styleInstalled & WS_MINIMIZEBOX) != 0);
        check("style has WS_MAXIMIZEBOX after install (maximize animation)",
              (styleInstalled & WS_MAXIMIZEBOX) != 0);
    }

    RECT windowRect{};
    RECT clientRect{};
    GetWindowRect(hwnd, &windowRect);
    GetClientRect(hwnd, &clientRect);
    const LONG windowW = windowRect.right - windowRect.left;
    const LONG windowH = windowRect.bottom - windowRect.top;
    const LONG clientW = clientRect.right;
    const LONG clientH = clientRect.bottom;
    const BOOL zoomed = IsZoomed(hwnd);
    const int borderX = GetSystemMetrics(SM_CXSIZEFRAME) +
                        GetSystemMetrics(SM_CXPADDEDBORDER);
    const int borderY = GetSystemMetrics(SM_CYSIZEFRAME) +
                        GetSystemMetrics(SM_CXPADDEDBORDER);
    float csX = 0.0f;
    float csY = 0.0f;
    glfwGetWindowContentScale(static_cast<GLFWwindow*>(handle), &csX, &csY);
    int workX = 0; int workY = 0; int workW = 0; int workH = 0;
    core::window::getPrimaryMonitorWorkArea(workX, workY, workW, workH);
    std::printf("env: window=%ldx%ld@(%ld,%ld) client=%ldx%ld zoomed=%d "
                "style=0x%08lx border=%d/%d contentScale=%.2f workarea=%dx%d\n",
                windowW, windowH, windowRect.left, windowRect.top, clientW,
                clientH, zoomed != FALSE, GetWindowLongW(hwnd, GWL_STYLE),
                borderX, borderY, static_cast<double>(csX), workW, workH);

    // 安装链自身补了 WS_THICKFRAME（resize 尺寸环的必要样式）；WM_NCCALCSIZE
    // 必须把削边钳回整窗——客户区被削 16px 就会直接破坏宿主「客户区 = 窗口外框」
    // 的全部布局假设，这里钉住；位置也钉：样式变更把窗口挪离请求位（重入反馈
    // 环推到 (0,0)）同样红
    char nccalcLine[128];
    std::snprintf(nccalcLine, sizeof(nccalcLine),
                  "client == outer window rect (%ldx%ld vs %ldx%ld)", clientW,
                  clientH, windowW, windowH);
    check(nccalcLine, clientW == windowW && clientH == windowH);
    char posLine[128];
    std::snprintf(posLine, sizeof(posLine),
                  "window position preserved (%ld,%ld)", windowRect.left,
                  windowRect.top);
    check(posLine, windowRect.left == request.x && windowRect.top == request.y);

    // ---- ① WM_NCHITTEST 命中码表（中心点安全余量：middle 取中、边点贴 1px）----
    struct HitCase {
        const char* label;
        int clientX;
        int clientY;
        LRESULT want;
    };
    const HitCase cases[] = {
        {"edge left        -> HTLEFT", 1, clientH / 2, HTLEFT},
        {"edge right       -> HTRIGHT", clientW - 1, clientH / 2, HTRIGHT},
        {"edge top         -> HTTOP", clientW / 2, 1, HTTOP},
        {"edge bottom      -> HTBOTTOM", clientW / 2, clientH - 1, HTBOTTOM},
        {"corner top-left  -> HTTOPLEFT", 1, 1, HTTOPLEFT},
        {"corner top-right -> HTTOPRIGHT", clientW - 1, 1, HTTOPRIGHT},
        {"corner bot-left  -> HTBOTTOMLEFT", 1, clientH - 1, HTBOTTOMLEFT},
        {"corner bot-right -> HTBOTTOMRIGHT", clientW - 1, clientH - 1,
         HTBOTTOMRIGHT},
        {"caption band     -> HTCAPTION", clientW / 2, 20, HTCAPTION},
        {"client center    -> HTCLIENT", clientW / 2, clientH / 2, HTCLIENT},
    };
    for (const HitCase& hitCase : cases) {
        const LRESULT got = SendMessageW(
            hwnd, WM_NCHITTEST, 0,
            screenLParamOf(hwnd, hitCase.clientX, hitCase.clientY));
        char line[128];
        std::snprintf(line, sizeof(line), "%s (got %s)", hitCase.label,
                      htName(got));
        check(line, got == hitCase.want);
    }

    // ---- ② requestWindowClose 必须经 close callback（托盘隐藏链的回归门）----
    // 托盘宿主在 close callback 里撤回关闭改隐藏到托盘；程序置 shouldClose 不
    // 回调 = 自绘关闭钮直接退整应用。探针自己装一个计数 callback 复现宿主形态。
    g_closeCallbackCount = 0;
    glfwSetWindowCloseCallback(static_cast<GLFWwindow*>(handle),
                               [](GLFWwindow* w) {
                                   ++g_closeCallbackCount;
                                   glfwSetWindowShouldClose(w, GLFW_FALSE);
                               });
    core::window::requestWindowClose(handle);
    glfwSetWindowShouldClose(static_cast<GLFWwindow*>(handle), GLFW_FALSE);
    char closeLine[128];
    std::snprintf(closeLine, sizeof(closeLine),
                  "requestWindowClose fires close callback (count=%d)",
                  g_closeCallbackCount);
    check(closeLine, g_closeCallbackCount == 1);
    glfwSetWindowCloseCallback(static_cast<GLFWwindow*>(handle), nullptr);

    // ---- ②b 移动不削边：caption 拖拽（生产路径）全程走 NCCALCSIZE 纯移动
    // 分支，THICKFRAME 在身时若该分支把客户区重新削回 984 即视面积缩水 ----
    {
        RECT before{};
        GetClientRect(hwnd, &before);
        SetWindowPos(hwnd, nullptr, request.x + 60, request.y + 40, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        RECT after{};
        GetClientRect(hwnd, &after);
        RECT movedRect{};
        GetWindowRect(hwnd, &movedRect);
        char moveLine[128];
        std::snprintf(moveLine, sizeof(moveLine),
                      "window move keeps client == outer (%ldx%ld, pos %ld,%ld)",
                      after.right - after.left, after.bottom - after.top,
                      movedRect.left, movedRect.top);
        check(moveLine,
              (after.right - after.left) == (before.right - before.left) &&
                  (after.bottom - after.top) == (before.bottom - before.top) &&
                  (after.right - after.left) == windowW);
    }

    // ---- ⑥ 模态区间活渲染门（「缩放很卡」修复的机制层，无需真鼠标）----
    // 契约：WM_ENTERSIZEMOVE..WM_EXITSIZEMOVE 区间内的 WM_SIZE/WM_MOVE 重入
    // live render 回调；区间外（程序化 SetWindowPos）不得触发——主循环既有
    // 渲染路径零行为变化。回调里做真实 GL present（makeCurrent + SwapBuffers），
    // 同线程提交路径一并实证；生产实装在 glfw_app_main（updateAndRender 同
    // 一入口渲染整帧）。
    {
        g_probeGlfwWindow = static_cast<GLFWwindow*>(handle);
        glfwMakeContextCurrent(g_probeGlfwWindow);
        core::window::setWindowLiveRenderHandler(handle, [] {
            ++g_liveRenderFrames;
            glfwSwapBuffers(g_probeGlfwWindow);
        });
        RECT probeRect{};
        GetWindowRect(hwnd, &probeRect);
        const LONG outerW = probeRect.right - probeRect.left;
        const LONG outerH = probeRect.bottom - probeRect.top;
        g_liveRenderFrames = 0;
        SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
        SetWindowPos(hwnd, nullptr, 0, 0, outerW + 24, outerH,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(hwnd, nullptr, 0, 0, outerW + 48, outerH,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
        const int modalLiveFrames = g_liveRenderFrames;
        // 区间外的程序化 resize（几何回原位）：门卫在位 = 0 新帧
        SetWindowPos(hwnd, nullptr, 0, 0, outerW, outerH,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        char liveGateLine[160];
        std::snprintf(liveGateLine, sizeof(liveGateLine),
                      "live render inside modal loop only (%d frames inside, "
                      "%d after exit)",
                      modalLiveFrames, g_liveRenderFrames - modalLiveFrames);
        check(liveGateLine, modalLiveFrames >= 2 &&
                                g_liveRenderFrames == modalLiveFrames);
    }

    // ---- ③ 真指针拖左边缘（--drag 显式开关：会动真实鼠标，别在 ctest 里跑）----
    // 方向语义：拖左缘向外（x-140）= 左缘左移 = 外框变宽 ~140。
    if (drag) {
        RECT outerBefore{};
        GetWindowRect(hwnd, &outerBefore);
        g_liveRenderFrames = 0;  // 本段起计入：边缘拖拽模态环内的活渲染帧
        g_logPreviousProc = reinterpret_cast<WNDPROC>(
            GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(&logProc));
        g_logEnabled = 1;
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(hwnd);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        // 激活稳定步：非前台时首击会被「激活」吞掉（drag 段曾因此假红）——
        // 客户区中心点一次空点击确保 active，再进拖拽
        for (int attempt = 0; attempt < 3 && GetActiveWindow() != hwnd;
             ++attempt) {
            POINT center{clientW / 2, clientH / 2};
            ClientToScreen(hwnd, &center);
            SetCursorPos(center.x, center.y);
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            INPUT press{};
            press.type = INPUT_MOUSE;
            press.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            INPUT release{};
            release.type = INPUT_MOUSE;
            release.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(1, &press, sizeof(INPUT));
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            SendInput(1, &release, sizeof(INPUT));
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
        }

        INPUT down{};
        down.type = INPUT_MOUSE;
        down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        INPUT up{};
        up.type = INPUT_MOUSE;
        up.mi.dwFlags = MOUSEEVENTF_LEFTUP;

        // 最多三试：inactive 窗的首击被 WM_MOUSEACTIVATE(MA_ACTIVATE) 吃掉
        //（激活而不重放按键）——首试 delta=0 是环境激活竞态，窗口 active 后第
        // 二试必过；活渲染帧数跨试累计（>0 即模态环活渲染在位）
        long delta = 0;
        long widthBefore = 0;
        long widthAfter = 0;
        for (int attempt = 0; attempt < 3 && delta < 90; ++attempt) {
            if (attempt > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            POINT edge{1, clientH / 2};
            ClientToScreen(hwnd, &edge);
            SetCursorPos(edge.x, edge.y);
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            SendInput(1, &down, sizeof(INPUT));
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            for (int step = 1; step <= 5; ++step) {
                SetCursorPos(edge.x - step * 28, edge.y);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            SendInput(1, &up, sizeof(INPUT));
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            glfwPollEvents();
            RECT attemptRect{};
            GetWindowRect(hwnd, &attemptRect);
            widthBefore = outerBefore.right - outerBefore.left;
            widthAfter = attemptRect.right - attemptRect.left;
            delta = widthAfter - widthBefore;  // 左缘外拖 → 变宽
            std::printf("drag: attempt %d delta=%ld active=%d\n", attempt,
                        delta, GetActiveWindow() == hwnd ? 1 : 0);
            std::fflush(stdout);
        }
        POINT cursorAtUp{};
        GetCursorPos(&cursorAtUp);
        POINT edgeAtUp{1, clientH / 2};
        ClientToScreen(hwnd, &edgeAtUp);
        RECT rectAtDrag{};
        GetWindowRect(hwnd, &rectAtDrag);
        std::printf("drag: window at (%ld,%ld)\n", rectAtDrag.left,
                    rectAtDrag.top);
        char dragLine[192];
        std::snprintf(dragLine, sizeof(dragLine),
                      "real-mouse drag left edge resizes (outer %ld -> %ld, "
                      "cursor %ld -> %ld, delta %ld)",
                      widthBefore, widthAfter,
                      static_cast<long>(edgeAtUp.x),
                      static_cast<long>(cursorAtUp.x), delta);
        check(dragLine, delta >= 90);

        // 活渲染证据（真鼠标端到端）：拖边缘的模态环里每个 WM_SIZE/WM_MOVE
        // 重入 live render 一帧——修复前这里恒 0（主循环阻塞、全程停帧）
        char liveResizeLine[128];
        std::snprintf(liveResizeLine, sizeof(liveResizeLine),
                      "live frames during edge-drag resize = %d",
                      g_liveRenderFrames);
        check(liveResizeLine, g_liveRenderFrames > 0);

        // caption 拖拽移动（SC_MOVE 模态环）：同一活渲染机制覆盖纯移动场景
        //（拖动期间动画不冻结），并断言窗口真位移
        g_liveRenderFrames = 0;
        RECT beforeCaptionDrag{};
        GetWindowRect(hwnd, &beforeCaptionDrag);
        POINT caption{clientW / 2, 20};
        ClientToScreen(hwnd, &caption);
        SetCursorPos(caption.x, caption.y);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        SendInput(1, &down, sizeof(INPUT));
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        for (int step = 1; step <= 4; ++step) {
            SetCursorPos(caption.x + step * 22, caption.y + step * 16);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        SendInput(1, &up, sizeof(INPUT));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        glfwPollEvents();
        RECT afterCaptionDrag{};
        GetWindowRect(hwnd, &afterCaptionDrag);
        const long movedX = afterCaptionDrag.left - beforeCaptionDrag.left;
        char captionDragLine[192];
        std::snprintf(captionDragLine, sizeof(captionDragLine),
                      "real-mouse caption drag moves window (dx=%ld) with "
                      "live frames = %d",
                      movedX, g_liveRenderFrames);
        check(captionDragLine, movedX > 0 && g_liveRenderFrames > 0);

        g_logEnabled = 0;
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(g_logPreviousProc));
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    // ---- ④ maximized 启动窗对照（打印不断言：IsZoomed 门卫关热区是设计行为）----
    {
        core::window::WindowCreateRequest maxRequest = request;
        maxRequest.maximized = true;
        maxRequest.title = "window_shell_probe (maximized)";
        core::window::Handle maxHandle = core::window::createWindow(maxRequest);
        if (maxHandle != nullptr) {
            glfwPollEvents();
            HWND maxHwnd = static_cast<HWND>(
                core::window::nativeWindowInfo(maxHandle).platformWindow);
            if (maxHwnd != nullptr) {
                core::window::setWindowCaptionHitTestHandler(
                    maxHandle,
                    [](int x, int y, int width, int height, bool maximized) {
                        (void)x; (void)width; (void)maximized;
                        return y >= 0 && y < 40 && height > 0;
                    }, /*resizable=*/true);
                RECT maxClient{};
                RECT maxWindow{};
                GetClientRect(maxHwnd, &maxClient);
                GetWindowRect(maxHwnd, &maxWindow);
                const LRESULT left = SendMessageW(
                    maxHwnd, WM_NCHITTEST, 0,
                    screenLParamOf(maxHwnd, 1, maxClient.bottom / 2));
                std::printf("maximized variant: zoomed=%d window=%ldx%ld@(%ld,%ld) "
                            "client=%ldx%ld edge-left got %s (by-design HTCLIENT "
                            "when maximized)\n",
                            IsZoomed(maxHwnd) != FALSE,
                            maxWindow.right - maxWindow.left,
                            maxWindow.bottom - maxWindow.top, maxWindow.left,
                            maxWindow.top, maxClient.right, maxClient.bottom,
                            htName(left));
            }
            core::window::destroyWindow(maxHandle);
        }
    }

    // 真窗口驻留 holdSeconds 秒（肉眼可验拖拽/双击），期间事件泵不断流
    for (int elapsed = 0; elapsed < holdSeconds * 20; ++elapsed) {
        glfwPollEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // ---- ⑤b 卸载还原：安装期补的样式位逐位还原（与安装前样式全等）----
    // 「缺动画」与 resize 样式位的卸载对称性：测试/换回调场景反复装卸时样式
    // 必须可逆，不留永久污染
    {
        const LONG_PTR styleBeforeUninstall = GetWindowLongPtrW(hwnd, GWL_STYLE);
        core::window::setWindowCaptionHitTestHandler(handle, {}, true);
        const LONG_PTR styleAfterUninstall = GetWindowLongPtrW(hwnd, GWL_STYLE);
        char restoreLine[192];
        std::snprintf(restoreLine, sizeof(restoreLine),
                      "uninstall restores style (0x%llx -> 0x%llx, base 0x%llx)",
                      static_cast<unsigned long long>(styleBeforeUninstall),
                      static_cast<unsigned long long>(styleAfterUninstall),
                      static_cast<unsigned long long>(styleBeforeInstall));
        check(restoreLine,
              styleAfterUninstall == styleBeforeInstall &&
                  (styleBeforeUninstall & WS_THICKFRAME) != 0 &&
                  (styleBeforeUninstall & WS_MINIMIZEBOX) != 0 &&
                  (styleBeforeUninstall & WS_MAXIMIZEBOX) != 0);
    }

    core::window::destroyWindow(handle);
    std::printf("window_shell_probe: %d/%d checks passed\n", checks - failures,
                checks);
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const Args args = parseArgs(argc, argv);
    if (!glfwInit()) {
        std::printf("FAIL glfwInit\n");
        return 1;
    }
    const int exitCode = probeWindowShell(args.drag, args.holdSeconds);
    glfwTerminate();
    return exitCode;
}

#endif // _WIN32
