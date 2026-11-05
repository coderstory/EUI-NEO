#include "core/platform/ime_bridge.h"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include <string.h>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wingdi.h>
#include <imm.h>
#include <stdlib.h>
#include <stdio.h> /* ImeTrace 临时诊断（bug245） */

#define EUI_IME_FILTER_PROP L"EuiNeoImeFilter"

typedef struct EuiImeFilterState {
    WNDPROC previousProc;
    double cursorX;
    double cursorY;
    double cursorWidth;
    double cursorHeight;
    double fontHeight;
    BOOL hasCursorRect;
    BOOL applyingCursorRect;
} EuiImeFilterState;

static LONG eui_ime_round_long(double value) {
    return (LONG)(value >= 0.0 ? value + 0.5 : value - 0.5);
}

/* ImeTrace 临时诊断 sink（bug245）：桌面双击启动无控制台，stderr 采不到——落文件。
   ≤100 行总量自动停写（含 session 头行）；打不开不重试（避免每帧 fopen 刷盘） */
void eui_ime_trace_log(const char* line) {
    static FILE* s_traceFile = NULL;
    static int s_traceLines = 0;
    if (line == 0 || s_traceLines >= 100) {
        return;
    }
    if (s_traceFile == 0) {
        char path[MAX_PATH];
        const char* root = getenv("DEVDESK_DATA_ROOT");
        const char* up = getenv("USERPROFILE");
        if (root != 0 && root[0] != '\0') {
            _snprintf(path, sizeof(path), "%s\\logs", root);
        } else if (up != 0 && up[0] != '\0') {
            _snprintf(path, sizeof(path), "%s\\DevDesk\\logs", up);
        } else {
            s_traceLines = 100; /* 无法定位数据根 → 停写 */
            return;
        }
        path[MAX_PATH - 1] = '\0';
        CreateDirectoryA(path, NULL); /* 已存在=ERROR_ALREADY_EXISTS，忽略 */
        if (strlen(path) + strlen("\\ime-trace.log") < sizeof(path)) {
            strcat(path, "\\ime-trace.log");
            s_traceFile = fopen(path, "ab");
        }
        if (s_traceFile == 0) {
            s_traceLines = 100;
            return;
        }
        fputs("==== ImeTrace session ====\n", s_traceFile);
        s_traceLines++;
    }
    fputs(line, s_traceFile);
    fputc('\n', s_traceFile);
    fflush(s_traceFile);
    s_traceLines++;
    if (s_traceLines >= 100) {
        fclose(s_traceFile);
        s_traceFile = 0;
    }
}

static void eui_ime_apply_font(HIMC context, double fontHeight) {
    if (context == 0 || fontHeight <= 0.0) {
        return;
    }

    if (fontHeight < 12.0) {
        fontHeight = 12.0;
    }
    LOGFONTW font;
    ZeroMemory(&font, sizeof(font));
    font.lfHeight = -eui_ime_round_long(fontHeight);
    font.lfCharSet = DEFAULT_CHARSET;
    font.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(font.lfFaceName, LF_FACESIZE, L"Microsoft YaHei UI");
    ImmSetCompositionFontW(context, &font);
}

/* bug245 兜底（Chromium/Windows Terminal 同款做法）：终端光标是自绘的，系统里
   没有 caret；TSF/CUAS 关闭时部分中文 IME 无视 ImmSetCandidateWindow，直接读
   GetCaretPos 定位候选窗——没有系统 caret 就落窗口左上角。造一个 1x1 caret
   跟随光标底部供 IME 查询：
   - 只在 hwnd 持有焦点时动 caret（caret 是全线程唯一共享资源，非焦点窗口动它
     会挪走别的窗口的 caret）；
   - caret 已存在就只 SetCaretPos 挪位置，不重建（CreateCaret 会重置闪烁节拍）；
   - 焦点丢失后系统可能销毁 caret，GetCaretPos 探测失败会自动重建，无需额外状态。 */
static void eui_ime_update_fallback_caret(HWND hwnd, LONG x, LONG y) {
    if (GetFocus() != hwnd) {
        return;
    }
    POINT caretPos;
    if (!GetCaretPos(&caretPos)) {
        if (!CreateCaret(hwnd, NULL, 1, 1)) {
            return;
        }
        ShowCaret(hwnd);
    }
    SetCaretPos(x, y);
}

static void eui_ime_apply_cursor_rect(HWND hwnd,
                                      double x,
                                      double y,
                                      double width,
                                      double height,
                                      double fontHeight) {
    if (hwnd == 0) {
        return;
    }

    {
        /* ImeTrace 段3（bug245 临时诊断，≤30 条；Windows 平台入参） */
        static int ime_trace_n = 0;
        if (ime_trace_n < 30) {
            char traceLine[160];
            _snprintf(traceLine, sizeof(traceLine),
                      "ImeTrace: win rect x=%.1f y=%.1f w=%.1f h=%.1f", x, y, width, height);
            traceLine[sizeof(traceLine) - 1] = '\0'; /* 加固：_snprintf 不保证 NUL 终止 */
            eui_ime_trace_log(traceLine);
            ime_trace_n++;
        }
    }

    HIMC context = ImmGetContext(hwnd);
    if (context == 0) {
        return;
    }

    eui_ime_apply_font(context, fontHeight);

    // bug245: standard Win32 IME anchoring trio for self-drawn carets
    // (winit / Chromium imm32_manager 同款).  两条 IMM 通道都要发：
    // 只发 CANDIDATEFORM 时，部分中文 IME 的候选窗锚点取自 COMPOSITIONFORM
    // 或系统 caret，旧实现（CFS_FORCE_POSITION @ 行顶）会被无视 → 候选窗
    // 不跟光标。坐标为窗口客户区物理像素，runtime 传入前已换算，此处不做
    // DPI 二次缩放。
    const LONG caretX = eui_ime_round_long(x);
    const LONG caretY = eui_ime_round_long(y + height); /* 光标 cell 底部 */

    // ① composition 窗：CFS_POINT 锚在光标 cell 底部——拼音候选窗跟随的
    //   关键通道（winit ime.rs:116-139 同款）。
    COMPOSITIONFORM composition;
    ZeroMemory(&composition, sizeof(composition));
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos.x = caretX;
    composition.ptCurrentPos.y = caretY;
    ImmSetCompositionWindow(context, &composition);

    CANDIDATEFORM candidate;
    ZeroMemory(&candidate, sizeof(candidate));
    candidate.dwIndex = 0;
    // ② candidate 窗：CFS_EXCLUDE 排除光标 rect，候选窗不得盖住编辑行
    //   （日韩 IME 认 rcArea）；CFS_CANDIDATEPOS + ptCurrentPos 是 TSF 开启
    //   时中文 IME 认的锚点（Chromium imm32_manager.cc 同款）。
    candidate.dwStyle = CFS_EXCLUDE | CFS_CANDIDATEPOS;
    candidate.ptCurrentPos.x = caretX;
    candidate.ptCurrentPos.y = caretY;
    candidate.rcArea.left = eui_ime_round_long(x);
    candidate.rcArea.top = eui_ime_round_long(y);
    candidate.rcArea.right = eui_ime_round_long(x + width);
    candidate.rcArea.bottom = eui_ime_round_long(y + height);
    ImmSetCandidateWindow(context, &candidate);

    // ③ 1x1 系统 caret 兜底（TSF/CUAS 关闭的中文 IME 读 GetCaretPos）。
    eui_ime_update_fallback_caret(hwnd, caretX, caretY);

    ImmReleaseContext(hwnd, context);
}

static void eui_ime_reapply_cursor_rect(HWND hwnd, EuiImeFilterState* state) {
    if (state == 0 || !state->hasCursorRect || state->applyingCursorRect) {
        return;
    }
    state->applyingCursorRect = TRUE;
    eui_ime_apply_cursor_rect(hwnd,
                              state->cursorX,
                              state->cursorY,
                              state->cursorWidth,
                              state->cursorHeight,
                              state->fontHeight);
    if ((EuiImeFilterState*)GetPropW(hwnd, EUI_IME_FILTER_PROP) == state) {
        state->applyingCursorRect = FALSE;
    }
}

static LRESULT CALLBACK eui_ime_window_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    EuiImeFilterState* state = (EuiImeFilterState*)GetPropW(hwnd, EUI_IME_FILTER_PROP);
    const BOOL placementChanged = message == WM_IME_COMPOSITION ||
                                  (message == WM_IME_NOTIFY &&
                                   (wParam == IMN_OPENCANDIDATE || wParam == IMN_CHANGECANDIDATE));
    if (message == WM_IME_COMPOSITION && (lParam & GCS_COMPSTR) != 0) {
        lParam &= ~GCS_COMPSTR;
        if (lParam == 0) {
            eui_ime_reapply_cursor_rect(hwnd, state);
            return 0;
        }
    }
    LRESULT result;
    if (state != 0 && state->previousProc != 0) {
        result = CallWindowProcW(state->previousProc, hwnd, message, wParam, lParam);
    } else {
        result = DefWindowProcW(hwnd, message, wParam, lParam);
    }
    if (placementChanged) {
        eui_ime_reapply_cursor_rect(
            hwnd,
            (EuiImeFilterState*)GetPropW(hwnd, EUI_IME_FILTER_PROP));
    }
    return result;
}

void eui_ime_install_message_filter(GLFWwindow* window) {
    if (window == 0) {
        return;
    }

    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == 0 || GetPropW(hwnd, EUI_IME_FILTER_PROP) != 0) {
        return;
    }

    EuiImeFilterState* state = (EuiImeFilterState*)calloc(1, sizeof(EuiImeFilterState));
    if (state == 0) {
        return;
    }
    state->previousProc = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
    if (state->previousProc == 0) {
        free(state);
        return;
    }
    SetPropW(hwnd, EUI_IME_FILTER_PROP, state);
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)eui_ime_window_proc);
}

void eui_ime_uninstall_message_filter(GLFWwindow* window) {
    if (window == 0) {
        return;
    }

    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == 0) {
        return;
    }

    EuiImeFilterState* state = (EuiImeFilterState*)GetPropW(hwnd, EUI_IME_FILTER_PROP);
    if (state == 0) {
        return;
    }
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)state->previousProc);
    RemovePropW(hwnd, EUI_IME_FILTER_PROP);
    free(state);
}

void eui_ime_set_cursor_rect(GLFWwindow* window, double x, double y, double width, double height) {
    eui_ime_set_cursor_rect_with_font(window, x, y, width, height, height);
}

void eui_ime_set_cursor_rect_with_font(GLFWwindow* window, double x, double y, double width, double height, double fontHeight) {
    if (window == 0) {
        return;
    }

    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == 0) {
        return;
    }

    EuiImeFilterState* state = (EuiImeFilterState*)GetPropW(hwnd, EUI_IME_FILTER_PROP);
    if (state != 0) {
        state->cursorX = x;
        state->cursorY = y;
        state->cursorWidth = width;
        state->cursorHeight = height;
        state->fontHeight = fontHeight;
        state->hasCursorRect = TRUE;
    }
    eui_ime_apply_cursor_rect(hwnd, x, y, width, height, fontHeight);
}

int eui_ime_is_composing(GLFWwindow* window) {
    if (window == 0) {
        return 0;
    }

    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == 0) {
        return 0;
    }

    HIMC context = ImmGetContext(hwnd);
    if (context == 0) {
        return 0;
    }

    const LONG length = ImmGetCompositionStringW(context, GCS_COMPSTR, 0, 0);
    ImmReleaseContext(hwnd, context);
    return length > 0 ? 1 : 0;
}

int eui_ime_get_composition_string_utf8(GLFWwindow* window, char* buffer, int bufferSize) {
    if (buffer != 0 && bufferSize > 0) {
        buffer[0] = '\0';
    }
    if (window == 0 || buffer == 0 || bufferSize <= 0) {
        return 0;
    }

    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == 0) {
        return 0;
    }

    HIMC context = ImmGetContext(hwnd);
    if (context == 0) {
        return 0;
    }

    const LONG byteLength = ImmGetCompositionStringW(context, GCS_COMPSTR, 0, 0);
    if (byteLength <= 0) {
        ImmReleaseContext(hwnd, context);
        return 0;
    }

    WCHAR* wide = (WCHAR*)malloc((size_t)byteLength + sizeof(WCHAR));
    if (wide == 0) {
        ImmReleaseContext(hwnd, context);
        return 0;
    }
    const LONG copied = ImmGetCompositionStringW(context, GCS_COMPSTR, wide, byteLength);
    ImmReleaseContext(hwnd, context);
    if (copied <= 0) {
        free(wide);
        return 0;
    }
    wide[copied / (LONG)sizeof(WCHAR)] = L'\0';

    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, 0, 0, 0, 0);
    if (needed <= 0) {
        free(wide);
        return 0;
    }
    const int written = WideCharToMultiByte(CP_UTF8, 0, wide, -1, buffer, bufferSize, 0, 0);
    free(wide);
    if (written <= 0) {
        buffer[0] = '\0';
        return 0;
    }
    return written - 1;
}

void eui_ime_clear_composition(GLFWwindow* window) {
    if (window == 0) {
        return;
    }

    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == 0) {
        return;
    }

    HIMC context = ImmGetContext(hwnd);
    if (context == 0) {
        return;
    }

    ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
    ImmReleaseContext(hwnd, context);
}

#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

static char euiImeRectKey;
static Class euiImeSwizzledClass = Nil;
static IMP euiImeOriginalFirstRect = nil;

static int eui_ime_copy_nsstring_utf8(NSString* text, char* buffer, int bufferSize) {
    if (buffer != 0 && bufferSize > 0) {
        buffer[0] = '\0';
    }
    if (text == nil || buffer == 0 || bufferSize <= 0 || [text length] == 0) {
        return 0;
    }

    const char* utf8 = [text UTF8String];
    if (utf8 == 0 || utf8[0] == '\0') {
        return 0;
    }

    const NSUInteger length = [text lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
    const int copyLength = (int)MIN((NSUInteger)(bufferSize - 1), length);
    memcpy(buffer, utf8, (size_t)copyLength);
    buffer[copyLength] = '\0';
    return copyLength;
}

static NSString* eui_ime_marked_text(NSView* view) {
    if (view == nil) {
        return nil;
    }

    Ivar markedTextIvar = class_getInstanceVariable([view class], "markedText");
    if (markedTextIvar != nil) {
        id markedText = object_getIvar(view, markedTextIvar);
        if ([markedText isKindOfClass:[NSAttributedString class]]) {
            return [(NSAttributedString*)markedText string];
        }
        if ([markedText isKindOfClass:[NSString class]]) {
            return (NSString*)markedText;
        }
    }

    if ([view respondsToSelector:@selector(markedRange)] &&
        [view respondsToSelector:@selector(attributedSubstringForProposedRange:actualRange:)]) {
        NSRange markedRange = [(id)view markedRange];
        if (markedRange.location != NSNotFound && markedRange.length > 0) {
            NSRange actualRange = NSMakeRange(NSNotFound, 0);
            NSAttributedString* attributedText = [(id)view attributedSubstringForProposedRange:markedRange
                                                                                   actualRange:&actualRange];
            return [attributedText string];
        }
    }

    return nil;
}

static void eui_ime_clear_marked_text(NSView* view) {
    if (view == nil) {
        return;
    }

    Ivar markedTextIvar = class_getInstanceVariable([view class], "markedText");
    if (markedTextIvar != nil) {
        id markedText = object_getIvar(view, markedTextIvar);
        if ([markedText isKindOfClass:[NSMutableAttributedString class]]) {
            [[(NSMutableAttributedString*)markedText mutableString] setString:@""];
            return;
        }
        if ([markedText isKindOfClass:[NSAttributedString class]] ||
            [markedText isKindOfClass:[NSString class]]) {
            NSMutableAttributedString* empty = [[NSMutableAttributedString alloc] init];
            object_setIvar(view, markedTextIvar, empty);
            [empty release];
            return;
        }
    }

    if ([view respondsToSelector:@selector(unmarkText)]) {
        [(id)view unmarkText];
    }
}

static NSRect eui_ime_first_rect_for_character_range(id self, SEL selector, NSRange range, NSRangePointer actualRange) {
    if (actualRange != nil) {
        *actualRange = range;
    }

    NSValue* rectValue = objc_getAssociatedObject(self, &euiImeRectKey);
    if (rectValue != nil) {
        NSRect viewRect = [rectValue rectValue];
        NSRect windowRect = [self convertRect:viewRect toView:nil];
        return [[self window] convertRectToScreen:windowRect];
    }

    if (euiImeOriginalFirstRect != nil) {
        typedef NSRect (*FirstRectFn)(id, SEL, NSRange, NSRangePointer);
        return ((FirstRectFn)euiImeOriginalFirstRect)(self, selector, range, actualRange);
    }

    return NSMakeRect(0.0, 0.0, 0.0, 0.0);
}

static void eui_ime_prepare_view(NSView* view) {
    if (view == nil) {
        return;
    }

    Class viewClass = [view class];
    if (viewClass == euiImeSwizzledClass) {
        return;
    }

    SEL selector = @selector(firstRectForCharacterRange:actualRange:);
    Method method = class_getInstanceMethod(viewClass, selector);
    if (method == nil) {
        return;
    }

    euiImeOriginalFirstRect = method_getImplementation(method);
    method_setImplementation(method, (IMP)eui_ime_first_rect_for_character_range);
    euiImeSwizzledClass = viewClass;
}

void eui_ime_set_cursor_rect(GLFWwindow* window, double x, double y, double width, double height) {
    if (window == 0) {
        return;
    }

    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    if (nsWindow == nil) {
        return;
    }

    NSView* view = [nsWindow contentView];
    if (view == nil) {
        return;
    }

    eui_ime_prepare_view(view);

    const CGFloat scale = MAX((CGFloat)1.0, [nsWindow backingScaleFactor]);
    const NSRect bounds = [view bounds];
    const CGFloat rectX = (CGFloat)x / scale;
    const CGFloat rectWidth = MAX((CGFloat)1.0, (CGFloat)width / scale);
    const CGFloat rectHeight = MAX((CGFloat)1.0, (CGFloat)height / scale);
    const CGFloat rectY = bounds.size.height - ((CGFloat)y / scale) - rectHeight;
    const NSRect viewRect = NSMakeRect(rectX, rectY, rectWidth, rectHeight);
    objc_setAssociatedObject(view, &euiImeRectKey, [NSValue valueWithRect:viewRect], OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

void eui_ime_set_cursor_rect_with_font(GLFWwindow* window, double x, double y, double width, double height, double fontHeight) {
    (void)fontHeight;
    eui_ime_set_cursor_rect(window, x, y, width, height);
}

void eui_ime_trace_log(const char* line) {
    (void)line; /* ImeTrace 文件 sink 仅 win 实现（apple no-op） */
}

void eui_ime_install_message_filter(GLFWwindow* window) {
    if (window == 0) {
        return;
    }

    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    if (nsWindow == nil) {
        return;
    }

    eui_ime_prepare_view([nsWindow contentView]);
}

void eui_ime_uninstall_message_filter(GLFWwindow* window) {
    (void)window;
}

int eui_ime_is_composing(GLFWwindow* window) {
    if (window == 0) {
        return 0;
    }

    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    if (nsWindow == nil) {
        return 0;
    }

    NSView* view = [nsWindow contentView];
    if (view == nil) {
        return 0;
    }

    if ([view respondsToSelector:@selector(hasMarkedText)] && [(id)view hasMarkedText]) {
        return 1;
    }

    NSString* markedText = eui_ime_marked_text(view);
    return markedText != nil && [markedText length] > 0 ? 1 : 0;
}

int eui_ime_get_composition_string_utf8(GLFWwindow* window, char* buffer, int bufferSize) {
    if (buffer != 0 && bufferSize > 0) {
        buffer[0] = '\0';
    }
    if (window == 0 || buffer == 0 || bufferSize <= 0) {
        return 0;
    }

    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    if (nsWindow == nil) {
        return 0;
    }

    NSView* view = [nsWindow contentView];
    eui_ime_prepare_view(view);
    return eui_ime_copy_nsstring_utf8(eui_ime_marked_text(view), buffer, bufferSize);
}

void eui_ime_clear_composition(GLFWwindow* window) {
    if (window == 0) {
        return;
    }

    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    if (nsWindow == nil) {
        return;
    }

    eui_ime_clear_marked_text([nsWindow contentView]);
}

#elif defined(__linux__) && defined(EUI_GLFW_X11_IME_POSITION)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <math.h>

void eui_ime_set_cursor_rect(GLFWwindow* window, double x, double y, double width, double height) {
    (void)width;
    if (!window || glfwGetPlatform() != GLFW_PLATFORM_X11 || !isfinite(x) || !isfinite(y) || !isfinite(height))
        return;
    // Runtime supplies framebuffer coordinates. XIM expects window-local pixels.
    int ww, wh, fw, fh;
    glfwGetWindowSize(window, &ww, &wh);
    glfwGetFramebufferSize(window, &fw, &fh);
    if (fw <= 0 || fh <= 0)
        return;
    glfwSetX11InputMethodCursorPos(window,
        (int)fmin(32767.0, fmax(0.0, x * ww / fw)),
        (int)fmin(32767.0, fmax(0.0, (y + height) * wh / fh)));
}
void eui_ime_set_cursor_rect_with_font(GLFWwindow* window, double x, double y, double width, double height, double fontHeight) {
    (void)fontHeight;
    eui_ime_set_cursor_rect(window, x, y, width, height);
}
void eui_ime_trace_log(const char* line) { (void)line; } /* 文件 sink 仅 win（linux/else no-op） */
void eui_ime_install_message_filter(GLFWwindow* window) { (void)window; }
void eui_ime_uninstall_message_filter(GLFWwindow* window) { (void)window; }
int eui_ime_is_composing(GLFWwindow* window) { (void)window; return 0; }
int eui_ime_get_composition_string_utf8(GLFWwindow* window, char* buffer, int bufferSize) {
    (void)window;
    if (buffer && bufferSize > 0) buffer[0] = '\0';
    return 0;
}
void eui_ime_clear_composition(GLFWwindow* window) { (void)window; }

#else

void eui_ime_set_cursor_rect(GLFWwindow* window, double x, double y, double width, double height) {
    (void)window;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

void eui_ime_set_cursor_rect_with_font(GLFWwindow* window, double x, double y, double width, double height, double fontHeight) {
    (void)fontHeight;
    eui_ime_set_cursor_rect(window, x, y, width, height);
}

void eui_ime_trace_log(const char* line) {
    (void)line; /* 文件 sink 仅 win（else no-op） */
}

void eui_ime_install_message_filter(GLFWwindow* window) {
    (void)window;
}

void eui_ime_uninstall_message_filter(GLFWwindow* window) {
    (void)window;
}

int eui_ime_is_composing(GLFWwindow* window) {
    (void)window;
    return 0;
}

int eui_ime_get_composition_string_utf8(GLFWwindow* window, char* buffer, int bufferSize) {
    (void)window;
    if (buffer != 0 && bufferSize > 0) {
        buffer[0] = '\0';
    }
    return 0;
}

void eui_ime_clear_composition(GLFWwindow* window) {
    (void)window;
}

#endif
