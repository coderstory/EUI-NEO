// core/platform/window_effect_macos.mm —— macOS 窗口磨砂实现
//（2026-09-28 macos-three-features 设计 §5.2；磨砂全链见
// 2026-09-22-eui-window-frost-design.md）
//
// 视图结构（2026-09-29 二版，**材质贯通标题栏 + 几何零位移**）：
//   material 档：① 内容区 = 一版结构（contentView 换成材质视图，GL 视图降为其子
//                  视图、frame 恒等于客户端矩形，一字不动）
//                ② 标题栏 = 额外一条同材质的 NSVisualEffectView，作为内容区材质的
//                  子视图顶出内容矩形那一条 —— 让标题栏与内容区走同一套合成
//   None/实色档：两处材质视图都摘除；GL 视图始终是 contentView（GLFW 创建期结构）
// 这样 GLFW 的窗口/帧缓冲尺寸（都读 [window->ns.view frame]）与 compose 侧
// logical 尺寸在任何档位下都不变；标题栏条只占非客户区那一条，不碰 GL 几何。
//（为什么需要标题栏条、挂哪儿、两次失败的结构尝试：见 attachMaterialView 注释。）
//
// 标题栏（非客户区）**不由本层自绘**，靠三处窗口属性让 AppKit 把标题栏画对
//（真机取证定案，详证见 docs/平台能力.md「macOS 标题栏底机制」）：
//  1. 窗口背景不能是 `[NSColor clearColor]`：AppKit 对背景为 clearColor 的非不透明窗
//     **不画标题栏底**，整条标题栏（含交通灯那行）透出桌面/下层窗口，黑壁纸下与
//     「全透明」无异 —— 用户原始投诉即此。两种取值：宿主给了自定义底色（customColor）
//     就用它（含 alpha，标题栏与内容区同色同透明度，DevDesk 需求「和 content 融为一体」）；
//     没给就退回「全透明但非 clear」的具体色（alpha=0，不引入任何着色）。
//  2. 给了自定义底色时另把 `titlebarAppearsTransparent` 设为 YES：AppKit 不画自画
//     标题栏材质 → 标题栏区域直接透出上面的窗口背景色（= 内容区底色）；未给自定义
//     色时该位为 NO，标题栏由 AppKit 材质画出。二版起标题栏底下还垫着本层的
//     NSVisualEffectView（material 档），故标题栏观感 = 材质叠窗口背景色，与内容区同源。
//  3. `applyTitleBarAppearance` 落到窗口 `NSAppearance`（浅 Aqua / 深 DarkAqua）：
//     标题栏材质/交通灯默认只跟随**系统外观**，应用浅色 + 系统深色时标题栏发暗，
//     同样像「全透明」；设窗口外观后即跟随宿主主题（Windows 侧同语义 =
//     USE_IMMERSIVE_DARK_MODE）。
// 曾尝试 `NSWindowStyleMaskFullSizeContentView` + contentView 容器 + 主题色条底让材质
// 贯通标题栏 —— 真机实测 **FSCV 会把 GL surface 整体下移一个标题栏高（32pt）**：内容
// 位移、底部被裁、标题栏与内容间留全透明空带（亮段 114/219pt → 143/244pt；空带
// 66..95pt）。二分实测容器重挂载本身不偏移 → 偏移由 FSCV 引入且无法从本侧可靠纠正，
// 故**不启用 FSCV**（几何零位移优先）。二版改用「材质视图当 NSThemeFrame 底层兄弟」
// 达成同一目的，不碰 FSCV、不碰 GL 视图（见 attachMaterialView 注释）。
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

// 标题栏条材质视图必须**事件透明**：它铺在红绿灯/标题底下，一旦参与命中判定就会
// 吃掉标题栏拖拽、双击缩放与按钮点击（命中判定落到它下面的 NSThemeFrame / 按钮视图
// 才是 AppKit 原生行为）。视图本身只负责画，不需要接收任何事件。
// ⚠ 只给标题栏条用：内容区材质是 contentView（GL 视图的父视图），覆写它的 hitTest
// 会让整棵内容区子树收不到鼠标事件（真机实测侧边栏点击全失效）。
@interface EUIPassthroughMaterialView : NSVisualEffectView
@end

@implementation EUIPassthroughMaterialView
- (NSView*)hitTest:(NSPoint)point {
    (void)point;
    return nil;
}
@end

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
constexpr float kMaterialAlpha = 0.95f;

// 关联对象键：状态按窗口各存一份（进程级单例会让多窗互抢同一视图——桌宠子窗
// apply(None) 曾把主窗已挂的材质视图摘走，真机取证见提交说明）
const void* kMaterialViewKey = &kMaterialViewKey;             // 内容区材质 NSVisualEffectView
const void* kHostedContentViewKey = &kHostedContentViewKey;   // GLFWContentView
const void* kTitleBarMaterialKey = &kTitleBarMaterialKey;     // 标题栏条材质视图
const void* kTitleBarColorKey = &kTitleBarColorKey;           // NSNumber(BOOL) 已设自定义底色

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
    NSVisualEffectView* tbMaterial = objc_getAssociatedObject(w, kTitleBarMaterialKey);
    NSView* content = [w contentView];
    NSView* glSuper = content != nil ? [content superview] : nil;
    char winFrame[64];
    char contentRect[64];
    char layoutRect[64];
    char contentFrame[64];
    char glSuperFrame[64];
    char materialFrame[64];
    char tbMaterialFrame[64];
    char bgDesc[96];
    fxRectTo(winFrame, sizeof(winFrame), [w frame]);
    fxRectTo(contentRect, sizeof(contentRect), [w contentRectForFrameRect:[w frame]]);
    fxRectTo(layoutRect, sizeof(layoutRect), [w contentLayoutRect]);
    fxRectTo(contentFrame, sizeof(contentFrame), content != nil ? [content frame] : NSZeroRect);
    fxRectTo(glSuperFrame, sizeof(glSuperFrame), glSuper != nil ? [glSuper frame] : NSZeroRect);
    fxRectTo(materialFrame, sizeof(materialFrame),
             material != nil ? [material frame] : NSZeroRect);
    fxRectTo(tbMaterialFrame, sizeof(tbMaterialFrame),
             tbMaterial != nil ? [tbMaterial frame] : NSZeroRect);
    fxColorDescTo(bgDesc, sizeof(bgDesc), [w backgroundColor]);
    std::fprintf(
        stderr,
        "[fxdiag] %s win=%p isOpaque=%d bg=%s bgIsClear=%d tbTransparent=%d styleMask=0x%lx "
        "appearance=%s frame=%s contentRect=%s layoutRect=%s | contentView=%s frame=%s "
        "glSuper=%s frame=%s | material=%p alpha=%.2f onWindow=%d sv=%s frame=%s materialValue=%ld "
        "blending=%ld state=%ld | tbMaterial=%p alpha=%.2f sv=%s frame=%s\n",
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
        fxCls(glSuper),
        glSuperFrame,
        (void*)material,
        material != nil ? (double)[material alphaValue] : 0.0,
        (material != nil && [material window] == w) ? 1 : 0,
        fxCls(material != nil ? [material superview] : nil),
        materialFrame,
        material != nil ? (long)[material material] : 0,
        material != nil ? (long)[material blendingMode] : 0,
        material != nil ? (long)[material state] : 0,
        (void*)tbMaterial,
        tbMaterial != nil ? (double)[tbMaterial alphaValue] : 0.0,
        fxCls(tbMaterial != nil ? [tbMaterial superview] : nil),
        tbMaterialFrame);
    std::fflush(stderr);
}

// —— 标题栏底（真机取证 2026-09-29）——
// 窗口带透明帧缓冲 hint 时 GLFW 把窗口背景设成 `[NSColor clearColor]`，而 AppKit
// **对背景为 clearColor 的非不透明窗不画标题栏底** → 整条标题栏（含交通灯那行）
// 透出下层窗口/桌面；在黑壁纸下与「全透明」无法区分（用户原始投诉）。
// 两套取值，二者互斥，由 applyTitleBarAppearance 决定：
//   a) 宿主给了自定义底色（customColor）→ 窗口背景 = 该色（含 alpha），标题栏
//      `titlebarAppearsTransparent=YES` → 标题栏区域直接透出窗口背景色，与内容区
//      同色同透明度（DevDesk 需求：三档窗口效果下标题栏都要和 content 融为一体）。
//   b) 宿主没给 → 全透明但非 clearColor 的具体色（alpha=0，不引入任何着色）+ 标题栏
//      恢复 AppKit 自画材质（深浅随窗口 NSAppearance = 宿主主题深浅）。alpha=0 组
//      内容区像素与不设时逐项相同（242,242,238 / 250,249,245）。
// 判据证据（同一构建，磨砂档）：不设时标题栏行 = 下层窗口颜色（浅色窗 250,249,245 /
// 深色窗 59,58,56 随背景变），设 alpha=0 后 = (255,255,255) 恒定（浅色外观）；用
// 半透明红@0.5 做对照时内容区被染成 (242,217,210) → 故兜底必须 alpha=0。
void applyFallbackTitleBarBackground(NSWindow* nsWindow) {
    [nsWindow setBackgroundColor:[NSColor colorWithSRGBRed:0.0
                                                     green:0.0
                                                      blue:0.0
                                                     alpha:0.0]];
}

// 是否已由 applyTitleBarAppearance 挂了宿主自定义标题栏底色（按窗口存，理由同上方
// 材质视图的关联对象）。设了就不许 applyWindowEffect 的 alpha=0 兜底覆盖——那会把
// 底色抹掉退回 AppKit 自画材质（= 本批要修的「标题栏恒纯白、不跟主题」）。
// 两条路径在启动期与运行时都会走（glfw_app_main：先 applyTitleBarAppearanceToWindow
// 再 applyWindowEffectToWindow，标题栏外观变更也早于窗口效果变更块）。
bool hasCustomTitleBarColor(NSWindow* nsWindow) {
    return objc_getAssociatedObject(nsWindow, kTitleBarColorKey) != nil;
}

// 材质视图：按窗口各一份（关联对象挂 NSWindow，随窗口释放）——
NSVisualEffectView* materialViewFor(NSWindow* nsWindow, bool create) {
    if (nsWindow == nil) {
        return nil;
    }
    NSVisualEffectView* view = objc_getAssociatedObject(nsWindow, kMaterialViewKey);
    if (view == nil && create) {
        // ⚠ 内容区材质**必须用普通 NSVisualEffectView**（默认 hitTest）：它同时是
        // contentView（GL 视图的父视图），一旦 hitTest 返回 nil，整棵内容区子树
        // （含 GL 视图）就不再接收鼠标事件——真机实测：侧边栏点击全失效。
        // 事件透明只对「标题栏条」那一份有意义（见 EUIPassthroughMaterialView）。
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

// 标题栏条材质视图（2026-09-29 二版新增）：与内容区材质**同材质/同混合/同 state**，
// 单独一份（frame 只有标题栏那一条），按窗口各存一份（关联对象）。
NSVisualEffectView* titleBarMaterialViewFor(NSWindow* nsWindow, bool create) {
    if (nsWindow == nil) {
        return nil;
    }
    NSVisualEffectView* view = objc_getAssociatedObject(nsWindow, kTitleBarMaterialKey);
    if (view == nil && create) {
        view = [[EUIPassthroughMaterialView alloc] initWithFrame:NSZeroRect];
        [view setMaterial:NSVisualEffectMaterialHUDWindow];
        [view setBlendingMode:NSVisualEffectBlendingModeWithinWindow];
        [view setState:NSVisualEffectStateActive];
        // 贴在标题栏那一条：宽度跟随窗口、下边距弹性 → 窗口变高时留在顶部
        [view setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
        objc_setAssociatedObject(nsWindow, kTitleBarMaterialKey, view,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    return view;
}

// 材质挂载/摘除。
//
// 内容区（一版结构保持不动）：材质视图成为 contentView，原 contentView（GL 视图）
// 降为其子视图、frame 保持客户端矩形 —— GLFW 的 _glfwGetWindowSizeCocoa /
// _glfwGetFramebufferSizeCocoa 都读 [window->ns.view frame]，故窗口/帧缓冲/compose
// 逻辑尺寸零位移。摘除时逐字段回滚（GL 视图复位为 contentView）。
//
// 标题栏条（二版新增，**为什么必须加**）：标题栏（非客户区）在二版之前
// **没有任何材质**，它的观感 = 窗口背景色一层（theme×alpha）叠在壁纸上；而内容区
// = clearColor 纱 + 材质 + 窗口背景色三层。alpha 一压低（磨砂档 0.30）两者立刻
// 劈叉：黑壁纸实测标题栏 (75,75,74) vs 内容区 (180,181,179)，差 105/255 —— 观感上
// 标题栏像另一块面板（用户原始投诉）。补上这一条材质后，标题栏与内容区走同一套
// 合成（材质 + 窗口背景色），差值只剩 clearColor 纱那一层（α×|主题色 − 材质输出|），
// 可用两处 alpha 分别标定压到 ≤ 20/255（DevDesk 侧实测 8.7）。
//
// 挂哪儿（2026-09-29 真机取证，两次失败尝试记录在案，勿重走）：
//   ① 不用 FSCV：实测把 GL surface 整体下移 32pt（撤回，见文件头）。
//   ② 不用「把材质视图挂到 NSThemeFrame 上（铺满整窗或只盖标题栏）」：真机实测
//      **AppKit 把窗口标题画到最左缘、与红绿灯重叠**——全窗 / 条状两种 frame ×
//      relativeTo:nil / relativeTo:contentView 两种插入点 × 含/不含 hitTest 覆写，
//      四组合全复现 → 该路线整体弃用。
//   最终采用：内容区那份一字不动（contentView 结构，几何零位移已验收），标题栏条
//   作为**内容区材质的子视图**挂在内容矩形之上（frame.y = 内容高，顶出去那一条，
//   由窗口自身的绘制边界裁剪）→ 不往 NSThemeFrame 里加任何视图，标题布局正常。
//
// 事件：标题栏条铺在红绿灯/标题底下，必须事件透明（EUIPassthroughMaterialView
// 的 hitTest 恒 nil），否则标题栏拖拽会被它吃掉；它 z 序在 GL 视图之上、但在
// titlebar 容器之下 → 按钮与标题仍压在它上面。
bool attachMaterialView(NSWindow* nsWindow, bool enabled, float alpha) {
    if (nsWindow == nil) {
        return false;
    }
    NSVisualEffectView* material = materialViewFor(nsWindow, enabled);
    if (material == nil) {
        if (!enabled) {
            fxDump("material:off", nsWindow);
            return true;   // 未挂且本次要关：无操作（关 backdrop 是受支持操作）
        }
        return false;      // 分配失败 → false → 走既有降级链
    }
    NSVisualEffectView* strip = titleBarMaterialViewFor(nsWindow, enabled);
    if (!enabled) {
        // 摘除两处：标题栏条 + 内容区材质（后者要把 GL 视图复位为 contentView）
        if (strip != nil && [strip superview] != nil) {
            [strip removeFromSuperview];
        }
        if ([nsWindow contentView] == material) {
            NSView* hosted = objc_getAssociatedObject(material, kHostedContentViewKey);
            if (hosted != nil) {
                [hosted removeFromSuperview];
                [nsWindow setContentView:hosted];
                objc_setAssociatedObject(material, kHostedContentViewKey, nil,
                                         OBJC_ASSOCIATION_ASSIGN);
            } else {
                [material removeFromSuperview];
            }
        } else {
            [material removeFromSuperview];
        }
        fxDump("material:off", nsWindow);
        return true;
    }
    NSView* content = [nsWindow contentView];
    if (content == nil || strip == nil) {
        return false;   // 异常窗口形态 → false → 走既有降级链
    }
    if (content == material) {
        // 已挂（幂等重复 apply）：只更新两处透明度
        [material setAlphaValue:alpha];
        [strip setAlphaValue:alpha];
        fxDump("material:on:update", nsWindow);
        return true;
    }
    // —— 内容区：一版结构 ——
    const NSRect clientRect = [content frame];
    [material setAlphaValue:alpha];
    [nsWindow setContentView:material];       // 材质视图占内容矩形，不侵入标题栏
    [content setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [content setFrame:NSMakeRect(0.0, 0.0, NSWidth(clientRect), NSHeight(clientRect))];
    [material addSubview:content];
    objc_setAssociatedObject(material, kHostedContentViewKey, content,
                             OBJC_ASSOCIATION_ASSIGN);
    // —— 标题栏条：材质视图挂在内容区材质之上（子视图），frame 顶出内容矩形 ——
    // ⚠ 不挂 NSThemeFrame：真机实测**任何**额外挂在 NSThemeFrame 上的子视图都会让
    // AppKit 把窗口标题画到最左缘、与红绿灯重叠（全窗矩形 / 只盖标题栏 两种 frame、
    // relativeTo:nil / relativeTo:contentView 两种插入点、含/不含 hitTest 覆写，
    // 四种组合全复现）→ 该路线弃用。改为挂在内容区材质下：不动 titlebar 视图层级，
    // 标题布局恢复正常（真机复验）；材质铺到 y = 内容高..内容高+标题栏高 那一条，
    // 由窗口自身的绘制边界裁剪。
    const CGFloat contentHeight = NSHeight(clientRect);
    const NSRect windowFrame = [nsWindow frame];
    const NSRect contentRect = [nsWindow contentRectForFrameRect:windowFrame];
    const CGFloat barHeight = NSHeight(windowFrame) - NSHeight(contentRect);
    if (barHeight > 0.0) {
        [strip setAlphaValue:alpha];
        [strip setFrame:NSMakeRect(0.0, contentHeight, NSWidth(windowFrame), barHeight)];
        [strip setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
        [material addSubview:strip];
    }
    fxDump("material:on", nsWindow);
    return true;
}

} // namespace

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
    // 标题栏底兜底（见 applyFallbackTitleBarBackground 上方注释）：宿主未给自定义
    // 底色时才铺「全透明非 clear」色；已给则由 applyTitleBarAppearance 全权决定窗口
    // 背景（本函数不得覆盖，否则标题栏退回 AppKit 自画材质）。只对标题窗生效：
    // 无边框 sprite 窗（桌宠）保持 GLFW 的 clearColor，避免影响其逐像素透明。
    // 窗口结构、GL 视图 frame、窗口/帧缓冲尺寸一概不动（几何零风险）。
    if (([nsWindow styleMask] & NSWindowStyleMaskTitled) != 0 &&
        !hasCustomTitleBarColor(nsWindow)) {
        applyFallbackTitleBarBackground(nsWindow);
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
        // 是给窗口设 NSAppearance —— 标题栏的深浅/交通灯随宿主主题取浅/深两套
        //（Aqua / DarkAqua）。customColor 两平台都属**可选增强**：Windows 走
        // DWMWA_CAPTION_COLOR、macOS 走「窗口背景色 + titlebarAppearsTransparent」
        //（见下），失败/未给时不影响 dark 主开关，也不因此返回 false。
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

        // 标题栏底色（2026-09-29 真机取证，DevDesk 需求「标题栏与 content 融为一体」）：
        // customColor 在 macOS **有**对应物（不再是「无对应 API、忽略」）——窗口背景色
        // 就是标题栏区域在 `titlebarAppearsTransparent=YES` 下透出的那一层：
        //   setBackgroundColor(宿主主题底色, alpha) + setTitlebarAppearsTransparent(YES)
        // → 标题栏 = 宿主主题底色，三档窗口效果都取到该档的 alpha
        //（实色 1.0 / 半透 0.90 / 磨砂 0.55，由宿主把 clearColor 的 alpha 传下来）。
        // 真机实测残差（实色档标题栏与内容区逐项相同，另两档见 docs/平台能力.md
        //「已知限制」）：内容区的实际不透明度 = 1−(1−clearAlpha)(1−colorAlpha) ≥ 标题栏
        //（半透档 0.99 vs 0.90），磨砂档内容区的材质本身不透明（1.00）而标题栏取 0.55。
        // 只碰窗口属性：不动 contentView / GL 视图 / FullSizeContentView → 几何零风险
        //（对照：FSCV 方案实测把 GL surface 下移 32pt，已撤回）。
        // 宿主没给自定义色时**必须**回到 alpha=0 兜底 + 关掉 titlebarAppearsTransparent，
        // 否则「标题栏整条透出桌面」的老缺陷会被这段代码重新引入。
        // 只对标题窗生效：无边框 sprite 窗（桌宠）的逐像素透明不能被窗口背景污染。
        if (([nsWindow styleMask] & NSWindowStyleMaskTitled) != 0) {
            if (appearance.customColor) {
                // COLORREF(0x00BBGGRR) → sRGB 分量（低字节是 R，别写反）；alpha 来自宿主
                //（窗口效果档）。真机取证：写反会把 #FAF9F5 渲染成 (246,249,250)，肉眼
                // 极难分辨、只有取像素均值才看得出。
                const CGFloat red = (appearance.colorAbgr & 0xFFu) / 255.0;
                const CGFloat green = ((appearance.colorAbgr >> 8) & 0xFFu) / 255.0;
                const CGFloat blue = ((appearance.colorAbgr >> 16) & 0xFFu) / 255.0;
                const CGFloat alpha = appearance.colorAlpha < 0.0f
                    ? 0.0
                    : (appearance.colorAlpha > 1.0f ? 1.0 : (CGFloat)appearance.colorAlpha);
                [nsWindow setBackgroundColor:[NSColor colorWithSRGBRed:red
                                                                 green:green
                                                                  blue:blue
                                                                 alpha:alpha]];
                // 关掉 AppKit 自画的标题栏材质 → 标题栏区域直接透出上面的窗口背景色
                [nsWindow setTitlebarAppearsTransparent:YES];
                objc_setAssociatedObject(nsWindow, kTitleBarColorKey, @YES,
                                         OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            } else {
                [nsWindow setTitlebarAppearsTransparent:NO];
                applyFallbackTitleBarBackground(nsWindow);
                objc_setAssociatedObject(nsWindow, kTitleBarColorKey, nil,
                                         OBJC_ASSOCIATION_ASSIGN);
            }
        }
    }
    if (fxDebug()) {
        std::fprintf(stderr, "[fxdiag] titlebar appearance dark=%d custom=%d abgr=0x%06x alpha=%.2f -> %s\n",
                     appearance.dark ? 1 : 0,
                     appearance.customColor ? 1 : 0,
                     (unsigned)(appearance.colorAbgr & 0xFFFFFFu),
                     (double)appearance.colorAlpha,
                     appearance.dark ? "DarkAqua" : "Aqua");
        std::fflush(stderr);
    }
    return true;
}

} // namespace core::platform
