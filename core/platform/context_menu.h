// core/platform/context_menu.h
// 原生右键菜单（平台无关接口）：小尺寸覆盖窗（桌宠 sprite 窗等）内的自绘
// 菜单会被窗口 bounds 裁剪，原生弹出菜单不受限。Windows 走 Win32
// CreatePopupMenu/TrackPopupMenuEx（复用托盘菜单的构造模式）；其他平台
// stub 返回 nullopt，调用方可回退自绘菜单。接口隔离模式与
// window_style / window_effect 一致（*_win.cpp / *_stub.cpp 按平台选择）。
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace core::platform {

struct ContextMenuItem {
    // "-" = 分隔线（disabled/checked/children 忽略）
    std::string text;
    bool disabled = false;
    bool checked = false;
    // 非空 = 子菜单（一级；更深层递归支持但常规菜单用不到）
    std::vector<ContextMenuItem> children;
};

// 菜单树 → 扁平命令路径表（深度优先、同序）：第 n 个可选中项的索引路径，
// 顶层项 {i}、子菜单项 {i, j}。分隔线不占命令位。showContextMenu 的命令 id
// 即「100 + 此表下标」，两侧遍历规则必须一致（win 实现里有 debug 断言对齐）。
inline void contextMenuCommandPathsRecursive(const std::vector<ContextMenuItem>& items,
                                              const std::vector<int>& prefix,
                                              std::vector<std::vector<int>>& out) {
    for (std::size_t i = 0; i < items.size(); ++i) {
        const ContextMenuItem& item = items[i];
        if (item.text == "-") {
            continue;
        }
        std::vector<int> path = prefix;
        path.push_back(static_cast<int>(i));
        out.push_back(path);
        if (!item.children.empty()) {
            contextMenuCommandPathsRecursive(item.children, path, out);
        }
    }
}

inline std::vector<std::vector<int>> contextMenuCommandPaths(
    const std::vector<ContextMenuItem>& items) {
    std::vector<std::vector<int>> paths;
    contextMenuCommandPathsRecursive(items, {}, paths);
    return paths;
}

/**
 * @brief 在光标处同步弹出原生右键菜单（阻塞到用户选择/取消）。
 *
 * 模态消息循环：调用方主循环在菜单期间不推进（与托盘菜单同一模式，短生命周期
 * 菜单可接受）。仅在主线程调用。
 *
 * @param nativeWindowHandle 宿主窗口的原生句柄（Windows HWND；经
 *        core::window::nativeWindowInfo(handle).platformWindow 取得）
 * @return 选择项的索引路径（顶层 {i} / 子菜单 {i,j}，与传入 items 同构）；
 *         用户取消/点击别处返回空 vector；平台不支持或参数无效返回 nullopt
 *        （调用方可回退自绘菜单）。
 */
std::optional<std::vector<int>> showContextMenu(void* nativeWindowHandle,
                                                const std::vector<ContextMenuItem>& items);

} // namespace core::platform
