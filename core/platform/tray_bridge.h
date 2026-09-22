#pragma once

#ifdef __cplusplus
extern "C" {
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
void eui_tray_shutdown(void);

#ifdef __cplusplus
}
#endif
