#include "core/platform/platform.h"
#include "core/platform/tray_bridge.h"
#include "eui/dsl_app.h"

#include <cassert>
#include <string>

// ---------------------------------------------------------------------------
// eui_tray_expand_menu：菜单合成纯函数（各平台后端共用）
// ---------------------------------------------------------------------------

namespace {

int g_show_calls = 0;
int g_exit_calls = 0;
int g_custom_calls = 0;
void* g_last_custom_user = nullptr;

void test_show_cb(void* user) {
    (void)user;
    ++g_show_calls;
}

void test_exit_cb(void* user) {
    (void)user;
    ++g_exit_calls;
}

void test_custom_cb(void* user) {
    ++g_custom_calls;
    g_last_custom_user = user;
}

} // namespace

static void testExpandEmptyFallsBackToDefaults() {
    eui_tray_menu_item out[EUI_TRAY_MAX_MENU_ITEMS + 3];
    int n = eui_tray_expand_menu(nullptr, 0, 0, &test_show_cb, &test_exit_cb,
                                 out, EUI_TRAY_MAX_MENU_ITEMS + 3);
    assert(n == 2);
    assert(std::string(out[0].text) == "Show" && out[0].cb == &test_show_cb);
    assert(std::string(out[1].text) == "Exit" && out[1].cb == &test_exit_cb);

    // 负数 count 同样视为空注册
    n = eui_tray_expand_menu(nullptr, -3, 1, &test_show_cb, &test_exit_cb,
                             out, EUI_TRAY_MAX_MENU_ITEMS + 3);
    assert(n == 2);
}

static void testExpandCustomKeepsDefaults() {
    int user_value = 42;
    eui_tray_menu_item custom[2] = {};
    custom[0].text = "快速操作";
    custom[0].cb = &test_custom_cb;
    custom[0].user = &user_value;
    custom[1].text = nullptr;   // 分隔线

    eui_tray_menu_item out[EUI_TRAY_MAX_MENU_ITEMS + 3];
    int n = eui_tray_expand_menu(custom, 2, 1, &test_show_cb, &test_exit_cb,
                                 out, EUI_TRAY_MAX_MENU_ITEMS + 3);
    assert(n == 5);   // 2 自定义 + 分隔线 + Show + Exit
    assert(std::string(out[0].text) == "快速操作" && out[0].cb == &test_custom_cb);
    assert(out[1].text == nullptr);   // 自定义分隔线原样保留
    assert(out[2].text == nullptr);   // 默认组前的分隔线
    assert(std::string(out[3].text) == "Show");
    assert(std::string(out[4].text) == "Exit");

    // 回调透传：user 指针原样送达
    out[0].cb(out[0].user);
    assert(g_custom_calls == 1 && g_last_custom_user == &user_value);
    out[3].cb(out[3].user);
    out[4].cb(out[4].user);
    assert(g_show_calls == 1 && g_exit_calls == 1);
}

static void testExpandCustomWithoutDefaults() {
    eui_tray_menu_item custom[1] = {};
    custom[0].text = "退出 DevDesk";
    custom[0].cb = &test_custom_cb;

    eui_tray_menu_item out[EUI_TRAY_MAX_MENU_ITEMS + 3];
    int n = eui_tray_expand_menu(custom, 1, 0, &test_show_cb, &test_exit_cb,
                                 out, EUI_TRAY_MAX_MENU_ITEMS + 3);
    assert(n == 1);
    assert(std::string(out[0].text) == "退出 DevDesk");
}

static void testExpandRespectsCapacity() {
    eui_tray_menu_item custom[4] = {};
    for (int i = 0; i < 4; i++) {
        custom[i].text = "item";
    }
    eui_tray_menu_item out[2];
    int n = eui_tray_expand_menu(custom, 4, 1, &test_show_cb, &test_exit_cb, out, 2);
    assert(n == 2);   // 按容量截断，不越界

    // 非法参数防御
    assert(eui_tray_expand_menu(custom, 4, 1, &test_show_cb, &test_exit_cb, nullptr, 8) == 0);
    assert(eui_tray_expand_menu(custom, 4, 1, &test_show_cb, &test_exit_cb, out, 0) == 0);
    assert(eui_tray_expand_menu(custom, 4, 1, nullptr, &test_exit_cb, out, 2) == 0);
}

// ---------------------------------------------------------------------------
// C++ 数据结构与 DSL 注册
// ---------------------------------------------------------------------------

static void testTrayMenuItemDefaults() {
    core::platform::TrayMenuItem item;
    assert(item.text.empty() && !item.callback);
    assert(!item.isSeparator());
    item.text = "-";
    assert(item.isSeparator());
}

static void testDslTrayMenuRegistration() {
    int hits = 0;
    app::DslAppConfig config = app::DslAppConfig{}
        .tray(true)
        .trayMenu({
            {"快速笔记", [&hits] { ++hits; }},
            {"-", nullptr},
        });
    assert(config.trayEnabledValue);
    assert(config.trayMenuValue.size() == 2);
    assert(config.trayMenuValue[0].text == "快速笔记");
    assert(static_cast<bool>(config.trayMenuValue[0].callback));
    assert(!config.trayMenuValue[0].isSeparator());
    assert(config.trayMenuValue[1].isSeparator());
    assert(!static_cast<bool>(config.trayMenuValue[1].callback));
    assert(config.trayKeepDefaultMenuValue);   // 默认保留 Show/Exit

    config.trayMenuValue[0].callback();
    assert(hits == 1);

    // keepDefault=false：仅自定义项
    config.trayMenu({{"退出", nullptr}}, false);
    assert(config.trayMenuValue.size() == 1);
    assert(!config.trayKeepDefaultMenuValue);

    // 向后兼容：不调用 trayMenu 时为空
    assert(app::DslAppConfig{}.trayMenuValue.empty());
    assert(app::DslAppConfig{}.trayKeepDefaultMenuValue);
}

static void testSetTrayMenuSmoke() {
    // 未初始化托盘时仅存储注册，不创建任何平台资源
    core::platform::setTrayMenu({
        {"A", [] {}},
        {"-", nullptr},
        {"B", [] {}},
    });
    assert(!core::platform::isTrayInitialized());
    core::platform::setTrayMenu({}, false);
    assert(!core::platform::isTrayInitialized());
}

int main() {
    testExpandEmptyFallsBackToDefaults();
    testExpandCustomKeepsDefaults();
    testExpandCustomWithoutDefaults();
    testExpandRespectsCapacity();
    testTrayMenuItemDefaults();
    testDslTrayMenuRegistration();
    testSetTrayMenuSmoke();
    return 0;
}
