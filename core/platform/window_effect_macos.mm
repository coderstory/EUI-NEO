// core/platform/window_effect_macos.mm —— macOS 窗口磨砂实现
//（2026-09-28 macos-three-features 设计 §5.2；磨砂全链见
// 2026-09-22-eui-window-frost-design.md）
//
// 视图结构（2026-09-29 定版，**几何零位移优先**）：
//   material 档：NSWindow.contentView = 材质视图，GL 视图（GLFWContentView）
//                作为其子视图，frame 恒等于客户端矩形
//   None/实色档：GL 视图仍是 contentView（GLFW 创建期结构，一字不动）
// 这样 GLFW 的窗口/帧缓冲尺寸（都读 [window->ns.view frame]）与 compose 侧
// logical 尺寸在任何档位下都不变；材质视图占的正是内容矩形，不会侵入标题栏。
//
// 标题栏（非客户区）**不铺自定义条底**，但要做两件事让 AppKit 把标题栏画出来
//（真机取证定案，两条都要，详证见 docs/平台能力.md「macOS 标题栏底机制」）：
//  1. 窗口背景从 `[NSColor clearColor]` 换成「全透明但非 clear」的具体色（alpha=0）：
//     AppKit 对背景为 clearColor 的非不透明窗**不画标题栏底**，整条标题栏（含交通灯
//     那行）透出桌面/下层窗口，黑壁纸下与「全透明」无异 —— 用户原始投诉即此。
//     alpha=0 不引入任何着色，实测内容区像素与不设时逐项相同。
//  2. `applyTitleBarAppearance` 落到窗口 `NSAppearance`（浅 Aqua / 深 DarkAqua）：
//     标题栏材质默认只跟随**系统外观**，应用浅色 + 系统深色时标题栏发暗，同样像
//     「全透明」；设窗口外观后即跟随宿主主题（Windows 侧同语义 = USE_IMMERSIVE_DARK_MODE）。
// 曾尝试 `NSWindowStyleMaskFullSizeContentView` + contentView 容器 + 主题色条底让材质
// 贯通标题栏 —— 真机实测 **FSCV 会把 GL surface 整体下移一个标题栏高（32pt）**：内容
// 位移、底部被裁、标题栏与内容间留全透明空带（亮段 114/219pt → 143/244pt；空带
// 66..95pt）。二分实测容器重挂载本身不偏移 → 偏移由 FSCV 引入且无法从本侧可靠纠正，
// 故**不启用 FSCV**（几何零位移优先）。材质贯通标题栏属未解需求，候选替代见上述文档。
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
const void* kHostedContentViewKey = &kHostedContentViewKey;   // GLFWContentView

// —— 诊断（EUI_FX_DEBUG=1 时输出 stderr）——
// macOS 窗口效果没有可断言的客观量可看（材质是否真挂上、几何是否位移），且
// screencapture 曾长期被 TCC 拦——诊断行即为验收手段：[fxdiag] 每行含不透明度/
// 背景/标题栏透明位/styleMask/窗口 frame/客户端矩形/内容视图与 GL 视图 frame/
// 材质参数，可逐条判定「哪一层生效、几何有没有动」。
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
    NSVisualEffectView* material = objc_getAssociatedObject(w, kMaterialViewKey);
    NSView* content = [w contentView];
    NSView* hosted = content != nil
        ? objc_getAssociatedObject(content, kHostedContentViewKey)
        : nil;
    char winFrame[64];
    char contentRect[64];
    char layoutRect[64];
    char contentFrame[64];
    char hostedFrame[64];
    char materialFrame[64];
    char bgDesc[96];
    fxRectTo(winFrame, sizeof(winFrame), [w frame]);
    fxRectTo(contentRect, sizeof(contentRect), [w contentRectForFrameRect:[w frame]]);
    fxRectTo(layoutRect, sizeof(layoutRect), [w contentLayoutRect]);
    fxRectTo(contentFrame, sizeof(contentFrame), content != nil ? [content frame] : NSZeroRect);
    fxRectTo(hostedFrame, sizeof(hostedFrame), hosted != nil ? [hosted frame] : NSZeroRect);
    fxRectTo(materialFrame, sizeof(materialFrame),
             material != nil ? [material frame] : NSZeroRect);
    fxColorDescTo(bgDesc, sizeof(bgDesc), [w backgroundColor]);
    std::fprintf(
        stderr,
        "[fxdiag] %s win=%p isOpaque=%d bg=%s bgIsClear=%d tbTransparent=%d styleMask=0x%lx "
        "appearance=%s frame=%s contentRect=%s layoutRect=%s | contentView=%s frame=%s "
        "hosted=%s frame=%s | material=%p alpha=%.2f onWindow=%d sv=%s frame=%s materialValue=%ld "
        "blending=%ld state=%ld\n",
        tag,
        (void*)w,
        [w isOpaque] ? 1 : 0,
        bgDesc,
        [[w backgroundColor] isEqual:[NSColor clearColor]] ? 1 : 0,
        [w titlebarAppearsTransparent] ? 1 : 0,
        (unsigned long)[w styleMask],
        [[[w effectiveAppearance] name] UTF8String],
        winFrame,
        contentRect,
        layoutRect,
        fxCls(content),
        contentFrame,
        fxCls(hosted),
        hostedFrame,
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

// 材质挂载/摘除。挂载结构：材质视图成为 contentView（占内容矩形，不侵入标题栏），
// 原 contentView（GL 视图）降为其子视图、frame 保持客户端矩形 —— GLFW 的
// _glfwGetWindowSizeCocoa/_glfwGetFramebufferSizeCocoa 都读 [window->ns.view frame]，
// 故窗口/帧缓冲/compose 逻辑尺寸零位移。摘除时逐字段回滚（GL 视图复位为 contentView）。
bool attachMaterialView(NSWindow* nsWindow, bool enabled, float alpha) {
    if (nsWindow == nil) {
        return false;
    }
    NSVisualEffectView* material = materialViewFor(nsWindow, enabled);
    if (!enabled) {
        // 「关掉 backdrop」本身是受支持操作（已挂则还原窗口视图结构，未挂则无操作）
        if (material != nil && [nsWindow contentView] == material) {
            NSView* hosted = objc_getAssociatedObject(material, kHostedContentViewKey);
            if (hosted != nil) {
                [hosted removeFromSuperview];
                [nsWindow setContentView:hosted];
                objc_setAssociatedObject(material, kHostedContentViewKey, nil,
                                         OBJC_ASSOCIATION_ASSIGN);
            } else {
                [material removeFromSuperview];
            }
        } else if (material != nil) {
            [material removeFromSuperview];
        }
        fxDump("material:off", nsWindow);
        return true;
    }
    NSView* content = [nsWindow contentView];
    if (material == nil || content == nil) {
        return false;   // 异常窗口形态 → false → 走既有降级链
    }
    if (content == material) {
        // 已挂（幂等重复 apply）：只更新透明度
        [material setAlphaValue:alpha];
        fxDump("material:on:update", nsWindow);
        return true;
    }
    const NSRect clientRect = [content frame];
    [material setAlphaValue:alpha];
    [nsWindow setContentView:material];       // 材质视图占内容矩形（含标题栏？不含）
    [content setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [content setFrame:NSMakeRect(0.0, 0.0, NSWidth(clientRect), NSHeight(clientRect))];
    [material addSubview:content];
    objc_setAssociatedObject(material, kHostedContentViewKey, content,
                             OBJC_ASSOCIATION_ASSIGN);
    fxDump("material:on", nsWindow);
    return true;
}

} // namespace

bool applyWindowEffect(void* nativeHandle, WindowEffect effect) {
    // 标题栏底（真机取证 2026-09-29）：窗口带透明帧缓冲 hint 时 GLFW 把背景设成
    // `[NSColor clearColor]`，而 AppKit **对背景为 clearColor 的非不透明窗不画标题栏
    // 底** → 整条标题栏（含交通灯那行）透出下层窗口/桌面；在黑壁纸下与「全透明」
    // 完全无法区分（用户原始投诉）。把背景换成「全透明但非 clearColor」的具体颜色
    // （alpha=0，不引入任何着色）即可让 AppKit 恢复画标题栏底，且实测对内容区
    // **零影响**（内容区像素与不设时逐项相同：242,242,238 / 250,249,245），窗口结构、
    // GL 视图 frame、窗口/帧缓冲尺寸一概不动（几何零风险）。
    // 判据证据（同一构建，磨砂档）：设前标题栏行 = 下层窗口颜色（浅色窗 250,249,245 /
    // 深色窗 59,58,56 随背景变），设后 = (255,255,255) 恒定（浅色外观）；用半透明
    // 红@0.5 做对照时内容区被染成 (242,217,210) → 故必须 alpha=0。
    // 只对标题窗生效：无边框 sprite 窗（桌宠）保持 GLFW 的 clearColor，避免影响其
    // 逐像素透明。
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
    // 标题栏底（真机取证 2026-09-29）：窗口带透明帧缓冲 hint 时 GLFW 把窗口背景设成
    // `[NSColor clearColor]`，而 AppKit **对背景为 clearColor 的非不透明窗不画标题栏
    // 底** → 整条标题栏（含交通灯那行）透出下层窗口/桌面；在黑壁纸（用户环境）下与
    // 「全透明」完全无法区分（用户原始投诉）。把背景换成「全透明但非 clearColor」的
    // 具体颜色（alpha=0，不引入任何着色）即可让 AppKit 恢复画标题栏底，标题栏深浅随
    // applyTitleBarAppearance 设的 NSAppearance 走（从而跟随 DevDesk 主题）。
    // 真机对照（同一构建、磨砂档）：设前标题栏行 = 下层窗口的颜色（浅色窗
    // 250,249,245 / 深色窗 59,58,56 随背景变，即无底）；设后 = (255,255,255) 恒定；
    // 半透明红@0.5 的对照组把内容区染成 (242,217,210) → 必须 alpha=0 才不影响内容区
    //（alpha=0 组内容区像素与不设时逐项相同：242,242,238 / 250,249,245）。
    // 窗口结构、GL 视图 frame、窗口/帧缓冲尺寸一概不动（几何零风险）；只对标题窗生效，
    // 无边框 sprite 窗（桌宠）保持 GLFW 的 clearColor，避免影响其逐像素透明。
    if (([nsWindow styleMask] & NSWindowStyleMaskTitled) != 0) {
        [nsWindow setBackgroundColor:[NSColor colorWithSRGBRed:0.0
                                                         green:0.0
                                                          blue:0.0
                                                         alpha:0.0]];
    }
    @autoreleasepool {
        @try {
            switch (effect) {
            case WindowEffect::None:
                // 实色/半透档共用 None（backdrop 关掉，透明/实色由 clearColor alpha 定）
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

bool applyTitleBarAppearance(void* nativeHandle,
                             const TitleBarAppearance& appearance) {
    if (nativeHandle == nullptr) {
        return false;
    }
    NSWindow* nsWindow = (__bridge NSWindow*)nativeHandle;
    if (nsWindow == nil) {
        return false;
    }
    @autoreleasepool {
        // 与 Windows 侧 applyTitleBarAppearance 语义对齐：Windows 用 appearance.dark
        // → DWMWA_USE_IMMERSIVE_DARK_MODE（标题栏深浅跟**应用主题**），macOS 的等价物
        // 是给窗口设 NSAppearance —— AppKit 自画的标题栏材质随之取浅/深两套
        //（Aqua / DarkAqua）。customColor 在 macOS 无对应 API（标题栏底色由材质决定），
        // 与 Windows 侧「customColor 是可选增强、失败不影响 dark 主开关」一致：忽略，
        // 且不因此返回 false。
        //
        // 口径变更（2026-09-29，设计 §9「不做 titlebarAppearsTransparent 联动」解禁）：
        // 当时「不做」的前提是「本批不做窗口效果，标题栏无所谓」；现在用户明确要求
        // 窗口效果档位下标题栏**可见且跟随 DevDesk 主题**。真机取证证明 AppKit 标题栏
        // 材质默认只跟随**系统外观**：应用浅色 + 系统深色时标题栏发暗，在纯黑壁纸下与
        // 「全透明」难以区分（用户原始投诉「titlebar 还是全透的」即此，实测对照：
        // 深色主题应用在浅色系统下拿到的是浅色标题栏 (250,249,245)，反之同理）。
        // 本实现不碰窗口结构（不动 contentView / GL 视图 / FullSizeContentView），
        // 几何零风险；这也是它优于「FSCV + 主题色条底」方案（实测会把 GL surface
        // 下移 32pt）的原因。
        NSAppearanceName name = appearance.dark ? NSAppearanceNameDarkAqua
                                               : NSAppearanceNameAqua;
        [nsWindow setAppearance:[NSAppearance appearanceNamed:name]];
    }
    if (fxDebug()) {
        std::fprintf(stderr, "[fxdiag] titlebar appearance dark=%d -> %s\n",
                     appearance.dark ? 1 : 0,
                     appearance.dark ? "DarkAqua" : "Aqua");
        std::fflush(stderr);
    }
    return true;
}

} // namespace core::platform
