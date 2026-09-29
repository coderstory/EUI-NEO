#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
/* UTF-8 → UTF-16（malloc 分配，调用方 free；失败返回 NULL）。
 * Win32 托盘菜单走 W 系 API，中文等非 ASCII 文本须经此转换（GBK 代码页下
 * ANSI 版会乱码）。tray.h 的 Win32 patch 与单元测试共用。 */
wchar_t* eui_tray_utf8_to_utf16(const char* utf8);
#endif

/* 托盘菜单单项。text 为 NULL 表示分隔线；cb 非空时点击该项会以 user 调用。 */
typedef struct eui_tray_menu_item {
    const char* text;
    int disabled;
    int checked;
    void (*cb)(void* user);
    void* user;
} eui_tray_menu_item;

#define EUI_TRAY_MAX_MENU_ITEMS 32

/*
 * 注册自定义托盘菜单（在 eui_tray_init 之前或之后调用均可）。
 * - items == NULL 或 count == 0：清空自定义项，回退默认 Show/Exit。
 * - count > 0 且 keep_default != 0：自定义项 + 分隔线 + Show + Exit。
 * - count > 0 且 keep_default == 0：仅自定义项（应用自行提供退出入口）。
 * text 指针由调用方持有，须保持有效直到下一次 set_menu / shutdown。
 */
void eui_tray_set_menu(const eui_tray_menu_item* items, int count, int keep_default);

/*
 * 菜单接管钩子（当前仅 Windows tray.h Win32 分支触发）。
 * fn(x, y, left_button)：托盘图标按下（左/右）在弹原生菜单前调用，坐标为
 * 光标物理像素（GetCursorPos）。返回非 0 = 上层已接管菜单展示，跳过原生
 * 菜单；返回 0 = 按原行为弹原生菜单。fn 传 NULL 注销钩子。
 */
typedef int (*eui_tray_menu_requested_fn)(int x, int y, int left_button);
void eui_tray_set_menu_requested_fn(eui_tray_menu_requested_fn fn);

/*
 * 纯函数：把自定义项与默认 Show/Exit 合成最终菜单，写入 out（容量 out_capacity）。
 * 返回实际项数。默认项使用调用方传入的 show_cb/exit_cb（user 为 NULL 调用）。
 * 供各平台后端与单元测试共用。
 */
int eui_tray_expand_menu(const eui_tray_menu_item* custom, int custom_count,
                         int keep_default,
                         void (*show_cb)(void* user), void (*exit_cb)(void* user),
                         eui_tray_menu_item* out, int out_capacity);

int eui_tray_init(const char* icon_path);
int eui_tray_is_initialized(void);
void eui_tray_poll(int blocking);
int eui_tray_consume_show_requested(void);
int eui_tray_consume_exit_requested(void);
/*
 * 应用级「带回前台」（macOS Dock reopen 复原主窗用）：
 * - AppKit 后端：应用隐藏态（Cmd+H）先 unhideWithoutActivation 解除隐藏，
 *   再 activateIgnoringOtherApps:YES 抢焦点——GLFW 的 show/focus 不解除
 *   应用隐藏，Dock 点击期望无论哪一态都把应用带回前台。
 * - 其余后端 / 无后端：空操作（Windows/Linux 无对应应用级语义）。
 * 未初始化托盘也可调用（只动 NSApp，不触碰托盘对象）。
 */
void eui_tray_activate_app(void);
void eui_tray_shutdown(void);

#ifdef __cplusplus
}
#endif
