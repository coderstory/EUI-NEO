// core/platform/window_effect_macos.mm —— macOS 窗口磨砂实现
//（2026-09-28 macos-three-features 设计 §5.2；磨砂全链见
// 2026-09-22-eui-window-frost-design.md）
//
// 不引任何 vibrancy crate（设计 D-2）：ObjC++ 直挂 NSVisualEffectView。
// Tahoe 风险门（设计 D-3）：挂载失败（异常/无效句柄）→ 返回 false → 上层既有
// 降级链（glfw_app_main applyWindowEffectToWindow → degradedWindowEffect →
// activeWindowEffect 回写）自动降级 Transparent，不崩、不阻塞。
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include "window_effect.h"

#include <cstdio>
#include <cstdlib>

namespace core::platform {

namespace {

// 材质视图**按窗口各一份**（关联对象挂在 NSWindow 上，随窗口释放）。原先的
// 进程级单例（g_effect_view）会让多个窗口互抢同一视图：桌宠/模态子窗
// apply(None) 时把主窗已挂的材质视图从主窗层级摘走（真机取证
// attach:off:before win=<pet> viewSuperview=NSThemeFrame viewWindow=<main>），
// 主窗磨砂随之失效，且 re-parent 结构下还会连带 GL 视图宿主一起丢。
const void* kEffectViewKey = &kEffectViewKey;
const void* kHostedContentViewKey = &kHostedContentViewKey;

NSView* hostedContentView(NSVisualEffectView* view) {
    if (view == nil) {
        return nil;
    }
    return objc_getAssociatedObject(view, kHostedContentViewKey);
}

NSVisualEffectView* effectViewFor(NSWindow* nsWindow, bool create) {
    if (nsWindow == nil) {
        return nil;
    }
    NSVisualEffectView* view = objc_getAssociatedObject(nsWindow, kEffectViewKey);
    if (view == nil && create) {
        view = [[NSVisualEffectView alloc] initWithFrame:NSZeroRect];
        // classic 材质组合（设计 §5.2；⚠️ 材质观感待 Mac 实机定，spec §13-2）：
        // HUDWindow 深色高对比 / UnderWindowBackground 通用背景二选一（可换）
        [view setMaterial:NSVisualEffectMaterialHUDWindow];
        [view setBlendingMode:NSVisualEffectBlendingModeWithinWindow];
        [view setState:NSVisualEffectStateActive];
        [view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
        objc_setAssociatedObject(nsWindow, kEffectViewKey, view,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    return view;
}

// —— AppKit 侧状态诊断（EUI_FX_DEBUG=1 时输出 stderr）——
// macOS 窗口效果没有可断言的客观量可看（材质是否真挂上、窗口是否不透明、
// 标题栏有无背景），且 screencapture 常被 TCC 拦——诊断行即为验收手段：
// [fxdiag] 每行含 NSWindow 不透明度/背景色/内容视图类与 frame/材质视图
// 材质·混合模式·状态·alpha·宿主，可逐条判定「哪一层生效」。
bool fxDebug() {
    static const bool enabled = []() {
        const char* v = std::getenv("EUI_FX_DEBUG");
        return v != nullptr && *v != '\0';
    }();
    return enabled;
}

const char* fxCls(NSView* view) {
    return view == nil ? "(nil)" : [[[view class] description] UTF8String];
}

void fxDump(const char* tag, NSWindow* w, NSVisualEffectView* view, bool enabledArg, double alphaArg,
            bool insertedNow) {
    if (!fxDebug()) {
        return;
    }
    NSView* content = [w contentView];
    NSView* host = [content superview];
    const NSRect hostBounds = host != nil ? [host bounds] : NSZeroRect;
    const NSRect viewFrame = view != nil ? [view frame] : NSZeroRect;
    NSView* hosted = hostedContentView(view);
    const NSRect hostedFrame = hosted != nil ? [hosted frame] : NSZeroRect;
    NSString* bgDesc = @"(nil)";
    if (NSColor* bg = [w backgroundColor]; bg != nil) {
        if (NSColor* srgb = [bg colorUsingColorSpace:[NSColorSpace sRGBColorSpace]]; srgb != nil) {
            bgDesc = [NSString stringWithFormat:@"srgb(%.3f,%.3f,%.3f,%.3f)",
                                                [srgb redComponent], [srgb greenComponent],
                                                [srgb blueComponent], [srgb alphaComponent]];
        } else {
            bgDesc = [bg description];
        }
    }
    std::fprintf(
        stderr,
        "[fxdiag] %s win=%p enabled=%d alpha=%.2f inserted=%d | isOpaque=%d bg=%s bgIsClear=%d "
        "tbTransparent=%d styleMask=0x%lx content=%s contentFrame=(%.0f,%.0f,%.0f,%.0f) "
        "host=%s hostBounds=(%.0f,%.0f,%.0f,%.0f) | view=%p viewSuperview=%s viewWindow=%p "
        "viewInThisWin=%d viewFrame=(%.0f,%.0f,%.0f,%.0f) viewIsContentView=%d "
        "hosted=%s hostedFrame=(%.0f,%.0f,%.0f,%.0f) material=%ld blending=%ld state=%ld "
        "alphaValue=%.2f\n",
        tag,
        (void*)w,
        enabledArg ? 1 : 0,
        alphaArg,
        insertedNow ? 1 : 0,
        [w isOpaque] ? 1 : 0,
        [bgDesc UTF8String],
        [[w backgroundColor] isEqual:[NSColor clearColor]] ? 1 : 0,
        [w titlebarAppearsTransparent] ? 1 : 0,
        (unsigned long)[w styleMask],
        fxCls(content),
        (double)NSMinX([content frame]),
        (double)NSMinY([content frame]),
        (double)NSWidth([content frame]),
        (double)NSHeight([content frame]),
        fxCls(host),
        (double)NSMinX(hostBounds),
        (double)NSMinY(hostBounds),
        (double)NSWidth(hostBounds),
        (double)NSHeight(hostBounds),
        (void*)view,
        fxCls([view superview]),
        (void*)[view window],
        ([view window] == w) ? 1 : 0,
        (double)NSMinX(viewFrame),
        (double)NSMinY(viewFrame),
        (double)NSWidth(viewFrame),
        (double)NSHeight(viewFrame),
        ([w contentView] == view) ? 1 : 0,
        fxCls(hosted),
        (double)NSMinX(hostedFrame),
        (double)NSMinY(hostedFrame),
        (double)NSWidth(hostedFrame),
        (double)NSHeight(hostedFrame),
        (long)[view material],
        (long)[view blendingMode],
        (long)[view state],
        (double)[view alphaValue]);
    std::fflush(stderr);
}

// 窗口视图结构装配（Fix 1）：AppKit 唯一能把材质垫在 GL 视图「之下」的落点。
// 原设计（插入 contentView.superview = NSThemeFrame，再 belowSubview:contentView）
// 在真机抛异常 —— NSThemeFrame 是 AppKit 私有 frame view，只实现 addSubview:
// （追加 = 置顶），插入家族（insertSubview:belowSubview:/atIndex:/aboveSubview:）
// 一律 unrecognized selector → applyWindowEffect 整个 @try 落到 @catch → 返回
// false → frosted 恒降级 Transparent，材质视图从未挂上（真机缺陷根因）。
// 替代结构（AppKit 标准做法，Apple 的 vibrancy 指南即此）：材质视图成为窗口
// contentView（占内容矩形），原 contentView（GLFW 的 GL 视图）降为其子视图——
// 层级上材质在 GL 视图之下，GL 侧带 alpha 的像素透出材质。GL 视图 frame 保持
// 等于内容矩形（GLFW 的 _glfwGetWindowSizeCocoa/_glfwGetFramebufferSizeCocoa
// 都读 [window->ns.view frame]，故窗口/帧缓冲尺寸不变、UI 布局零位移）。
// 还原（None 档/换窗）走 detachEffectView，逐字段回滚到 GLFW 创建期状态。
void detachEffectView(NSWindow* nsWindow, NSVisualEffectView* view) {
    if (nsWindow == nil || view == nil) {
        return;
    }
    NSView* hosted = hostedContentView(view);
    if ([nsWindow contentView] != view) {
        // 未被本窗持有（材质视图在别处或未挂）：常规摘除
        [view removeFromSuperview];
        return;
    }
    // 本窗持有：把 GL 视图复位为 contentView（材质视图随之脱离窗口层级）
    if (hosted != nil) {
        [hosted removeFromSuperview];
        [nsWindow setContentView:hosted];
        objc_setAssociatedObject(view, kHostedContentViewKey, nil,
                                 OBJC_ASSOCIATION_ASSIGN);
    } else {
        [view removeFromSuperview];
    }
}

bool attachEffectView(NSWindow* nsWindow, bool enabled, float alpha) {
    if (nsWindow == nil) {
        return false;
    }
    if (!enabled) {
        // 「关掉 backdrop」本身是受支持操作（已挂则还原窗口视图结构，未挂则无操作；
        // 未挂的窗口不创建材质视图，也不触碰别人的窗口）
        NSVisualEffectView* existing = effectViewFor(nsWindow, false);
        if (fxDebug()) {
            fxDump("attach:off:before", nsWindow, existing, enabled, alpha, false);
        }
        detachEffectView(nsWindow, existing);
        fxDump("attach:off:after", nsWindow, existing, enabled, alpha, false);
        return true;
    }
    NSVisualEffectView* view = effectViewFor(nsWindow, true);
    NSView* content = [nsWindow contentView];
    if (view == nil || content == nil) {
        // 无内容视图/视图创建失败（异常窗口形态）：挂不上 → false → 走既有降级链
        return false;
    }
    if (content == view) {
        // 已挂（幂等重复 apply）：只更新透明度
        [view setAlphaValue:alpha];
        fxDump("attach:on:update", nsWindow, view, enabled, alpha, false);
        return true;
    }
    [view setAlphaValue:alpha];
    [nsWindow setContentView:view];
    [content setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [content setFrame:[view bounds]];
    [view addSubview:content];
    objc_setAssociatedObject(view, kHostedContentViewKey, content,
                             OBJC_ASSOCIATION_ASSIGN);
    fxDump("attach:on", nsWindow, view, enabled, alpha, true);
    return true;
}

} // namespace

// 不透明窗口外观（头文件声明；判据 = clearColor alpha，见头文件注释）。
// 只处理标题窗：无边框 sprite 窗（桌宠）靠逐像素 alpha 透桌面，置不透明会
// 破坏其透明（桌宠黑底/灰底同类回归）。
bool applyWindowOpaqueAppearance(void* nativeHandle, bool opaque) {
    if (nativeHandle == nullptr) {
        return false;
    }
    NSWindow* nsWindow = (__bridge NSWindow*)nativeHandle;
    if (nsWindow == nil || ([nsWindow styleMask] & NSWindowStyleMaskTitled) == 0) {
        return false;
    }
    @autoreleasepool {
        if (opaque) {
            [nsWindow setOpaque:YES];
            [nsWindow setBackgroundColor:[NSColor windowBackgroundColor]];
            [nsWindow setHasShadow:YES];
        } else {
            [nsWindow setOpaque:NO];
            [nsWindow setBackgroundColor:[NSColor clearColor]];
            [nsWindow setHasShadow:NO];
        }
    }
    fxDump("opaque", nsWindow, effectViewFor(nsWindow, false), opaque, 0.0, false);
    return true;
}

bool applyWindowEffect(void* nativeHandle, WindowEffect effect) {
    if (fxDebug()) {
        std::fprintf(stderr, "[fxdiag] applyWindowEffect enter handle=%p effect=%d\n",
                     nativeHandle, static_cast<int>(effect));
        std::fflush(stderr);
    }
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
                // 实色/半透档共用 None：窗口不透明性与标题栏背景由
                // applyWindowOpaqueAppearance（调用方按 clearColor alpha 判）
                // 单独管，这里只负责摘除材质
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
        } @catch (NSException* exception) {
            // Tahoe 风险门：AppKit 异常（材质不受支持等）→ false → 既有降级链
            if (fxDebug()) {
                std::fprintf(stderr, "[fxdiag] applyWindowEffect EXCEPTION name=%s reason=%s\n",
                             [[exception name] UTF8String],
                             [[exception reason] UTF8String]);
                std::fflush(stderr);
            }
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