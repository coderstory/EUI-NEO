// core/platform/context_menu_stub.cpp
// macOS/Linux stub：无原生右键菜单实现（macOS NSMenu popUp 可后续补）。
// 返回 nullopt = 平台不支持，调用方（桌宠右键菜单）回退自绘 contextMenu
// 组件——菜单在小窗内被裁剪的退化行为与既有版本一致，不比修前更差。
#include "core/platform/context_menu.h"

namespace core::platform {

std::optional<std::vector<int>> showContextMenu(void* nativeWindowHandle,
                                                const std::vector<ContextMenuItem>& items) {
    (void)nativeWindowHandle;
    (void)items;
    return std::nullopt;
}

} // namespace core::platform
