// core/platform/context_menu_macos.mm —— macOS 原生右键菜单
//（2026-09-28 macos-three-features 设计 §6.2）
// 桌宠右键菜单由自绘 contextMenu 组件升级为原生 NSMenu 弹出；
// 命令 = 扁平命令路径表下标（对齐 context_menu.h / win 实现语义）。
#import <Cocoa/Cocoa.h>

#include <optional>
#include <vector>

#include "context_menu.h"

// 弹出期间记录选中结果（popUp 同步返回，主线程使用安全）
// 注：ObjC 类只能在全局作用域声明（@implementation 方法体按文件作用域查名），
// 故下列被回调访问的量一并放文件作用域；static 保持原匿名 namespace 的内部
// 链接，语义不变。
static const std::vector<std::vector<int>>* g_popup_paths = nullptr;
static std::vector<int> g_selected_command_path;
static bool g_selected_any = false;

// 选中回调目标：action 必须是对象方法（C 函数不能作 selector），
// 与 tray_bridge 的 EUITrayTarget 同模式
@interface EUIContextMenuTarget : NSObject
- (void)itemPicked:(id)sender;
@end

@implementation EUIContextMenuTarget
- (void)itemPicked:(id)sender {
    NSMenuItem* item = (NSMenuItem*)sender;
    const NSInteger idx = [item tag];
    g_selected_any = true;
    g_selected_command_path =
        (g_popup_paths != nullptr && idx >= 0 &&
         idx < (NSInteger)g_popup_paths->size())
            ? (*g_popup_paths)[(size_t)idx]
            : std::vector<int>{};
}
@end

namespace core::platform {

namespace {

EUIContextMenuTarget* g_context_menu_target = nil;

// 深度优先构建 NSMenu；可选中项分配顺序与 contextMenuCommandPaths 的 DFS
// 完全一致（分隔线不占命令位），两侧规则对齐（win 实现里同样的 debug 断言）。
NSUInteger g_next_command_index = 0;

void buildMenuItems(NSMenu* menu, const std::vector<ContextMenuItem>& items,
                    id target) {
    for (const ContextMenuItem& item : items) {
        if (item.text == "-") {
            [menu addItem:[NSMenuItem separatorItem]];
            continue;
        }
        NSString* title = [NSString stringWithUTF8String:item.text.c_str()];
        const bool leaf = item.children.empty();
        NSMenuItem* mi =
            [[NSMenuItem alloc] initWithTitle:(title != nil ? title : @"")
                                       action:leaf ? @selector(itemPicked:) : nil
                                keyEquivalent:@""];
        [mi setTarget:leaf ? target : nil];
        [mi setEnabled:item.disabled ? NO : YES];
        if (item.checked) {
            [mi setState:NSOnState];
        }
        [mi setTag:(NSInteger)g_next_command_index++];   // 扁平命令路径表下标
        if (leaf) {
            [menu addItem:mi];
            [mi release];
        } else {
            NSMenu* sub = [[NSMenu alloc] initWithTitle:(title != nil ? title : @"")];
            buildMenuItems(sub, item.children, target);
            [menu setSubmenu:sub forItem:mi];
            [sub release];
            [mi release];
        }
    }
}

} // namespace

std::optional<std::vector<int>> showContextMenu(
    void* nativeHandle, const std::vector<ContextMenuItem>& items) {
    if (nativeHandle == nullptr) {
        return std::nullopt;   // 句柄无效 → 调用方回退自绘菜单（与 stub 语义一致）
    }
    // 入参句柄在 __APPLE__ 下已由上游解析为 NSWindow*（glfw_app_main.cpp
    // nativeWindowHandle / nativeWindowInfo().platformWindow），与
    // window_effect_macos.mm 同一约定——禁止再按 GLFWwindow* 二次
    // glfwGetCocoaWindow（会把 NSWindow 内存按 GLFW 窗口结构解引用）。
    NSWindow* nsWindow = (__bridge NSWindow*)nativeHandle;
    if (nsWindow == nil) {
        return std::nullopt;
    }
    @autoreleasepool {
        // 命令路径表：复用头内 inline 纯函数（菜单树 → 扁平索引路径，DFS 同序）
        const std::vector<std::vector<int>> paths = contextMenuCommandPaths(items);
        g_popup_paths = &paths;
        g_selected_any = false;
        g_selected_command_path.clear();
        g_next_command_index = 0;
        if (g_context_menu_target == nil) {
            g_context_menu_target = [[EUIContextMenuTarget alloc] init];
        }

        NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
        [menu setAutoenablesItems:NO];
        buildMenuItems(menu, items, g_context_menu_target);

        // ⚠️ 坐标系（设计 §6.2 注记）：atLocation 是 contentView 视图坐标、
        // 原点左下。当前鼠标的屏幕坐标（同为左下原点）经
        // convertPoint:fromView:nil 一步换算成视图坐标即可——**不要**再做
        // 手动翻转（翻转仅适用于上层给的是左上原点逻辑坐标的场景）；
        // 也勿与托盘 mouseLocation 的屏系翻转（屏高 − y）混用，两套原点。
        NSView* content = [nsWindow contentView];
        const NSPoint screenPoint = [NSEvent mouseLocation];
        const NSPoint viewPoint = [content convertPoint:screenPoint fromView:nil];
        [menu popUpMenuPositioningItem:nil atLocation:viewPoint inView:content];

        [menu release];
        g_popup_paths = nullptr;
        if (!g_selected_any) {
            return std::nullopt;   // 点空白/取消 → nullopt（调用方回退）
        }
        return std::vector<int>(g_selected_command_path);
    }
}

} // namespace core::platform