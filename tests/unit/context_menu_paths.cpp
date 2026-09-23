// tests/unit/context_menu_paths.cpp
// contextMenuCommandPaths（原生右键菜单的命令路径表）纯逻辑单测：
//   1. 空菜单 → 空路径表
//   2. 顶层项顺序编号 {0},{1},...
//   3. 分隔线不占命令位（后续顶层项编号不受影响）
//   4. 子菜单项 = 父路径 + 子下标（{1,0},{1,1}），与父项同序深搜
//   5. 子菜单里的分隔线同样跳过
//   6. showContextMenu 空句柄/空菜单守卫：nullopt（win 与 stub 同语义）
// 真实 HMENU 构建 + TrackPopupMenuEx 弹出需真窗口与交互，属真机验证
// 范畴（DevDesk 桌宠右键菜单，截屏证据见 DevDesk 仓 fix/winfx-pet）。
#include "core/platform/context_menu.h"

#include <cassert>
#include <cstdio>
#include <vector>

namespace {

core::platform::ContextMenuItem item(const char* text) {
    core::platform::ContextMenuItem out;
    out.text = text;
    return out;
}

} // namespace

int main() {
    using core::platform::contextMenuCommandPaths;
    using core::platform::showContextMenu;

    // ---- 1. 空菜单 ----
    assert(contextMenuCommandPaths({}).empty());

    // ---- 2. 顶层项顺序编号 ----
    const std::vector<core::platform::ContextMenuItem> flat{
        item("穿透"),
        item("位置重置"),
        item("退出"),
    };
    const auto flatPaths = contextMenuCommandPaths(flat);
    assert(flatPaths.size() == 3);
    assert((flatPaths[0] == std::vector<int>{0}));
    assert((flatPaths[1] == std::vector<int>{1}));
    assert((flatPaths[2] == std::vector<int>{2}));

    // ---- 3. 分隔线不占命令位 ----
    const std::vector<core::platform::ContextMenuItem> withSep{
        item("穿透"),
        item("-"),  // 分隔线
        item("退出"),
    };
    const auto sepPaths = contextMenuCommandPaths(withSep);
    assert(sepPaths.size() == 2);                  // 分隔线无路径
    assert((sepPaths[1] == std::vector<int>{2}));  // 顶层下标仍是 2（原始位置）

    // ---- 4. 子菜单深搜：父项后紧跟其子项 ----
    core::platform::ContextMenuItem skinMenu = item("换肤");
    skinMenu.children = {item("duck"), item("cat")};
    const std::vector<core::platform::ContextMenuItem> tree{
        item("穿透"),
        std::move(skinMenu),
        item("退出"),
    };
    const auto treePaths = contextMenuCommandPaths(tree);
    assert(treePaths.size() == 5);  // 穿透 + 换肤 + 2 皮肤 + 退出
    assert((treePaths[0] == std::vector<int>{0}));
    assert((treePaths[1] == std::vector<int>{1}));     // 换肤父项
    assert((treePaths[2] == std::vector<int>{1, 0}));  // duck
    assert((treePaths[3] == std::vector<int>{1, 1}));  // cat
    assert((treePaths[4] == std::vector<int>{2}));     // 退出

    // ---- 5. 子菜单内的分隔线同样跳过 ----
    core::platform::ContextMenuItem skinMenu2 = item("换肤");
    skinMenu2.children = {item("duck"), item("-"), item("cat")};
    const auto subSepPaths = contextMenuCommandPaths(
        {item("wrap"), std::move(skinMenu2)});
    assert(subSepPaths.size() == 4);  // wrap + 换肤父 + duck + cat（子分隔线跳过）
    assert((subSepPaths[3] == std::vector<int>{1, 2}));  // cat 保留原始子下标 2

    // ---- 6. showContextMenu 守卫：空句柄 / 空菜单 → nullopt（无交互）----
    assert(!showContextMenu(nullptr, flat).has_value());
    assert(!showContextMenu(nullptr, {}).has_value());

    std::printf("context_menu_paths: all assertions passed\n");
    return 0;
}
