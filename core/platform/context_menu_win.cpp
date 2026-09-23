// core/platform/context_menu_win.cpp
// Win32 实现：CreatePopupMenu + InsertMenuItemW（UTF-16，复用 tray_bridge 的
// eui_tray_utf8_to_utf16——ANSI 菜单 API 在非 UTF-8 代码页下中文乱码）+
// TrackPopupMenuEx(TPM_RETURNCMD) 在光标处弹出。命令 id = 100 + 扁平路径
// 下标（路径表见 context_menu.h 的 contextMenuCommandPaths），选择后反查
// 索引路径返回。SetForegroundWindow/WM_NULL 是弹出菜单在别处点击时能正确
// 关闭的老坑修复（MSDN 文档化做法，托盘菜单同款）。
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

// 递归构建 HMENU；flatPaths 与命令 id 同步增长，顺序必须与
// contextMenuCommandPaths 的深度优先遍历一致（下方 debug 断言对齐）
void appendMenuItems(HMENU menu,
                     const std::vector<ContextMenuItem>& items,
                     const std::vector<int>& prefix,
                     std::vector<std::vector<int>>& flatPaths) {
    for (std::size_t i = 0; i < items.size(); ++i) {
        const ContextMenuItem& item = items[i];
        if (item.text == "-") {
            InsertMenuW(menu, GetMenuItemCount(menu), MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
            continue;
        }
        std::vector<int> path = prefix;
        path.push_back(static_cast<int>(i));
        const UINT cmd = kFirstContextMenuCmd + static_cast<UINT>(flatPaths.size());
        flatPaths.push_back(std::move(path));

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
            appendMenuItems(sub, item.children, flatPaths.back(), flatPaths);
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

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return std::nullopt;
    }
    std::vector<std::vector<int>> flatPaths;
    appendMenuItems(menu, items, {}, flatPaths);
    if (GetMenuItemCount(menu) == 0) {
        DestroyMenu(menu);
        return std::nullopt;
    }
    // 构造序与纯函数遍历序必须一致，否则命令 id 反查错位（分隔线跳过规则同步）
    assert(flatPaths.size() == contextMenuCommandPaths(items).size());

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
    if (index >= flatPaths.size()) {
        return std::vector<int>{};
    }
    return flatPaths[index];
}

} // namespace core::platform
