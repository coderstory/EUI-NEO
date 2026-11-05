#pragma once

struct GLFWwindow;

#ifdef __cplusplus
extern "C" {
#endif

void eui_ime_set_cursor_rect(struct GLFWwindow* window, double x, double y, double width, double height);
void eui_ime_set_cursor_rect_with_font(struct GLFWwindow* window, double x, double y, double width, double height, double fontHeight);
void eui_ime_install_message_filter(struct GLFWwindow* window);
void eui_ime_uninstall_message_filter(struct GLFWwindow* window);
int eui_ime_is_composing(struct GLFWwindow* window);
int eui_ime_get_composition_string_utf8(struct GLFWwindow* window, char* buffer, int bufferSize);
void eui_ime_clear_composition(struct GLFWwindow* window);

/* ImeTrace 临时诊断 sink（bug245；删诊断时移除本声明与各平台实现）：
   win=append 到 %USERPROFILE%\DevDesk\logs\ime-trace.log（DEVDESK_DATA_ROOT 可覆盖，
   ≤100 行总量自动停写）；apple/linux=no-op。段1/2/3 共用，三段坐标同一文件对齐 */
void eui_ime_trace_log(const char* line);

#ifdef __cplusplus
}
#endif
