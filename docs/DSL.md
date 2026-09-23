# DSL 设计与当前实现

当前推荐写法是声明式 DSL：应用只描述页面结构、样式、交互回调和目标状态，内部 Runtime 负责布局、状态缓存、事件、动画、脏区渲染和后端 primitive 同步。

## 核心元素

```cpp
enum class ElementKind {
    Row,
    Column,
    Stack,
    Flow,
    Rect,
    Polygon,
    Text,
    Image,
    Svg,
    Shadertoy
};
```

- `Row`：横向布局容器。
- `Column`：纵向布局容器。
- `Stack`：叠放布局容器。
- `Flow`：横向流式布局容器，空间不足时自动换行。
- `Rect`：基础视觉图元，支持颜色、渐变、圆角、边框、阴影、透明度、blur、transform、hover/pressed 状态。
- `Polygon`：多边形图元，支持点集、圆角、颜色、透明度、transform 和 hover/pressed 状态；当前图表 tooltip 指针和 pieChart 扇区都基于它。
- `Text`：文本图元，支持字体、字号、颜色、换行、行高、对齐、透明度和 transform。
- `Image`：图片图元，支持本地图片、网络图片、本地 SVG、Bing daily、cover/contain/stretch。
- `Svg`：内联 SVG 图元，通过 `ui.svg(...).source(svgMarkup)` 声明并复用 Image 的布局、动画和渲染路径。

组件不进入 core 枚举。组件层只是组合 DSL 图元，例如 `components::button(ui, id)` 内部使用 `Stack + Rect + Row + Text`。

## App 入口

DSL app 推荐只实现：

```cpp
#include "eui_neo.h"

namespace app {

const DslAppConfig& dslAppConfig();
void compose(eui::Ui& ui, const eui::Screen& screen);

} // namespace app
```

`include/eui/dsl_app.h` 已封装：

- initialize
- update
- isAnimating
- render
- shutdown
- app::async 后台任务
- 按屏幕刷新率主动节流
- 无动画时等待事件休眠

`DslAppConfig` 推荐使用链式写法，配置项名会直接写在调用里。它也支持为单个 app 覆盖默认字体：

```cpp
static const DslAppConfig config = DslAppConfig{}
    .title("Clock")
    .pageId("clock")
    .clearColor({0.965f, 0.966f, 0.970f, 1.0f})
    .windowSize(1600, 1080)
    .uiScale(1.25f)
    .fps(90.0)
    .textFont("YouSheBiaoTiHei-2.ttf");
```

窗口创建参数也可以直接在 `DslAppConfig` 中设置：

```cpp
static const DslAppConfig config = DslAppConfig{}
    .windowSize(1280, 800)
    .windowPosition(120, 80) // 不调用时默认居中
    .minWindowSize(800, 500)
    .maxWindowSize(1920, 1200)
    .resizable(true)
    .highDpi(true)
    .decorated(true)
    .alwaysOnTop(false)
    .maximized(false)
    .debugTitleInterval(1.0)
    .showDebugStatsInTitle(true)
    .showDebugOverlay(true)
    .onDebugOverlay([](eui::Ui& ui, const eui::Screen& screen) {
        components::layoutDebugOverlay(ui, "debug.bounds", screen.width, screen.height, 8.0f, "debug");
    });
```

`minWindowSize` 和 `maxWindowSize` 中的 `0` 表示对应方向不限制；窗口尺寸约束由 GLFW/SDL2 后端执行。`centerWindow()` 会清除显式位置并恢复居中。`highDpi` 在 SDL2 中控制 `SDL_WINDOW_ALLOW_HIGHDPI`；GLFW 的 DPI 感知由其初始化阶段按平台设置，是进程级行为，不能安全地按单个窗口关闭。标题栏外观与窗口效果（透明半透）属于 `DslAppConfig`（见下）；全屏和 VSync 不属于 `DslAppConfig`，它们会改变平台窗口或渲染后端生命周期，应通过专用平台/渲染配置处理。

Debug 配置只控制诊断输出，不参与业务状态。`showDebugStatsInTitle` 控制窗口标题中的 FPS、CPU/GPU 和渲染统计；`debugTitleInterval` 控制标题统计刷新间隔（秒）。`showDebugOverlay` 与 `onDebugOverlay` 用于注入布局边界、性能标记等调试框，回调在每次页面 compose 后执行；未设置回调时不会绘制任何额外内容。Debug 构建默认开启标题统计和覆盖层开关，Release 构建默认关闭。

`DslAppConfig` 的标题、页面 ID、图标和字体路径、托盘文本与图标路径都由配置对象以 `std::string` 持有。setter 可以安全接收局部或临时 `std::string`；调用返回后不会保留调用方字符串的指针。

`.onShutdown(callback)` 在 UI/渲染线程、主窗口 Runtime 和 GPU 设备销毁前调用，供应用停止自己的
后台生产者并释放所持有的外部 GPU 图像引用。回调也可能在应用初始化失败的清理路径执行，
必须容忍资源尚未创建，不应抛异常。参见 [外部 GPU 图像的生命周期](动态纹理.md)。

托盘后台运行默认关闭。需要托盘的页面可以在 `DslAppConfig` 中显式调用 `.tray(true)`，例如串口工具。启用托盘后，关闭或最小化窗口会隐藏到托盘并释放图形资源；托盘 `Show` 会重新显示窗口，`Exit` 才真正退出。

托盘菜单默认是内置的 `Show`/`Exit` 两项。`.trayMenu(...)` 注册自定义菜单项（`TrayMenuItem{text, callback}`，`text` 为 `"-"` 表示分隔线），默认项 `Show`/`Exit` 保留在末尾；第二参数传 `false` 可移除默认项，此时应用需自行提供退出入口。不调用 `.trayMenu` 或传空列表时行为与旧版完全一致（仅 `Show`/`Exit`）。回调在主线程触发，可在其中更新 UI 状态并调用 `requestUpdate()`：

```cpp
.tray(true)
.trayMenu({
    {"清空日志", [] { state.logs.clear(); requestUpdate(); }},
    {"-", nullptr},
})  // 菜单为：清空日志 | 分隔线 | Show | Exit
```

运行期更新菜单用 `eui::platform::setTrayMenu(items, keepDefault)`（托盘初始化前后均可调用，已初始化时即时生效；Linux SNI 后端在面板下次展开菜单时生效）。

### 标题栏外观（深浅主题联动）

`.darkTitleBar(true)` 让窗口标题栏（非客户区）在启动时进入深色模式，浅色界面配浅色标题栏、深色界面配深色标题栏。仅 Windows（DWMWA_USE_IMMERSIVE_DARK_MODE，Win11 22000+）生效，其他平台或老系统静默忽略。默认 `false`，行为与现状一致。

运行时切换主题时用 `app::setTitleBarAppearance(core::platform::TitleBarAppearance{dark})`：即时生效、无需重启，覆盖主窗口与全部存活子窗口（此后新开的子窗口同样跟随），并优先于 `.darkTitleBar(...)` 的启动快照。`app::currentTitleBarAppearance()` 返回当前生效值。`TitleBarAppearance` 还预留了 `customColor`/`colorAbgr`（Windows 自定义标题栏底色，COLORREF），默认不启用——系统材质贯通标题栏时应让 DWM 自绘。示例见 `examples/window_effect.cpp`。

### 窗口效果（透明半透 + DWM backdrop）

`.windowEffect(core::platform::WindowEffect::Transparent)` 让主窗口与子窗口以透明帧缓冲创建（GLFW `GLFW_TRANSPARENT_FRAMEBUFFER`；SDL2 无逐像素透明 flag、Vulkan 在 Windows 上 compositeAlpha 普遍只报 OPAQUE，两者降级 `None`），此时 `clearColor` 的 alpha < 1 即整体半透、可透出桌面与下层窗口。默认 `None`（实色，行为与历史版本逐像素一致）。

要点：

- 透明 hint 是创建期属性，不能运行时关闭；「半透 ↔ 不透明」的运行时切换通过把 clearColor alpha 拉回 1 实现视觉等价，不重建窗口。需要运行时切到半透/磨砂档的应用，可以常开 hint（启动即 `Transparent`）+ 「关」档 alpha=1 视觉等价。
- 半透像素的合成正确性由 GL 后端自动保证：透明窗口的 render cache blit 走 premultiply（DWM 按 premultiplied alpha 合成窗口表面，straight 拷贝会整体过亮/发灰）；alpha=1 时该变换恒等，不透明窗口路径不变。
- Vulkan 后端（Windows compositeAlpha 普遍 OPAQUE）与 SDL2 后端（无逐像素透明 flag）降级为 `None`（标题栏联动不受影响）。
- 桌面合成被禁用等导致透明帧缓冲不可用时，GLFW 静默降级为不透明窗口（stderr 诊断一行），渲染侧按同一属性自动回退 straight blit。

运行时改底色/不透明度用 `app::setClearColor(color)`：下一帧生效，覆盖主窗口与全部存活子窗口（此后新开的子窗口同样跟随），优先于 `DslAppConfig::clearColor` 与 `DslWindowConfig::clearColor` 的启动快照；`app::currentClearColor()` 返回当前生效值。这也是主题切换底色即时跟随的正规通道（不再需要重启）。

运行时切 DWM backdrop 材质用 `app::setWindowEffect(effect)`（磨砂 Phase C）：`Acrylic`（TRANSIENTWINDOW 实时模糊）/ `Mica`（MAINWINDOW 壁纸采样）/ `MicaAlt`（TABBEDWINDOW）/ `None`·`Transparent`（显式关 backdrop，回到纯 clearColor alpha 半透），下一帧应用到主窗口与全部存活子窗口，优先于 `.windowEffect(...)` 启动快照。仅 Windows 11 22H2（22621）+ 的 GLFW 后端生效；`DWMSBT_AUTO` 不可靠（spike 实测无效），档位一律显式。backdrop 材质画在窗口整个 bounds 后面（含标题栏），窗口内容需带 alpha（透明 hint + clearColor alpha < 1）才能露出；「关」档把 clearColor alpha 拉回 1 即完全遮住材质。`app::activeWindowEffect()` 返回实际生效值（能力降级后的真值，主循环每次应用后回写）：请求 `Acrylic`/`Mica` 而系统不支持时，按透明帧缓冲是否实际开启降级为 `Transparent`/`None`——设置页用它回查降级并提示用户。示例见 `examples/window_effect.cpp`。

### 子窗口配置（无边框 / 置顶 / 穿透 / 任务栏隐藏）

`app::openWindow` 的 `DslWindowConfig` 支持完整窗口形态（桌宠设计 §2.6 G1-G5；此前子窗口必然带标题栏/不置顶/可缩放，且有 160×120 最小尺寸下限）：

```cpp
app::openWindow(app::DslWindowConfig{}
    .title("Pet").pageId("pet")
    .windowSize(128, 128)               // 小窗下限已放开（只防 0/负数）
    .windowPosition(x, y)               // 初始位置（GLFW 左上原点，两平台一致）
    .decorated(false)                   // 无边框
    .alwaysOnTop(true)                  // 置顶（失焦仍保持）
    .resizable(false)
    .focusOnShow(false)                 // 显示时不抢前台焦点（SDL2 后端忽略）
    .clickThrough(true)                 // 创建期整窗鼠标穿透（仅无边框生效）
    .hideFromTaskbar(true)              // 不进任务栏/Alt+Tab（Windows；macOS/Linux 静默降级）
    .ignoreClearColorOverride()         // 不跟随 app::setClearColor 全局广播（自管背景的窗口）
    .clearColor({0, 0, 0, 0})           // 全透底（需 windowEffect 非 None 开透明 hint）
    .onWindowCreated([](core::window::Handle h) { /* 保存句柄 */ }),
    composeFn);
```

配套的运行时窗口操作（`core/window/window_backend.h`，拖拽移动/穿透切换/位置钳制）：

- `core::window::setWindowPos / getWindowPos`——运行时移动/读取窗口位置；
- `core::window::setWindowMousePassthrough(window, bool)`——运行时切换整窗鼠标穿透（挂机模式）；
- `core::window::getPrimaryMonitorWorkArea(x, y, w, h)`——主显示器工作区，供持久化位置钳制回屏幕内。

任务栏隐藏的平台层接口是 `core::platform::applyWindowStyleFlags(handle, WindowStyleFlags{toolWindow, noActivate})`（Windows `WS_EX_TOOLWINDOW`/`WS_EX_NOACTIVATE`；macOS/Linux stub 返回 false）。`.hideFromTaskbar(true)` 内部即调它。

`ignoreClearColorOverride()` 面向自管背景色的覆盖窗口（如桌宠 sprite 窗）：`app::setClearColor` 的全局广播（主题切换/窗口效果切档）不会冲掉它自己的 `clearColor`，默认 false = 跟随广播（既有行为不变）。示例见 `examples/pet_window.cpp`。



不设置 `.textFont(...)` 时使用 `core/render/text.cpp` 里的全局默认文本字体；不设置 `.iconFont(...)` 时使用全局默认图标字体。默认字体优先从可执行文件旁的 `assets/`、工作目录 `assets/`、上级运行目录 `assets/` 查找；找不到内置字体资源时会回退到平台系统字体，避免单 exe 漏带 assets 后普通文本整段不可见。

## 颜色

所有接收 `eui::Color`（即 `core::Color`）的接口均可直接使用 HEX 字符串，原有浮点 RGBA 写法继续可用：

```cpp
ui.rect("panel").color("#1E293B").border(1.0f, "#38BDF880").build();
ui.text("label").color("#FFF").text("Hello").build();
components::button(ui, "save").colors("#2563EB", "#3B82F6", "#1D4ED8").build();
eui::Color accent = "#38BDF8";
eui::Color original{0.2f, 0.4f, 0.6f, 1.0f};
auto rgb = eui::Color::fromHex(0x336699u);
auto rgba = eui::Color::fromHexRgba(0x33669980u);
```

支持 `const char*`、`std::string`、`std::string_view`；解析时复制颜色数值，不保留字符串引用。

| 格式 | 示例 | 说明 |
| --- | --- | --- |
| `#RGB` | `#369` | 等同 `#336699`，不透明 |
| `#RGBA` | `#3698` | 等同 `#33669988` |
| `#RRGGBB` | `#336699` | 不透明 |
| `#RRGGBBAA` | `#33669980` | alpha 在末尾，`80` 表示 128/255 |

HEX 不区分大小写，必须包含 `#`，不接受额外空白。隐式构造和 `Color::fromHex(string)` 遇到非法字符串时返回透明黑；外部配置建议先校验，失败时保留原值：

```cpp
eui::Color color = "#38BDF8";
if (!eui::Color::tryFromHex(userInput, color)) {
    // 提示颜色格式无效，color 保持不变。
}
```

数值 HEX 必须显式使用 `fromHex(0xRRGGBB)` 或 `fromHexRgba(0xRRGGBBAA)`，以避免带前导零的数值产生 alpha 歧义。`Color` 仍由四个 float 组成、支持普通 RGBA 列表初始化；新增构造函数后不再是 aggregate，不支持 C++20 指定成员初始化（如 `{.r = ...}`）。

## 布局 DSL

容器：

```cpp
ui.row("toolbar")
ui.column("content")
ui.stack("root")
```

通用布局属性：

```cpp
.x(value)
.y(value)
.position(x, y)
.width(value)
.height(value)
.size(width, height)
.fill()
.wrapContent()
.margin(value)
.margin(horizontal, vertical)
.margin(left, top, right, bottom)
.gap(value)
.justifyContent(eui::Align::CENTER)
.alignItems(eui::Align::CENTER)
.align(eui::Align::CENTER, eui::Align::CENTER)
.ignoreLayout()
.zIndex(value)
.clip()
```

`.zIndex(...)` 只影响同级元素的绘制顺序和 topmost hit-test，不参与布局计算；值越大越靠上。`.clip()` 会按该元素布局矩形裁剪自己和子树，并且命中测试也不会穿出裁剪区域。

`Row / Column` 的主轴 flex 分配会先用自身 fixed / fill 结果约束 inner size，再把结果作为子项测量约束；fixed、wrapContent 和 fill 子项都可以显式参与 grow / shrink，默认 `flexShrink` 为 0，避免固定尺寸控件被意外压扁。`Stack` 会用自身最终 inner size 重新测量 fill 子项，适合普通叠放背景层；`Stack.wrapContent()` 会把子项正向 x/y 偏移计入包裹尺寸，负向偏移仍视为向外溢出。

`.ignoreLayout()` 用于装饰背景、调试框等不应该占据布局流的覆盖层。它仍会渲染、仍遵守 zIndex / hit-test，但不参与父容器 measure、gap、Row / Column / Flow 排列，也不会撑开 wrapContent 或 scrollView 内容高度。不要用 `z(-1)` 代替 `.ignoreLayout()`；zIndex 只表达绘制层级，不表达布局语义。

## Loader 与实例状态

`loader` 是 DSL 级生命周期 scope，不是可视图元。它用于按 `active` 动态挂载一段 UI 子树，并决定这段子树隐藏时是否释放实例状态。

```cpp
ui.loader("settings.loader")
    .active(showSettings)
    .destroyOnHide()
    .content([&] {
        settingsPage.compose(ui, width, height);
    })
    .build();

ui.loader("editor.loader")
    .active(showEditor)
    .keepAlive()
    .content([&] {
        editorPage.compose(ui, width, height);
    })
    .build();
```

模式：

- `destroyOnHide()`：`active(false)` 时不 compose 内容，并释放该 loader scope 下通过 `ui.state<T>(id)` 创建的实例状态。
- `keepAlive()`：`active(false)` 时不 compose 内容，但保留该 loader scope 下的实例状态，下一次 active 后恢复。

Loader 内部会给内容 id 增加 scope 前缀，避免不同 loader 中的同名元素和实例状态相互串扰。组件作者可以用 `ui.state<T>(id)` 存放纯交互状态；状态 key 会自动包含当前 `pageId` 和 loader scope。

```cpp
struct CounterState {
    int value = 0;
};

ui.loader("counter.loader")
    .active(showCounter)
    .keepAlive()
    .content([&] {
        CounterState& state = ui.state<CounterState>("counter");
        ui.text("value")
            .text(std::to_string(state.value))
            .build();
    })
    .build();
```

组件内部的纯交互状态应统一走 `ui.state<T>(id)`，不要再使用组件级静态 `id -> state` 表。当前已接入该模型的状态包括 `input`、`mouseArea`、`carousel`、`datePicker`、`timePicker`、`colorPicker`、`pieChart`、`workshop::heartSwitch` 和 `workshop::neumorphicButton`。

## 通用交互 DSL

`Row / Column / Stack / Flow / Rect / Polygon / Text / Image / Svg` 都支持通用交互方法：

```cpp
.interactive(true)
.disabled(false)
.cursor(eui::CursorShape::Hand)
.hitTestMode(eui::dsl::HitTestMode::Transformed)
.transformedHitTest()
.onClick(callback)
.onPress(callback)
.onRelease(callback)
.onMove(callback)
.onContextMenu(callback)
.onHover(callback)
.focusable()
.onFocusChanged(callback)
.onKeyEvent(callback)
.onTextInput(callback)
.onScroll(callback)
.onDrag(callback)
```

`.onClick(...)` 会自动开启 interactive，并把 cursor 设置为手型。Runtime 会做 topmost hit-test、按下捕获、点击判定和回调派发。命中测试按实际绘制顺序从最上层往下找，同一位置只会选中第一个命中的 interactive 元素；`zIndex` 越高越靠上，同级同 z 时后声明的元素更靠上。默认命中使用布局矩形；需要旋转、缩放或 2.5D 投影后的视觉区域参与命中时，用 `.hitTestMode(eui::dsl::HitTestMode::Transformed)` 或 `.transformedHitTest()`。`HitTestMode::Layout` 是默认布局命中，`Transformed` 会按当前投影矩阵反算命中，`None` 表示该元素自身不参与命中。

focus 命中也遵守同样的 topmost 顺序。鼠标按下时，如果最上层命中的是 focusable 元素，就聚焦它；如果最上层命中的是非 focusable 的 interactive 元素，例如 sidebar scrim、modal panel 背景或透明 hit rect，Runtime 会停止向下查找并清空焦点，避免点击穿透到底层输入框。

底层 DSL 的 `onPress/onRelease/onMove/onDrag/onScroll` 回调直接使用 Runtime 原始事件。交互默认只接受左键；`.acceptedButtons(eui::PointerButton::Middle | eui::PointerButton::Right)` 可显式启用中键/右键，`DragEvent::button` 标识拥有捕获的按钮。`onMove` 只在元素是当前 topmost hover 目标且指针移动或刚进入 hover 时派发。纯视觉、高频 pointer-follow transform 应优先用 Runtime binding，例如 `.runtimePointerTransformFrom(...)` / `.runtimePointerTiltFrom(...)`，避免每次 mouse move 触发组件状态写入和 compose。普通按钮或菜单项等简单点击直接绑定可视图元；页面或组件需要 tap、拖拽阈值、滚轮步进、局部坐标、进入/离开 hover 时，再使用组件层的 `components::mouseArea(ui, id)`。

`onKeyEvent(const eui::KeyEvent&)` 与 `onTextInput(const eui::TextInputEvent&)` 相互独立。前者接收逻辑键、`Press/Repeat/Release`、scan code 和原始修饰键并返回是否消费；后者只处理文字、粘贴和 IME。焦点元素未消费的按键会进入 `DslAppConfig::onKeyEvent` 或子窗口的 `DslWindowConfig::onKeyEvent`。

示例：

```cpp
ui.text("github.link")
    .size(260.0f, 34.0f)
    .text("GitHub")
    .color(accent)
    .onClick([] {
        eui::platform::openUrl("https://github.com/sudoevolve/EUI-NEO");
    })
    .build();
```

## Rect DSL

```cpp
ui.rect("card")
    .size(360.0f, 260.0f)
    .color({0.10f, 0.12f, 0.16f, 1.0f})
    .radius(18.0f)
    .border(1.0f, {0.23f, 0.29f, 0.38f, 1.0f})
    .shadow(26.0f, 0.0f, 8.0f, {0.0f, 0.0f, 0.0f, 0.26f})
    .insetShadow(12.0f, 4.0f, 4.0f, {0.0f, 0.0f, 0.0f, 0.22f})
    .transition(0.2f, eui::Ease::OutCubic)
    .build();
```

Rect 支持：

```cpp
.color(...)
.gradient(...)
.radius(...)
.border(...)
.shadow(...)
.insetShadow(...)
.blur(...)
.opacity(...)
.translate(...)
.translate3d(...)
.translateX(...)
.translateY(...)
.translateZ(...)
.scale(...)
.rotate(...)
.rotateX(...)
.rotateY(...)
.rotateZ(...)
.rotate(...)
.perspective(...)
.transformOrigin(...)
.states(normal, hover, pressed)
```

`.states(normal, hover, pressed)` 是 Rect 专用的 hover / pressed 视觉状态，会开启交互并由 Runtime 维护 blend。

`.shadow(...)` 绘制圆角外阴影，会扩大元素的 visual dirty rect。`.insetShadow(...)` 绘制裁剪在圆角内部的内阴影，不扩大 visual rect，适合拟态按压、内凹面板、输入框内侧暗边等效果。两者都是单阴影字段；需要多重阴影时，叠多个透明 `rect` 图层，每层使用稳定 id。

## Text DSL

```cpp
ui.text("title")
    .size(420.0f, 48.0f)
    .text("EUI Gallery")
    .fontFamily("YouSheBiaoTiHei")
    .fontSize(38.0f)
    .lineHeight(44.0f)
    .color({0.94f, 0.97f, 1.0f, 1.0f})
    .horizontalAlign(eui::HorizontalAlign::Center)
    .verticalAlign(eui::VerticalAlign::Center)
    .build();
```

Text 支持：

```cpp
.text(...)
.icon(codepoint)
.fontFamily(...)
.fontSize(...)
.fontWeight(...)
.color(...)
.opacity(...)
.translate(...)
.translate3d(...)
.translateX(...)
.translateY(...)
.translateZ(...)
.scale(...)
.rotate(...)
.rotateX(...)
.rotateY(...)
.rotateZ(...)
.rotate(...)
.perspective(...)
.transformOrigin(...)
.maxWidth(...)
.wrap(...)
.horizontalAlign(...)
.verticalAlign(...)
.lineHeight(...)
```

`.icon(...)` 会自动使用图标字体；图标字体默认来自 `core/render/text.cpp`，也可以通过配置里的 `.iconFont(...)` 按 app 覆盖。找不到内置图标字体资源时会尝试平台 symbol/icon 字体兜底；但 FontAwesome 图标使用 FontAwesome 自己的 codepoint，系统字体不一定有兼容 glyph，发布包仍建议携带默认 `assets/` 或显式配置 `.iconFont(...)`。

`.fontWeight(int)` 取值范围 100–900，会按权重选择平台 UI 字体的对应字重文件：macOS 上的 SFNS-Bold / SFNS-Light、Windows 上的 segoeuib / segoeuil、Linux 上的 NotoSans-Bold / NotoSans-Light。系统在重量文件缺失时会优雅降级到 Regular / 默认字体；可以用 `core::TextPrimitive::resolveSystemUiFontPathForWeight(N)` 在运行时查询实际选中的字体文件路径。默认字重 400 不变，向后兼容现有 UI 代码。

`.fontSize(float)` 自 v0.7.0 起遵循 CSS `font-size` 语义：数值就是字体的 em 尺寸（1em = 字体的 units_per_EM），与字体自身 hhea ascender/descender 度量无关。v0.7.0 之前所有 scalable 字体栅格化时会按 upem/(ascender−descender) 缩放，导致 hhea 度量较大的字体（Windows Segoe UI 约 0.75 倍、微软雅黑约 0.76 倍）实际渲染比标称字号小约 25%；修复后 15px 就是 15px em。emoji / 彩色 bitmap 字体的 strike 匹配逻辑不受影响。**迁移提示**：从 v0.6.x 升级的应用中，此前依赖这些字体"整体偏小"渲染效果的布局（固定行高、卡片高度、像素级对齐）在升级后文字会放大约 25%–35%，需要复核相关数值；如有全局字号缩放等临时止血配置，应调回 100% 后重新验收。可运行 `examples/font_em_size_demo`（`font_em_size_demo` app）对比同一 fontSize 在不同系统字体下的视觉尺寸一致性。

底层文本使用 FreeType 渲染 glyph，并通过字体栈 fallback 选择覆盖字符的字体。`fontFamily("monospace")` 会选择跨平台等宽字体，`fontFamily("Emoji")` 会选择平台 emoji 字体；如果指定字体或内置 assets 字体加载失败，文本栈会继续尝试默认 UI 字体和系统字体兜底。需要精确光标位置或命中测试时，使用 `core::TextPrimitive::measureTextMetrics(...)` 获取 caret stops；返回的 `byteIndices` 是 UTF-8 byte offset，`caretX` 是对应的逻辑 x，和实际渲染使用同一套 fallback、emoji 缩放和 glyph advance。

`.run(int byteStart, int byteEnd, const Color&)` / `.runs(std::vector<core::TextRun>)` / `.clearRuns()` 是 v0.8 新增的行内多色文本入口：一条 `ui.text` 元素内按 **UTF-8 字节偏移区间**（`[byteStart, byteEnd)`，全文坐标系）着色，未命中 run 的部分用 `.color()` 的元素级颜色。典型用法是语法高亮器把一行的 token 直接映射成 runs：

```cpp
ui.text("md.editor.line.42")
    .text(lineText)
    .color(tokens.text)                    // 未命中 run 的默认色
    .runs(highlighter.runsForLine(42))     // vector<TextRun>
    .build();
```

规则与约束：`.run()` 可多次调用、顺序即优先级（重叠区间**后写覆盖前写**）；乱序 / 重叠 / 越界 / 劈开多字节 codepoint 的输入都允许——框架在渲染前用 `core::normalizeTextRuns(text, runs)` 归一化（clamp 到 UTF-8 边界、去重叠、合并相邻同色、丢弃空 run），需要干净 runs 时也可自行调用。runs 只是颜色：不改布局、测量与 caret 映射（`measureTextMetrics` 与光标定位零影响），空 runs 时顶点输出与旧单色路径逐字节一致。使用 `dirtyKey` 的调用方必须把 runs 变化编进 key，否则增量更新不会触发重着色；`components::input` 的行级文本（`.lineStyles()` 回调）已自动把 runs 指纹编进行级 dirtyKey，调用方无需处理。可运行 `examples/styled_text_demo`（`styled_text_demo` app）查看一行多色 / CJK 多色 / 行级高亮模拟 / input 编辑器高亮的效果。

Text 的 transform 作用在生成后的 glyph 顶点上，适合做滚轮、轻量缩放和旋转动效；默认命中测试仍按未 transform 的布局 frame 计算，需要跟随视觉变换时开启 `.transformedHitTest()`。

`ui.text(id)` 是标准文本入口。

## Image DSL

```cpp
ui.image("cover")
    .size(320.0f, 180.0f)
    .source("assets/icon.png")
    .cover()
    .radius(16.0f)
    .build();

ui.image("bing")
    .size(420.0f, 220.0f)
    .bingDaily(0, "zh-CN")
    .cover()
    .build();

ui.svg("heart.icon")
    .size(48.0f, 48.0f)
    .source(R"svg(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
  <path fill="#ffffff" d="M12 21s-8-4.7-8-11a5 5 0 0 1 8-4 5 5 0 0 1 8 4c0 6.3-8 11-8 11z"/>
</svg>)svg")
    .tint({1.0f, 0.36f, 0.58f, 1.0f})
    .contain()
    .build();
```

Image 支持：

```cpp
.source(pathOrUrl)
.bingDaily(idx, mkt)
.svg(id).source(svgMarkup)
.fit(eui::ImageFit::Cover)
.cover()
.contain()
.stretch()
.radius(...)
.blur(...)
.opacity(...)
.tint(...)
.translate(...)
.translate3d(...)
.translateX(...)
.translateY(...)
.translateZ(...)
.scale(...)
.rotate(...)
.rotateX(...)
.rotateY(...)
.rotateZ(...)
.perspective(...)
.transformOrigin(...)
```

默认 fit 是 `Cover`，图片会适应裁剪，不会强行压缩变形。

图片 facade 还提供主题色采样 API，适合让按钮、卡片或控制条跟随当前图片变色：

```cpp
const eui::Color accent = eui::image::themeColor(
    "bing://daily?idx=0&mkt=zh-CN",
    {0.38f, 0.72f, 0.96f, 1.0f});
```

`themeColor(source, fallback)` 接受本地路径、`http/https` 图片 URL 和 `bing://daily?...` 源。图片未就绪或解码失败时返回 fallback；成功后返回底层缓存的采样主题色。

## Shadertoy DSL

Shadertoy 是后端无关的 `mainImage()` fragment 图元，使用固定四边形 vertex shader。
同一个 `ShaderToyGraph` 支持文件/inline source、EUI JSON、按顺序执行的多
Pass，以及四路 `sampler2D`。通道 kind 固定为
`ShaderToyChannel::image()`、`buffer()`、`self()`、`none()`；所有 Pass 使用同一
目标尺寸和固定 RGBA32F ping-pong feedback。图片采样固定为 linear/repeat，
Buffer/Self/None 固定为 linear/clamp。

`ShadertoyBuilder::resolutionScale()` 调整整个 graph 的离屏尺寸；
`timeScale()`、`paused()`、`resetKey()` 和 `onCompileError()` 管理时钟、暂停、
feedback 重置与结构化错误。builder 继承 `ShapeBuilderBase` 的布局、裁剪、圆角、
opacity、transform、交互和 dirty rect API。`fill()` 是布局填充，fragment 颜色仍由
`mainImage()` 输出；`rect` 是四边形几何，不是 vertex shader API。鼠标只在变换和
裁剪后的 Shadertoy 区域内更新，区域内开始的拖动会捕获到释放。

标准 uniform、自定义 uniform、JSON/schema、Vulkan 构建期 helper、feedback 和双后端
兼容矩阵见 [Shadertoy 底层图元](Shadertoy.md)。Keyboard、Video、Sound、
Cubemap、Volume、动态纹理、可配置 sampler、每 Pass 格式/分辨率明确不属于当前范围，
也不是待办扩展。可执行示例
[`examples/shadertoy.cpp`](../examples/shadertoy.cpp) 展示 inline Demo 与 Blackhole、
Fish 内置预设切换。

## Polygon DSL

```cpp
ui.polygon("slice")
    .size(120.0f, 120.0f)
    .points({
        {60.0f, 60.0f},
        {120.0f, 0.0f},
        {120.0f, 120.0f},
    })
    .color({0.22f, 0.50f, 0.88f, 1.0f})
    .build();
```

Polygon 支持：

```cpp
.points(...)
.point(x, y)
.clearPoints()
.radius(...)
.color(...)
.opacity(...)
.translate(...)
.translate3d(...)
.translateX(...)
.translateY(...)
.translateZ(...)
.scale(...)
.rotate(...)
.rotateX(...)
.rotateY(...)
.rotateZ(...)
.perspective(...)
.transformOrigin(...)
.states(normal, hover, pressed)
```

`radius(...)` 会对多边形顶点做圆润过渡，适合 tooltip 指针这类小三角形；命中测试会复用同一套圆角几何。渲染后端为 `Polygon` 使用独立 polygon shader，OpenGL 和 Vulkan 都按多边形边段计算覆盖率抗锯齿，不再借圆角矩形 shader 或 bounding box 填充。

## Transform / 2.5D DSL

Transform 是渲染阶段能力，不参与 measure / layout。所有可视元素和 `Row` / `Column` / `Stack` 容器都可以声明 transform，父容器 transform 会以投影矩阵继承到子树。

```cpp
ui.stack("flip.card")
    .size(220.0f, 132.0f)
    .rotateY(open ? 3.14159f : 0.0f)
    .perspective(520.0f)
    .transformOrigin(0.5f, 0.5f)
    .transition(0.42f, eui::Ease::OutBack)
    .animate(eui::AnimProperty::Transform)
    .content([&] {
        ui.rect("flip.card.bg")
            .size(220.0f, 132.0f)
            .radius(18.0f)
            .color({0.18f, 0.28f, 0.72f, 1.0f})
            .build();
    })
    .build();
```

2D API：

```cpp
.translate(x, y)
.translateX(x)
.translateY(y)
.scale(value)
.scale(x, y)
.rotate(radians)
.rotateZ(radians)
.rotate(radians)
.transformOrigin(xRatio, yRatio)
```

2.5D API：

```cpp
.translate3d(x, y, z)
.translateZ(z)
.rotateX(radians)
.rotateY(radians)
.perspective(distance)
```

`rotateX` / `rotateY` 会把元素所在平面投影回屏幕坐标；`perspective(distance)` 是透视距离，值越小透视越强，`0` 表示关闭透视。`translateZ` 只有配合 perspective 才会产生明显视觉缩放。当前不会启用真实 depth buffer，绘制顺序仍由 DSL 树顺序和 `zIndex` 决定；默认 hit-test 仍按未 transform 的布局 frame 计算，需要跟随视觉投影时开启 `.transformedHitTest()`。

## 动画 DSL

动画目标写在元素属性上，Runtime 负责从当前值插值到目标值：

```cpp
ui.rect("actor")
    .x(active ? 420.0f : 40.0f)
    .opacity(active ? 0.4f : 1.0f)
    .rotate(active ? 0.4f : 0.0f)
    .transition(0.42f, eui::Ease::OutBack)
    .animate(eui::AnimProperty::Frame |
             eui::AnimProperty::Opacity |
             eui::AnimProperty::Transform)
    .build();
```

Frame 动画需要显式 `.animate(eui::AnimProperty::Frame)`。窗口大小变化、页面切换导致的普通布局尺寸变化不会默认产生长宽动画。

容器 `Row` / `Column` / `Stack` / `Flow` 也支持 `opacity` 和 transform。Runtime 会把容器的 `translate`、`scale`、`rotate`、`rotateX`、`rotateY`、`translateZ`、`perspective`、`transformOrigin` 组合成投影矩阵并继承到子树，因此弹窗、下拉、菜单、卡片翻转和透视动画会作用到内部 Rect / Polygon / Text / Image / Svg。布局占位仍由未 transform 的逻辑 frame 决定。

### 布局 Frame 动画限制

`Frame` 动画只作用于可见叶子节点：`Rect`、`Text`、`Image`、`Svg` 和 `Polygon`。`Row`、`Column`、`Stack`、`Flow` 是布局容器，它们的逻辑 frame 改变会立即重新测量子树，不会自身执行长宽插值。

当任意祖先容器的目标 frame 改变时，Runtime 会让该子树的叶子 frame 同步到新的布局结果，避免插值期间出现错误的流式排版、命中区域、裁剪或滚动范围。因此，下面的写法不会得到预期的平滑展开：

```cpp
ui.stack("card")
    .size(width, expanded ? 238.0f : 148.0f)
    .content([&] {
        ui.rect("card.background")
            .fill()
            .transition(0.3f)
            .animate(eui::AnimProperty::Frame)
            .build();
    })
    .build();
```

需要平滑展开的面板、卡片或折叠内容，应保持外层布局容器的逻辑尺寸稳定，让可见叶子节点在其中独立插值：

```cpp
constexpr float maxHeight = 238.0f;
const float height = expanded ? maxHeight : 148.0f;
const float top = (maxHeight - height) * 0.5f;

ui.stack("card")
    .size(width, maxHeight) // 不随状态变化，避免祖先布局 snap
    .content([&] {
        ui.rect("card.background")
            .position(0.0f, top)
            .size(width, height)
            .transition(0.3f, eui::Ease::OutCubic)
            .animate(eui::AnimProperty::Frame)
            .build();
    })
    .build();
```

完整、可运行的参考实现见 `examples/animated_card.cpp`。它同时展示了 frame、transform、颜色、圆角、边框、阴影和透明度动画。

当前可动画属性：

- Rect：frame、color、opacity、radius、border、shadow、blur、transform。
- Text：frame、text color、opacity、transform。
- Image / Svg：frame、tint/color、opacity、radius、transform。
- Polygon：frame、color、opacity、transform。

## 组件写法

组件层在 `components/`，不要直接持有 primitive，也不要绕过 Runtime。

当前组件：

- `components::panel(ui, id)`：返回套用 theme token 的 `RectBuilder`。
- `components::text(ui, id)`：返回套用 theme token 文本色的 `TextBuilder`。
- `components::image(ui, id)`：返回套用 theme token 的 `ImageBuilder`。
- `components::markdown(ui, id)`：基于 MD4C 解析 Markdown，在组件层组合 `Column / Row / Stack / Rect / Text` 显示 CommonMark 与启用的 MD4C 扩展。当前接入 MD4C 的 block/span/text 回调面：标题、段落、引用、无序/有序/任务列表、分隔线、fenced/indented code、HTML block、表格和表格对齐，以及 emphasis、strong、link/autolink、image、inline code、strikethrough、LaTeX math、wiki link、underline、inline HTML、entity、soft/hard break 等 inline 内容。inline code、链接、图片、math、wiki、HTML、删除线和下划线会在组件内部 line-box 排版中映射为可见文本、胶囊或装饰线；HTML/CSS 不执行，图片显示为占位文本。默认由 `EUI_ENABLE_MARKDOWN=ON` 启用；关闭后退化为纯文本段落。
- `components::mouseArea(ui, id)`：透明输入热区，封装 tap、press、release、hover、move、drag、scroll、context menu。
- `components::button(ui, id)`：薄 builder，内部组合 `Stack + Rect + Row + Text`。
- `components::checkbox(ui, id)`：无状态 checkbox，点击回调 next checked。
- `components::radio(ui, id)`：无状态 radio，点击回调 next selected。
- `components::toggleSwitch(ui, id)`：无状态 switch，点击回调 next checked。
- `components::progress(ui, id)`：进度条，value 范围 `0.0f - 1.0f`。
- `components::slider(ui, id)`：滑块，点击或拖拽回调 next value。
- `components::input(ui, id)`：基础文本输入，页面传 value，组件回调 next value。
- `components::segmented(ui, id)`：分段选择，点击回调 next index。
- `components::tabs(ui, id)`：标签页切换，点击回调 next index。
- `components::scroll(ui, id)`：滚动条，绑定 Runtime scroll state 后由 Runtime 更新 thumb transform。
- `components::scrollView(ui, id)`：普通滚动区域，适合内容量可完整 compose 的页面；滚轮由 Runtime 按时间推进惯性，滚动内容通过 Runtime transform 移动，滚轮和滚动条拖动不触发整页 compose。
- `components::virtualList(ui, id)`：固定行高虚拟列表，适合超长纵向列表；组件按 `offset + viewport + overscan` 只 compose 可见 slot，行回调收到真实 index、宽度和行高。回调 id 是可见 slot id，不是业务行 id；需要保存行级业务状态时，用真实 index 或业务 key 存在页面状态里。
- `components::dropdown(ui, id)`：下拉选择，页面传 selected/open，组件回调 next index 和 open 状态。
- `components::datePicker(ui, id)`：dialog 式日期选择器，页面传 date/open，面板内调整是 draft，点击 `Done` 后才回调 next date。
- `components::timePicker(ui, id)`：dialog 式时间选择器，页面传 time/open，面板内调整是 draft，点击 `Done` 后才回调 next time。
- `components::colorPicker(ui, id)`：dialog 式颜色选择器，页面传 color/open，RGB slider 和色块只改 draft，点击 `Done` 后才回调 next color。
- `components::dataTable(ui, id)`：简单数据表。
- `components::dialog(ui, id)`：模态对话框，页面传 open 状态。
- `components::toast(ui, id)`：toast 提示，支持 duration。
- `components::tooltip(ui, id)`：轻量提示浮层，支持 anchor、title/value、hover source 和圆润 Polygon 指针，指针边缘走 polygon shader 抗锯齿。
- `components::contextMenu(ui, id)`：右键菜单，支持 position、screen、递归 `ContextMenuItem`、open / onOpenChange；多级菜单通过 `onSelectPath` 返回索引路径。
- `components::lineChart(ui, id)`：折线图，通过 `.style(components::LineStyle::...)` 支持 Linear / Curve / Step 线型，折线段使用 capsule polygon 绘制，hover 数据点显示 tooltip。
- `components::barChart(ui, id)`：柱状图，hover 柱子显示 tooltip。
- `components::pieChart(ui, id)`：饼图，用 `Polygon` 绘制扇区，hover 扇区显示 tooltip。

按钮示例：

```cpp
components::button(ui, "save")
    .size(180.0f, 54.0f)
    .icon(0xF0C7)
    .text("Save")
    .colors(normal, hover, pressed)
    .transition(0.18f)
    .onClick([] {
        // state change
    })
    .build();
```

## Runtime 行为

内部 Runtime 负责：

- 持有 `Ui`。
- 调用 `ui.layout()` 计算逻辑坐标。
- 按 id 缓存 Rect / Polygon / Text / Image / Svg primitive 实例，其中 Image 与 Svg 共用 image instance 和渲染路径。
- 每帧回收已经不在 DSL 树里的 primitive、交互状态和 dirty key 实例。
- 统一处理 pointer event、hit-test、press capture、click。
- disabled 父节点会禁用整棵子树的交互、焦点、文本输入和 IME 光标状态。
- 维护 scroll state；滚轮输入在 Runtime 内累加速度并按时间衰减，滚动条拖动则直接更新 offset。两种路径都只更新滚动 transform 和 dirty rect，不触发整页 compose。少数需要滚动时重组可见内容的组件可以显式使用 `.composeOnScrollOffsetChange()`，例如 `components::virtualList`。
- interactive blocker 会阻断下层 hover / click / focus；弹层、侧边栏、遮罩和面板背景应声明透明或实体 hit rect 来吃掉事件。
- 维护 hover / press 动画状态。
- 推进 transition 动画。
- 维护 dirty rect。
- 使用离屏 framebuffer cache + scissor 做 Runtime 层脏区重绘；后端再负责 cache blit 和窗口 present。
- 在 `layout()` 后缓存同级绘制顺序和子树能力标记，避免 update / hit-test / render 热路径反复分配、排序和扫描无关子树。
- 自动 retained layer cache 会缓存稳定静态子树；候选判断使用 layout/update 阶段缓存的子树标记，避免动画帧里重复递归扫描。
- 对静态、无交互、无动画、无 timer、无 scroll、无 dirty key 的子树做保守 early-out；指针不在子树 bounds 内且没有继承 transform / opacity 变化时，可以复用上一帧 paint bounds。
- 指针没有移动、没有按键边沿、树结构没有变化且上一帧没有动画时，Runtime 会复用上一帧 hover 命中目标，避免复杂静态页面每帧重新全树 hit-test。
- 处理 DPI scale。
- render / shutdown。

纯 hover / press / transition 视觉变化不会重新 compose 页面。click 回调通常会修改 app 状态，因此 Runtime 会设置 `composeRequested()`，`include/eui/dsl_app.h` 再重新 compose 并保守触发 full paint。

## 当前限制

- 已有基础 z-index、矩形 clip 和 Runtime scroll state；复杂圆角 clip、嵌套滚动区域的事件冒泡还没做。
- `components::scrollView` 是推荐的普通滚动区域；超长固定行高数据使用 `components::virtualList`，避免一次性 compose 全部行。底层 `components::scroll` 只负责滚动条，需要和内容容器绑定同一个 Runtime scroll state。
- 已有独立键盘 Press/Repeat/Release、应用级快捷键、focus / text input / 选择 / 剪贴板 / 撤销 / 重做 / IME 预编辑组合串和系统候选窗口定位。
- 还没有事件冒泡。
- 已有左/中/右/X1/X2 button 事件、click / press / release / pointer move / hover / context menu / key / text input / scroll / 多按钮 drag 回调；局部坐标或组合手势开发使用 `components::mouseArea`，简单点击直接绑定可视图元。
- 默认 hit-test 按布局矩形计算；开启 `.transformedHitTest()` 后会按元素当前 transform 和父容器继承矩阵反投影命中。
- 脏区渲染是保守矩形，复杂重叠场景可能扩大重绘区域。Runtime 可以只重绘脏区，Vulkan 可以按 dirty rect 同步 render cache；最终 present 是否也是脏区提交取决于平台窗口系统、图形 API 和驱动能力。
- 当前优化是 Runtime 级遍历、framebuffer cache、dirty rect 和自动 retained layer cache 的组合；OpenGL/Vulkan 后端都提供 retained layer 资源。它不是完整 retained scene graph，复杂 blur、动态 image/svg、交互和动画子树仍会走普通 dirty repaint。
