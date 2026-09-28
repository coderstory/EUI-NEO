// core/platform/window_effect_macos.mm —— macOS 窗口磨砂 / 标题栏实现
//（2026-09-28 macos-three-features 设计 §5.2；磨砂全链见
// 2026-09-22-eui-window-frost-design.md）
//
// 视图结构（2026-09-28 真机缺陷修复后定版）：
//   NSWindow
//   └── contentView = 容器（NSView；标题窗开 FullSizeContentView 后覆盖整窗）
//       ├── 材质视图（NSVisualEffectView，material 档；frame = 容器 bounds）
//       ├── GLFWContentView（GL 视图；frame = 客户端矩形，任何档位切换都不变）
//       └── 标题栏条底（NSView，frame = 整窗 − 客户端矩形；None 档铺主题色）
// 三层职责：材质 = backdrop（磨砂）；GL 视图 = 应用内容（透明度由 clearColor alpha
// 定）；条底 = 标题栏条底色（AppKit 只给不透明窗画标题栏背景，非不透明窗必须
// 自己铺，且底色跟随宿主主题 —— 见 applyWindowChromeAppearance）。
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

// —— 材质 alpha 取值（唯一改动点，便于实机再微调）——
// ⚠️ 与宿主 clearColor alpha 是**两层叠加**，调观感时只能动一层：
//   窗口最终不透明度 ≈ clearColorAlpha + (1 − clearColorAlpha) × materialAlphaValue
// 宿主（DevDesk settings_config.cpp windowEffectAlpha）：off 1.0 / 半透 0.85 /
// 磨砂 0.55（磨砂档材质 alpha = kMaterialAlpha = 1.0，透明度只由 clearColor 定）。
// 半透档：DevDesk 映射 WindowEffect::None（显式关 backdrop）→ 材质视图整条摘除
// → kTranslucentMaterialAlpha 在 DevDesk 的半透档**不参与合成**，该档观感 100%
// 由 clearColor alpha 决定（要更不透明只能改 DevDesk 侧那个数）。此值仅在宿主
// 显式请求 WindowEffect::Transparent 档时生效。
constexpr float kTranslucentMaterialAlpha = 0.4f;
constexpr float kMaterialAlpha = 1.0f;

// 关联对象键：状态按窗口各存一份（进程级单例会让多窗互抢同一视图——桌宠子窗
// apply(None) 曾把主窗已挂的材质视图摘走，真机取证见提交说明）
const void* kMaterialViewKey = &kMaterialViewKey;             // NSVisualEffectView
const void* kContainerKey = &kContainerKey;                   // NSView（contentView）
const void* kHostedContentViewKey = &kHostedContentViewKey;   // GLFWContentView
const void* kStripViewKey = &kStripViewKey;                   // NSView（标题栏条底）
const void* kChromeColorKey = &kChromeColorKey;               // NSColor（主题底色）

// —— 诊断（EUI_FX_DEBUG=1 时输出 stderr；非 Apple 平台本文件不参与编译）——
// macOS 窗口效果没有可断言的客观量可看（材质是否真挂上、窗口是否不透明、标题栏
// 条是否铺了底、GL 视图几何是否位移），且 screencapture 常被 TCC 拦——诊断行即
// 为验收手段：[fxdiag] 每行含不透明度/背景/标题栏透明位/styleMask/窗口 frame/
// 客户端矩形/层容器与条底 frame/材质参数，可逐条判定「哪一层生效」。
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

void fxRectTo(char* out, size_t size, NSRect rect) {
    std::snprintf(out,
                  size,
                  "(%.0f,%.0f,%.0f,%.0f)",
                  (double)NSMinX(rect),
                  (double)NSMinY(rect),
                  (double)NSWidth(rect),
                  (double)NSHeight(rect));
}

void fxColorDescTo(char* out, size_t size, NSColor* color) {
    if (color == nil) {
        std::snprintf(out, size, "(nil)");
        return;
    }
    NSColor* srgb = [color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    if (srgb == nil) {
        std::snprintf(out, size, "%s", [[color description] UTF8String]);
        return;
    }
    std::snprintf(out,
                  size,
                  "srgb(%.3f,%.3f,%.3f,%.3f)",
                  (double)[srgb redComponent],
                  (double)[srgb greenComponent],
                  (double)[srgb blueComponent],
                  (double)[srgb alphaComponent]);
}

void fxDump(const char* tag, NSWindow* w) {
    if (!fxDebug()) {
        return;
    }
    NSView* container = objc_getAssociatedObject(w, kContainerKey);
    NSVisualEffectView* material = objc_getAssociatedObject(w, kMaterialViewKey);
    NSView* strip = objc_getAssociatedObject(w, kStripViewKey);
    NSView* hosted = container != nil
        ? objc_getAssociatedObject(container, kHostedContentViewKey)
        : nil;
    char winFrame[64];
    char contentRect[64];
    char layoutRect[64];
    char contentViewFrame[64];
    char hostedFrame[64];
    char stripFrame[64];
    char materialFrame[64];
    char bgDesc[96];
    char stripDesc[96];
    fxRectTo(winFrame, sizeof(winFrame), [w frame]);
    fxRectTo(contentRect, sizeof(contentRect), [w contentRectForFrameRect:[w frame]]);
    fxRectTo(layoutRect, sizeof(layoutRect), [w contentLayoutRect]);
    fxRectTo(contentViewFrame, sizeof(contentViewFrame), [[w contentView] frame]);
    fxRectTo(hostedFrame, sizeof(hostedFrame), hosted != nil ? [hosted frame] : NSZeroRect);
    fxRectTo(stripFrame, sizeof(stripFrame), strip != nil ? [strip frame] : NSZeroRect);
    fxRectTo(materialFrame, sizeof(materialFrame),
             material != nil ? [material frame] : NSZeroRect);
    fxColorDescTo(bgDesc, sizeof(bgDesc), [w backgroundColor]);
    fxColorDescTo(stripDesc, sizeof(stripDesc),
                  (strip != nil && [[strip layer] backgroundColor] != nullptr)
                      ? [NSColor colorWithCGColor:[[strip layer] backgroundColor]]
                      : nil);
    std::fprintf(
        stderr,
        "[fxdiag] %s win=%p isOpaque=%d bg=%s bgIsClear=%d tbTransparent=%d styleMask=0x%lx "
        "frame=%s contentRect=%s layoutRect=%s | contentView=%s frame=%s container=%s "
        "hosted=%s frame=%s strip=%s frame=%s stripColor=%s | material=%p alpha=%.2f "
        "onWindow=%d sv=%s frame=%s materialValue=%ld blending=%ld state=%ld\n",
        tag,
        (void*)w,
        [w isOpaque] ? 1 : 0,
        bgDesc,
        [[w backgroundColor] isEqual:[NSColor clearColor]] ? 1 : 0,
        [w titlebarAppearsTransparent] ? 1 : 0,
        (unsigned long)[w styleMask],
        winFrame,
        contentRect,
        layoutRect,
        fxCls([w contentView]),
        contentViewFrame,
        fxCls(container),
        fxCls(hosted),
        hostedFrame,
        fxCls(strip),
        stripFrame,
        stripDesc,
        (void*)material,
        material != nil ? (double)[material alphaValue] : 0.0,
        (material != nil && [material window] == w) ? 1 : 0,
        fxCls(material != nil ? [material superview] : nil),
        materialFrame,
        material != nil ? (long)[material material] : 0,
        material != nil ? (long)[material blendingMode] : 0,
        material != nil ? (long)[material state] : 0);
    std::fflush(stderr);
}

// —— 材质视图：按窗口各一份（关联对象挂 NSWindow，随窗口释放）——
NSVisualEffectView* materialViewFor(NSWindow* nsWindow, bool create) {
    if (nsWindow == nil) {
        return nil;
    }
    NSVisualEffectView* view = objc_getAssociatedObject(nsWindow, kMaterialViewKey);
    if (view == nil && create) {
        view = [[NSVisualEffectView alloc] initWithFrame:NSZeroRect];
        // classic 材质组合（设计 §5.2；⚠️ 材质观感待 Mac 实机定，spec §13-2）：
        // HUDWindow 深色高对比 / UnderWindowBackground 通用背景二选一（可换）
        [view setMaterial:NSVisualEffectMaterialHUDWindow];
        [view setBlendingMode:NSVisualEffectBlendingModeWithinWindow];
        [view setState:NSVisualEffectStateActive];
        [view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
        objc_setAssociatedObject(nsWindow, kMaterialViewKey, view,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    return view;
}

// —— 容器：把窗口 contentView 换成我们的容器（幂等）——
// 标题窗额外开 FullSizeContentView + titlebarAppearsTransparent：容器随之覆盖
// 整窗，标题栏条区域才归我们画（AppKit 只给不透明窗画标题栏背景，窗口带透明
// 帧缓冲 hint 时恒为非不透明 → 不自己铺底则整条标题栏含交通灯透出桌面）。
//
// 几何不变量（glfw_app_layout 零位移的依据）：GL 视图 frame 任何时候都等于
// 「客户端矩形」= 切换前的 contentView frame。GLFW 的
// _glfwGetWindowSizeCocoa / _glfwGetFramebufferSizeCocoa 都读
// [window->ns.view frame]，故窗口尺寸 / 帧缓冲尺寸 / compose 侧 logical 尺寸
// 与容器化之前逐项一致；标题栏条只是容器内一条新增视图，不占内容区。
// 缩放时靠 autoresizing 维持：GL 视图高度伸缩而上下边距固定（上边距 = 条高），
// 条底高度与顶边距固定、底边距伸缩。
NSView* ensureContainer(NSWindow* nsWindow) {
    if (nsWindow == nil) {
        return nil;
    }
    NSView* container = objc_getAssociatedObject(nsWindow, kContainerKey);
    NSView* content = [nsWindow contentView];
    if (container != nil && content == container) {
        return container;   // 已就位
    }
    if (container != nil) {
        // contentView 被外部换掉（异常路径）：弃旧容器重建，避免把窗口留在半装状态
        objc_setAssociatedObject(nsWindow, kContainerKey, nil, OBJC_ASSOCIATION_ASSIGN);
        container = nil;
    }
    if (content == nil) {
        return nil;
    }
    const NSRect clientRect = [content frame];   // 客户端矩形（= GL 视图应有 frame）
    const NSRect frameRect = [nsWindow frame];
    const bool titled = ([nsWindow styleMask] & NSWindowStyleMaskTitled) != 0;
    const CGFloat stripHeight =
        titled ? MAX(0.0, NSHeight(frameRect) - NSHeight(clientRect)) : 0.0;

    container = [[NSView alloc] initWithFrame:clientRect];
    [container setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    objc_setAssociatedObject(nsWindow, kContainerKey, container,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    if (titled) {
        [nsWindow setStyleMask:([nsWindow styleMask] | NSWindowStyleMaskFullSizeContentView)];
        [nsWindow setTitlebarAppearsTransparent:YES];
        // ⚠️ FullSizeContentView 会改变 AppKit 的 frame/contentRect 语义：开启瞬间
        // contentRectForFrameRect: 退化为恒等，AppKit 会保持「原内容矩形」而把窗口
        // frame 缩掉标题栏高度（真机实测 833→801，客户区顶边下移 32pt → 应用顶部
        // 被标题栏盖住）。这里把 frame 原样恢复：客户区绝对位置/尺寸与开启前逐项
        // 相同，多出的 32pt 正好是标题栏条（容器内 y=clientH 起的那条）。
        [nsWindow setFrame:frameRect display:YES];
    }
    // 容器成为 contentView（AppKit 会按整窗尺寸重排它）
    [nsWindow setContentView:container];
    // GL 视图搬进容器（frame 保持客户端矩形；GLFW 的窗口/帧缓冲尺寸都读
    // [window->ns.view frame] → 窗口、帧缓冲、compose 逻辑尺寸零位移）
    [content removeFromSuperview];
    [content setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [container addSubview:content];
    objc_setAssociatedObject(container, kHostedContentViewKey, content,
                             OBJC_ASSOCIATION_ASSIGN);
    [content setFrame:clientRect];

    NSView* strip = nil;
    if (stripHeight > 0.0) {
        strip = [[NSView alloc] initWithFrame:NSMakeRect(0.0,
                                                         NSHeight(clientRect),
                                                         NSWidth([container bounds]),
                                                         stripHeight)];
        // 宽度伸缩 + 底边距伸缩 → 条高与顶边距恒定（窗口缩放时条底贴顶、等厚）
        [strip setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
        [strip setWantsLayer:YES];
        [container addSubview:strip];
        objc_setAssociatedObject(nsWindow, kStripViewKey, strip,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    return container;
}

// —— 标题栏条底颜色 ——
// 有材质（material 档）时材质本身覆盖整窗（含标题栏条）→ 条底透明；无材质
//（None 档：关 / 半透）时条底铺宿主主题色，alpha 取 clearColor alpha —— 半透档
// 条与内容区同透明度「贯通」，关档为实色条。
void updateStripColor(NSWindow* nsWindow) {
    NSView* strip = objc_getAssociatedObject(nsWindow, kStripViewKey);
    if (strip == nil) {
        return;
    }
    NSVisualEffectView* material = objc_getAssociatedObject(nsWindow, kMaterialViewKey);
    const bool materialVisible = material != nil && [material superview] != nil;
    NSColor* color = materialVisible
        ? [NSColor clearColor]
        : objc_getAssociatedObject(nsWindow, kChromeColorKey);
    if (color == nil) {
        color = [NSColor clearColor];
    }
    [strip setWantsLayer:YES];
    [[strip layer] setBackgroundColor:[color CGColor]];
}

// 材质挂载/摘除（enabled=false 时只摘材质，容器与标题栏条底保留 —— None 档的
// 标题栏照样要有底色）。返回是否成功。
bool attachMaterialView(NSWindow* nsWindow, bool enabled, float alpha) {
    if (nsWindow == nil) {
        return false;
    }
    if (!enabled) {
        NSVisualEffectView* material = objc_getAssociatedObject(nsWindow, kMaterialViewKey);
        if (material != nil) {
            [material removeFromSuperview];
        }
        updateStripColor(nsWindow);
        fxDump("material:off", nsWindow);
        return true;
    }
    NSView* container = ensureContainer(nsWindow);
    if (container == nil) {
        // 无 contentView（异常窗口形态）：挂不上 → false → 走既有降级链
        return false;
    }
    NSVisualEffectView* material = materialViewFor(nsWindow, true);
    if (material == nil) {
        return false;
    }
    [material setAlphaValue:alpha];
    [material setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    if ([material superview] != container) {
        [material removeFromSuperview];
        // 垫底：必须位于 GL 视图之下（GL 侧带 alpha 的像素才能透出材质）
        [container addSubview:material positioned:NSWindowBelow relativeTo:nil];
    }
    [material setFrame:NSMakeRect(0.0, 0.0,
                                  NSWidth([container bounds]),
                                  NSHeight([container bounds]))];
    updateStripColor(nsWindow);
    fxDump("material:on", nsWindow);
    return true;
}

} // namespace

// 窗口 chrome 外观（头文件声明）：不透明性/阴影 + 标题栏条底主题色。
bool applyWindowChromeAppearance(void* nativeHandle, float r, float g, float b, float a) {
    if (nativeHandle == nullptr) {
        return false;
    }
    NSWindow* nsWindow = (__bridge NSWindow*)nativeHandle;
    if (nsWindow == nil) {
        return false;
    }
    // 无标题栏窗（桌宠 sprite 窗）：没有标题栏条，且必须保持非不透明
    //（逐像素 alpha 透桌面；置不透明会出黑底/灰底回归）
    if (([nsWindow styleMask] & NSWindowStyleMaskTitled) == 0) {
        return false;
    }
    @autoreleasepool {
        NSColor* color = [NSColor colorWithSRGBRed:r green:g blue:b alpha:a];
        objc_setAssociatedObject(nsWindow, kChromeColorKey, color,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        // ⚠️ 不动 isOpaque / backgroundColor：GLFW 在 macOS 上把
        // GLFW_TRANSPARENT_FRAMEBUFFER 定义为 !([nsWindow isOpaque])（cocoa_window.m
        // _glfwFramebufferTransparentCocoa），而 GL 后端的 straight/premultiply blit
        // 自检把该属性**缓存在首个渲染帧**（opengl_backend.cpp outputUsesTransparentFramebuffer）
        // —— 这里若按档位改 isOpaque（曾如此：实色档 setOpaque:YES 想换回原生标题栏），
        // 会把「透明帧缓冲可用」这一会话级真值带偏，切到半透/磨砂后 blit 路径仍是
        // 不透明路径（alpha 合成错）。标题栏底色改由容器内条底承担后，窗口无需在
        // 不透明性上做文章：全档保持非不透明，透明帧缓冲属性恒为真。
        // 阴影例外：GLFW 因透明 hint 关掉了阴影（cocoa_window.m 创建期
        // setHasShadow:NO），实色档（alpha >= 1）恢复成原生窗观感；阴影不参与
        // 上述属性判定。
        [nsWindow setHasShadow:(a >= 1.0f) ? YES : NO];
        if (ensureContainer(nsWindow) == nil) {
            return false;
        }
        updateStripColor(nsWindow);
        fxDump("chrome", nsWindow);
        return true;
    }
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
                // 实色/半透档共用 None（backdrop 关掉，透明/实色由 clearColor 定）：
                // 只摘材质，窗口不透明性与标题栏条底由 applyWindowChromeAppearance 管
                return attachMaterialView(nsWindow, false, kMaterialAlpha);
            case WindowEffect::Transparent:
                // 半透档材质视图整体半透明（kTranslucentMaterialAlpha；DevDesk 当前
                // 不请求本档，见文件头 alpha 注释）
                return attachMaterialView(nsWindow, true, kTranslucentMaterialAlpha);
            case WindowEffect::Acrylic:
            case WindowEffect::Mica:
            case WindowEffect::MicaAlt:
                // Frosted 语义 = Acrylic 档（DevDesk settings_config 映射）；
                // Mica 族同挂同一材质视图（观感差异并入实机复核项）
                return attachMaterialView(nsWindow, true, kMaterialAlpha);
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
    // → 恒 false（静默不刷日志，与既有 stub 语义一致）。
    // 2026-09-28 口径变更：§9 那条「不做标题栏联动」的前提是当时不做窗口效果
    //（标题栏底色无所谓）；现在窗口效果三档要标题栏可见且底色跟随宿主主题，
    // 该联动改由 applyWindowChromeAppearance（macOS 专用，带 clearColor 主题色）
    // 承担——本函数仍保持恒 false，避免两条路径同时写标题栏。
    return false;
}

} // namespace core::platform
