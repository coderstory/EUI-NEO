// core/platform/context_menu_win.cpp
// Win32 实现：CreatePopupMenu + InsertMenuItemW（UTF-16，复用 tray_bridge 的
// eui_tray_utf8_to_utf16——ANSI 菜单 API 在非 UTF-8 代码页下中文乱码）+
// TrackPopupMenuEx(TPM_RETURNCMD) 在光标处弹出。
//
// 命令 id ↔ 索引路径的映射：路径表由 context_menu.h 的纯函数
// contextMenuCommandPaths 生成（唯一权威，单测覆盖），HMENU 构建只按同一
// 深度优先序（分隔线跳过、父项先于子项）递增分配 id，两侧由 showContextMenu
// 里的断言对齐。首版把路径表构建混进 HMENU 递归（flatPaths.push_back 重分配
// 使引用其元素的前缀悬空——真机症状：子菜单项选择路径乱码、派发落空，
// 桌宠「换肤」无效的根因），重构为「纯函数出表 + 构建只发号」消灭该类问题。
#include "core/platform/context_menu.h"
#include "core/platform/tray_bridge.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cassert>
#include <cstdlib>

namespace core::platform {
namespace {

// 命令 id 基址（0 是 TPM_RETURNCMD 的「取消」返回值，避开）
constexpr UINT kFirstContextMenuCmd = 100;

// 递归构建 HMENU，命令 id 从 nextCmd 深度优先递增（与 contextMenuCommandPaths
// 的遍历序一致：分隔线跳过、父项先于子项——showContextMenu 断言对齐）
void appendMenuItems(HMENU menu,
                     const std::vector<ContextMenuItem>& items,
                     UINT& nextCmd) {
    for (std::size_t i = 0; i < items.size(); ++i) {
        const ContextMenuItem& item = items[i];
        if (item.text == "-") {
            InsertMenuW(menu, GetMenuItemCount(menu), MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
            continue;
        }
        const UINT cmd = nextCmd++;

        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
        info.wID = cmd;
        info.fState = (item.disabled ? MFS_DISABLED : 0u) |
                      (item.checked ? MFS_CHECKED : 0u);
        wchar_t* text16 = eui_tray_utf8_to_utf16(item.text.c_str());
        info.dwTypeData = text16 != nullptr ? text16 : const_cast<LPWSTR>(L"");
        if (!item.children.empty()) {
            info.fMask |= MIIM_SUBMENU;
            HMENU sub = CreatePopupMenu();
            appendMenuItems(sub, item.children, nextCmd);
            info.hSubMenu = sub;
        }
        InsertMenuItemW(menu, GetMenuItemCount(menu), TRUE, &info);
        free(text16);
    }
}

} // namespace

std::optional<std::vector<int>> showContextMenu(void* nativeWindowHandle,
                                                const std::vector<ContextMenuItem>& items) {
    HWND hwnd = static_cast<HWND>(nativeWindowHandle);
    if (hwnd == nullptr || items.empty()) {
        return std::nullopt;
    }

    // 路径表先建（纯函数，唯一权威）：第 n 个可选项的命令 id = 基址 + n
    const std::vector<std::vector<int>> paths = contextMenuCommandPaths(items);
    if (paths.empty()) {
        return std::nullopt;  // 只有分隔线：无可选项
    }

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return std::nullopt;
    }
    UINT nextCmd = kFirstContextMenuCmd;
    appendMenuItems(menu, items, nextCmd);
    // HMENU 构造序必须与路径表序一致，否则命令 id 反查错位（分隔线跳过规则同步）
    assert(static_cast<std::size_t>(nextCmd - kFirstContextMenuCmd) == paths.size());
    if (GetMenuItemCount(menu) == 0) {
        DestroyMenu(menu);
        return std::nullopt;
    }

    POINT cursor{};
    GetCursorPos(&cursor);
    // 弹出菜单可靠关闭的经典前置/后置（无此处理点击菜单外不消失）
    SetForegroundWindow(hwnd);
    const int cmd = static_cast<int>(TrackPopupMenuEx(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, cursor.x, cursor.y, hwnd, nullptr));
    PostMessage(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (cmd <= 0) {
        return std::vector<int>{};  // 用户取消 / 点击菜单外
    }
    const std::size_t index = static_cast<std::size_t>(cmd) - kFirstContextMenuCmd;
    if (index >= paths.size()) {
        return std::vector<int>{};
    }
    return paths[index];
}

} // namespace core::platform
