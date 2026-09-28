// core/platform/window_effect_macos.mm —— macOS 窗口磨砂实现
//（2026-09-28 macos-three-features 设计 §5.2；磨砂全链见
// 2026-09-22-eui-window-frost-design.md）
//
// 不引任何 vibrancy crate（设计 D-2）：ObjC++ 直挂 NSVisualEffectView。
// Tahoe 风险门（设计 D-3）：挂载失败（异常/无效句柄）→ 返回 false → 上层既有
// 降级链（glfw_app_main applyWindowEffectToWindow → degradedWindowEffect →
// activeWindowEffect 回写）自动降级 Transparent，不崩、不阻塞。
#import <Cocoa/Cocoa.h>

#include "window_effect.h"

namespace core::platform {

namespace {

// 已创建的材质视图（进程级复用；attach 时按目标窗口重挂）
NSVisualEffectView* g_effect_view = nil;

NSVisualEffectView* effectView() {
    if (g_effect_view == nil) {
        g_effect_view = [[NSVisualEffectView alloc] initWithFrame:NSZeroRect];
        // classic 材质组合（设计 §5.2；⚠️ 材质观感待 Mac 实机定，spec §13-2）：
        // HUDWindow 深色高对比 / UnderWindowBackground 通用背景二选一（可换）
        [g_effect_view setMaterial:NSVisualEffectMaterialHUDWindow];
        [g_effect_view setBlendingMode:NSVisualEffectBlendingModeWithinWindow];
        [g_effect_view setState:NSVisualEffectStateActive];
        [g_effect_view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    }
    return g_effect_view;
}

// 把材质视图垫到 contentView（GL 渲染视图）之下：插入 contentView 的宿主视图
//（contentView.superview，标题窗为 theme frame 的内容区、无边框窗为窗口内容
// 根视图）并位于 contentView 正下方——GL 侧需透明帧缓冲/wantsLayer 才能透出
// 材质（⚠️ 合成顺序待实机验证，spec §12-2）。返回是否成功垫底。
bool attachEffectView(NSWindow* nsWindow, bool enabled, float alpha) {
    if (nsWindow == nil) {
        return false;
    }
    NSView* content = [nsWindow contentView];
    NSVisualEffectView* view = effectView();
    if (!enabled) {
        // 「关掉 backdrop」本身是受支持操作（已挂则摘除，未挂则无操作）
        [view removeFromSuperview];
        return true;
    }
    NSView* host = [content superview];
    if (host == nil) {
        // 无宿主视图（异常窗口形态）：挂不上 → false → 走既有降级链
        return false;
    }
    [view setAlphaValue:alpha];
    [view setFrame:[host bounds]];
    if ([view superview] != host) {
        // 换窗重挂：先从旧宿主摘除，再垫到新窗口 contentView 正下方
        [view removeFromSuperview];
        [host insertSubview:view belowSubview:content];
    }
    return true;
}

} // namespace

bool applyWindowEffect(void* nativeHandle, WindowEffect effect) {
    if (nativeHandle == nullptr) {
        return false;   // M0-1 未合入前恒发生：空句柄 → 走既有降级链
    }
    // M0 的 nativeWindowHandle 在 __APPLE__ 分支已用 glfwGetCocoaWindow 解析为
    // NSWindow*（glfw_app_main.cpp:252-267）——句柄即 NSWindow，直接桥接（与
    // window_effect_win.cpp 把句柄当 HWND 一致）；禁止再把它当 GLFWwindow*
    // 二次 glfwGetCocoaWindow（会把 NSWindow 内存当 GLFW 窗口结构解引用）
    NSWindow* nsWindow = (__bridge NSWindow*)nativeHandle;
    if (nsWindow == nil) {
        return false;
    }
    @autoreleasepool {
        @try {
            switch (effect) {
            case WindowEffect::None:
                return attachEffectView(nsWindow, false, 1.0f);
            case WindowEffect::Transparent:
                // 半透档：材质视图整体半透明（数值 ⚠️ 待 Mac 实机定，暂取 0.4）
                return attachEffectView(nsWindow, true, 0.4f);
            case WindowEffect::Acrylic:
            case WindowEffect::Mica:
            case WindowEffect::MicaAlt:
                // Frosted 语义 = Acrylic 档（DevDesk settings_config 映射）；
                // Mica 族同挂同一材质视图（观感差异并入实机复核项）
                return attachEffectView(nsWindow, true, 1.0f);
            }
        } @catch (NSException*) {
            // Tahoe 风险门：AppKit 异常（材质不受支持等）→ false → 既有降级链
            return false;
        }
    }
    return false;
}

bool applyTitleBarAppearance(void* /*nativeHandle*/,
                             const TitleBarAppearance& /*appearance*/) {
    // macOS 标题栏深浅由系统深色模式自动联动（NSWindow 无独立 API）；
    // customColor/titlebarAppearsTransparent 联动设计 §9 明确不做
    // → 恒 false（静默不刷日志，与既有 stub 语义一致）
    return false;
}

} // namespace core::platform