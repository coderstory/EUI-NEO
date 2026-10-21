// components/styledmenu.h
// styledMenu——独立弹出菜单窗组件（DevDesk 菜单美化需求）：
// 圆角面板 + 主题色 + FontAwesome 图标 + hover/pressed 态 + 分隔线 + disabled 项
// + checked 勾选 + 一级子项内联展开（组头 + 缩进子行，不递归开窗）。
//
// 与 components/contextmenu.h（窗口内绘制、受宿主窗 bounds 裁剪）互补：
// 本组件经 app::openWindow 开**独立**无边框透明置顶子窗，在任意屏幕位置
// （光标处）弹出，不受宿主窗口尺寸限制——128px 桌宠 sprite 窗 / 托盘这类
// 小窗或非 GL 宿主场景的 styled 菜单只能这样做。
//
// 关闭语义（对齐原生菜单直觉）：
//   1. 选中项 → requestWindowClose + onSelect(path)；
//   2. Esc → 关闭（DslWindowConfig.onKeyEvent）；
//   3. 失焦（点击其他窗口/桌面）→ WM_KILLFOCUS / WM_ACTIVATE(deactivate)
//      经平台 focus 回调当拍同步 requestDismiss（DslWindowConfig.
//      onWindowFocusChanged，Windows 布线）；兜底再保留旧的 compose 轮询链
//      （下一帧 compose 经 core::window::windowIsFocused 检测自关，仅
//      Windows 同步查询）——直连链修「托盘驻留/后台进程下 compose 被饿死、
//      失焦后菜单像个独立悬浮窗赖着不走」的真机缺陷；
//   4. 同一时刻只保留一个 styled 菜单（showStyledMenu 开新窗前关旧窗）。
//
// 选中回调路径语义与 core::platform::contextMenuCommandPaths 一致：path 是
// 原始 items 向量的下标路径（顶层 {i}、子项 {i,j}），分隔线不可选——应用层
// 可让 styled 菜单与原生降级菜单共用同一套选择处理代码。
//
// 线程约定：showStyledMenu / closeStyledMenu 只可在主线程调用（经 app::openWindow
// 入队，主循环下一帧创建）。菜单文本按 UTF-8 传入（同其余组件）。
// headless（无 GL 窗口/未初始化）时 openWindow 入队不崩，但菜单永不显示——
// 结构断言测试请走 styledMenuFlatten / styledMenuPanelHeight /
// styledMenuClampPosition 纯函数。
#pragma once

#include "components/theme.h"
#include "core/dsl.h"
#include "eui/dsl_app.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace components {

struct StyledMenuItem {
    // "-" = 分隔线（iconCodepoint/disabled/checked/children 忽略）
    std::string text;
    int iconCodepoint = 0;  // FontAwesome 码点，0 = 无图标
    bool disabled = false;
    bool checked = false;   // 行尾画勾选图标（fa-check 0xF00C）
    // 非空 = 子项组：本行画成组头（muted + chevron，不可选），
    // 子项紧随其后缩进渲染（独立弹窗内不递归开窗）
    std::vector<StyledMenuItem> children;
    // opt-in 组折叠：true = 默认只显组头（折叠态），运行期点组头/chevron 切换
    // 展开。false（默认）= 旧行为（总展平），向后兼容。DevDesk 桌宠换肤组设 true。
    bool collapsedByDefault = false;

    StyledMenuItem() = default;
    explicit StyledMenuItem(std::string value) : text(std::move(value)) {}

    bool isSeparator() const { return text == "-"; }
    bool hasChildren() const { return !children.empty(); }
};

enum class StyledMenuRowKind {
    Item,         // 可选中
    Separator,    // 分隔线
    GroupHeader,  // 子项组头（不可选）
};

struct StyledMenuRow {
    StyledMenuRowKind kind = StyledMenuRowKind::Item;
    std::vector<int> path;  // 原始 items 下标路径（{i} 或 {i,j}）
    std::string text;
    int iconCodepoint = 0;
    bool disabled = false;
    bool checked = false;
    int depth = 0;  // 缩进层级（组内子项 = 1）
    // 组头行：源项 collapsedByDefault=true 时为可折叠组（true），
    // 运行期可点组头/chevron 切换展开态；false = 总展平（旧行为）
    bool collapsible = false;
};

namespace styled_menu_detail {

// 菜单树 → 扁平行列表（顺序渲染序）。路径语义与
// core::platform::contextMenuCommandPaths 对齐：向量的原始下标，分隔线
// 不产生可选中 path。子项组 = 组头行（path={i}，不可选）+ 子项行
// （path={i,j}）。
//
// expanded 为 nullptr → 全展开（忽略 collapsedByDefault，所有组展平），
//   用于开窗高度/宽度预留（宁高勿叠，展开子行不溢出窗外）。
// expanded 非 nullptr → 按展开态：collapsedByDefault=true 且 path 不在
//   expanded 中 → 只推组头（折叠态）；否则推组头 + 子行。
// collapsedByDefault=false 的组恒展平（旧行为，向后兼容）。
inline void flattenRecursive(const std::vector<StyledMenuItem>& items,
                             const std::vector<int>& prefix, int depth,
                             const std::set<std::vector<int>>* expanded,
                             std::vector<StyledMenuRow>& out) {
    for (std::size_t i = 0; i < items.size(); ++i) {
        const StyledMenuItem& item = items[i];
        std::vector<int> path = prefix;
        path.push_back(static_cast<int>(i));
        if (item.isSeparator()) {
            StyledMenuRow row;
            row.kind = StyledMenuRowKind::Separator;
            row.path = path;
            out.push_back(std::move(row));
            continue;
        }
        if (item.hasChildren()) {
            StyledMenuRow header;
            header.kind = StyledMenuRowKind::GroupHeader;
            header.path = path;
            header.text = item.text;
            header.iconCodepoint = item.iconCodepoint;
            header.depth = depth;
            header.collapsible = item.collapsedByDefault;
            out.push_back(std::move(header));
            const bool expand =
                !item.collapsedByDefault ||
                expanded == nullptr ||
                expanded->count(path) > 0;
            if (expand) {
                flattenRecursive(item.children, path, depth + 1, expanded, out);
            }
            continue;
        }
        StyledMenuRow row;
        row.kind = StyledMenuRowKind::Item;
        row.path = path;
        row.text = item.text;
        row.iconCodepoint = item.iconCodepoint;
        row.disabled = item.disabled;
        row.checked = item.checked;
        row.depth = depth;
        out.push_back(std::move(row));
    }
}

} // namespace styled_menu_detail

// 默认折叠态 flatten：collapsedByDefault=true 组折叠（只出组头），
// collapsedByDefault=false 组展平（旧行为）。向后兼容——未设
// collapsedByDefault 的菜单与改造前行为完全一致。
inline std::vector<StyledMenuRow> styledMenuFlatten(const std::vector<StyledMenuItem>& items) {
    std::vector<StyledMenuRow> rows;
    const std::set<std::vector<int>> emptyExpanded;
    styled_menu_detail::flattenRecursive(items, {}, 0, &emptyExpanded, rows);
    return rows;
}

// 指定展开态 flatten：expanded 含 path 的可折叠组展开，其余可折叠组折叠。
// 运行期点组头切换 expandedSet 后重算 rows 用此重载。
inline std::vector<StyledMenuRow> styledMenuFlatten(const std::vector<StyledMenuItem>& items,
                                                    const std::set<std::vector<int>>& expanded) {
    std::vector<StyledMenuRow> rows;
    styled_menu_detail::flattenRecursive(items, {}, 0, &expanded, rows);
    return rows;
}

// 全展开 flatten（忽略 collapsedByDefault，所有组展平）——用于开窗时按
// 全展开行数预留**宽度**（折叠子行文本也要装得下）；高度改按当前折叠态
// 行数（U3：收起态不空留，组头 toggle 时经 core::window::setWindowSize
// 即时回收/补高，见 showStyledMenu）。
inline std::vector<StyledMenuRow> styledMenuFlattenAllExpanded(const std::vector<StyledMenuItem>& items) {
    std::vector<StyledMenuRow> rows;
    styled_menu_detail::flattenRecursive(items, {}, 0, nullptr, rows);
    return rows;
}

// 可选中行数（Item 且非 disabled）：为 0 时 showStyledMenu 判定失败
inline std::size_t styledMenuSelectableCount(const std::vector<StyledMenuRow>& rows) {
    std::size_t count = 0;
    for (const StyledMenuRow& row : rows) {
        if (row.kind == StyledMenuRowKind::Item && !row.disabled) {
            ++count;
        }
    }
    return count;
}

// 是否有任一行带图标：决定整菜单是否保留行首图标列——有则无图标行也空出该
// 列，文字跨行左对齐（微信菜单的图标距/对齐规则；全菜单无图标时不空列）。
// showStyledMenu 按全展开行表判定，窗口生命期内恒定（组头切换不改变列宽）。
inline bool styledMenuAnyIcon(const std::vector<StyledMenuRow>& rows) {
    for (const StyledMenuRow& row : rows) {
        if (row.kind != StyledMenuRowKind::Separator && row.iconCodepoint != 0) {
            return true;
        }
    }
    return false;
}

struct StyledMenuStyle {
    StyledMenuStyle() : StyledMenuStyle(theme::dark()) {}

    // 微信观感整改（真机 UAT：「样式与微信不搭」）：色档与几何全部从注入
    // tokens 派生——注入侧（DevDesk componentTokensWeChat）传 wechat 令牌时
    // 本构造即产出微信菜单观感（白底圆角卡 / ~36 行高 / hover=selectedBg /
    // 文字 textPrimary+textSecondary 当量档），spec 接口（showStyledMenu 签名）
    // 不变。色档注意三项与旧派生的差异：
    //   - 面板恒 surface（不再按 dark 混黑）：注入的 wechat.cardBg 深浅主题
    //     同为 #FFFFFF 白卡，旧 dark 混黑会把白卡洗成灰卡；
    //   - hover 恒 surfaceActive（wechat.selectedBg #E5E5E5）：旧 surfaceHover
    //     映射 dividerSecondary #EDEDED，白卡上近不可见；
    //   - 阴影恒浅色档（轻）：白卡配 dark 重阴影不搭，与上面第一条同理。
    explicit StyledMenuStyle(const theme::ThemeColorTokens& tokens) {
        const theme::ThemeMetricTokens& metrics = tokens.metrics;
        background = tokens.surface;
        hover = tokens.surfaceActive;
        pressed = tokens.surfaceActive;
        text = tokens.text;
        mutedText = theme::withOpacity(tokens.text, 0.67f);
        border = theme::withOpacity(tokens.border, 0.60f);
        separator = theme::withOpacity(tokens.border, 0.80f);
        accent = tokens.primary;
        theme::ThemeColorTokens shadowTokens = tokens;
        shadowTokens.dark = false;  // 白卡恒配浅档 popup 阴影（12 blur / 5 偏移）
        shadow = theme::popupShadow(shadowTokens);
        // 几何：微信菜单档——圆角 radiusWeChat(6) / 行高 ~36 / 行内边距 12-16
        radius = metrics.radius.small;
        panelWidth = 220.0f;
        rowHeight = 36.0f;
        separatorHeight = metrics.spacing.compact + metrics.spacing.hairline;
        inset = metrics.spacing.tiny;        // hover 药丸/面板内缘距（4）
        rowPaddingX = 12.0f;                 // 行内边距：文字/图标距药丸左缘（12-16 档）
        indent = metrics.spacing.panel;      // 子项缩进
        iconArea = 24.0f;                     // 行首图标位宽（任一行有图标即整列保留）
        margin = 12.0f;                      // 阴影留白（窗比面板大的部分）
        fontSize = metrics.typography.option;
        iconSize = metrics.typography.label;
        lineHeight = metrics.typography.option + metrics.typography.lineGapTight;
    }

    core::Color background;
    core::Color hover;
    core::Color pressed;
    core::Color text;
    core::Color mutedText;
    core::Color border;
    core::Color separator;
    core::Color accent;
    core::Shadow shadow;
    float radius = 6.0f;
    float panelWidth = 220.0f;
    float rowHeight = 36.0f;
    float separatorHeight = 9.0f;
    float inset = 4.0f;
    float rowPaddingX = 12.0f;
    float indent = 24.0f;
    float iconArea = 24.0f;
    float margin = 12.0f;
    float fontSize = 15.0f;
    float iconSize = 14.0f;
    float lineHeight = 18.0f;
};

// 面板高度（不含窗四周阴影留白）
inline float styledMenuPanelHeight(const std::vector<StyledMenuRow>& rows, const StyledMenuStyle& style) {
    float height = style.inset * 2.0f;
    for (const StyledMenuRow& row : rows) {
        height += row.kind == StyledMenuRowKind::Separator ? style.separatorHeight : style.rowHeight;
    }
    return height;
}

// 窗口尺寸（窗宽高，物理像素——openWindow 的 windowSize 口径）
struct StyledMenuWindowSize {
    float width = 0.0f;
    float height = 0.0f;
};

// 面板度量（逻辑单位）+ 缩放系数 → 窗口尺寸（物理像素）。
// 面板四周 style.margin 为阴影留白（同样按逻辑单位定义），一并折算。
// scale = core::window::windowScaleForPoint(锚点) × app::uiScale()：窗口尺寸走
// 后端客户区物理像素，而布局按 framebuffer / (dpiScale × uiScale) 折算逻辑
// 单位——不折算则高 DPI 显示器上窗比内容小一个缩放比（125% 下菜单末行溢出
// 窗底、长菜单项被截）。scale <= 0（查询异常/未配置）按 1:1 降级。
// 返回值已 ceil 到整像素：同一份值同时用于位置钳制与 windowSize，两处必须
// 完全一致（钳制按物理像素工作区算，用逻辑尺寸会钳错）。
// 纯函数（无平台/主题依赖），单测锁 1:1 与 1.25 两种取值。
inline StyledMenuWindowSize styledMenuWindowSize(float panelWidth, float panelHeight,
                                                 float margin, float scale) {
    const float factor = scale > 0.0f ? scale : 1.0f;
    return StyledMenuWindowSize{
        std::ceil((panelWidth + margin * 2.0f) * factor),
        std::ceil((panelHeight + margin * 2.0f) * factor)};
}

struct StyledMenuRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// 期望位（左上角）+ 菜单尺寸 + 工作区 → 钳制进工作区的位置：
// 越界右缘 → 贴右；越界下缘 → 向上翻转（贴光标上沿）；翻转后仍越上界 → 贴顶。
// 工作区尺寸无效（w/h<=0，无头/查询失败）时原样返回。
inline StyledMenuRect styledMenuClampPosition(float desiredX, float desiredY,
                                              float width, float height,
                                              float areaX, float areaY,
                                              float areaWidth, float areaHeight) {
    StyledMenuRect rect{desiredX, desiredY, width, height};
    if (areaWidth <= 0.0f || areaHeight <= 0.0f) {
        return rect;
    }
    const float right = areaX + areaWidth;
    const float bottom = areaY + areaHeight;
    if (rect.x + width > right) {
        rect.x = std::max(areaX, right - width);
    }
    if (rect.x < areaX) {
        rect.x = areaX;
    }
    if (rect.y + height > bottom) {
        rect.y = desiredY - height;  // 向上翻转
    }
    if (rect.y + height > bottom) {
        rect.y = std::max(areaY, bottom - height);
    }
    if (rect.y < areaY) {
        rect.y = areaY;
    }
    return rect;
}

namespace styled_menu_detail {

struct StyledMenuRuntime {
    std::vector<StyledMenuItem> items;  // 原始菜单树（组头切换展开态时重算 rows）
    std::set<std::vector<int>> expandedSet;  // 当前展开的可折叠组 path 集合
    std::vector<StyledMenuRow> rows;  // 当前展开态扁平行（渲染用）
    StyledMenuStyle style;
    std::function<void(const std::vector<int>&)> onSelect;
    core::window::Handle handle = nullptr;
    int focusGraceFrames = 3;  // 创建初期焦点未稳，跳过 compose 轮询检查
    bool dismissed = false;
    // 整菜单是否保留行首图标列（styledMenuAnyIcon 全展开判定，开窗时算好，
    // 组头切换不改列宽）——无图标行也空列，文字跨行左对齐
    bool anyIcon = false;
    // U3（2026-09-28 DevDesk UI 现代化）：开窗时的面板逻辑宽与物理/逻辑
    // 折算系数——组头 toggle 重算行后按最新行数重测窗高（styledMenuWindowSize
    // 同口径），宽度不随折叠变化
    float panelWidth = 0.0f;
    float windowScale = 1.0f;
};

// 当前活跃的 styled 菜单（单实例；新开窗前关闭旧窗）
inline std::shared_ptr<StyledMenuRuntime>& activeRuntime() {
    static std::shared_ptr<StyledMenuRuntime> runtime;
    return runtime;
}

// 注意：参数必须按值拷贝，不能按 const& 绑定——closeStyledMenu / 菜单再开
// （showStyledMenu 关旧开新）直接传 activeRuntime() 的引用，函数内
// activeRuntime().reset() 会把调用方那个 shared_ptr 本身置空，随后
// runtime->handle 解引用空指针崩（DevDesk pet_probes 实测 SIGSEGV；
// 真实场景「菜单开着再开第二个」同样触发）。按值拷贝后 reset 只清静态，
// 本函数持有的副本仍有效。
inline void requestDismiss(std::shared_ptr<StyledMenuRuntime> runtime) {
    if (!runtime || runtime->dismissed) {
        return;
    }
    runtime->dismissed = true;
    core::window::Handle handle = runtime->handle;
    if (activeRuntime() == runtime) {
        activeRuntime().reset();
    }
    runtime->handle = nullptr;
    if (handle != nullptr) {
        core::window::requestWindowClose(handle);
    }
}

} // namespace styled_menu_detail

/**
 * @brief 在屏幕坐标 (screenX, screenY)（物理像素，左上原点，同 Win32
 *        GetCursorPos / glfwSetWindowPos 语义）弹出一个 styled 菜单独立窗。
 *
 * @return true = 已入队开窗（异步显示）；false = 构建失败（空项/无可选行），
 *         调用方应降级（如 core::platform::showContextMenu 原生菜单）。
 * @param onSelect 选中项回调（path 语义见文件头注释），触发后菜单自动关闭。
 *
 * 只可在主线程调用（app::openWindow 入队 + requestUpdate 唤帧）。
 */
inline bool showStyledMenu(const std::string& id,
                           std::vector<StyledMenuItem> items,
                           const StyledMenuStyle& style,
                           float screenX, float screenY,
                           std::function<void(const std::vector<int>&)> onSelect) {
    using namespace styled_menu_detail;

    // 当前折叠态行（初始显示 + 降级守卫）：collapsedByDefault=true 组只出组头
    const std::vector<StyledMenuRow> currentRows = styledMenuFlatten(items);
    if (currentRows.empty() || styledMenuSelectableCount(currentRows) == 0) {
        return false;
    }
    // 全展开行（开窗**宽度**预留——宽度按最宽文本项，折叠子行文本也要装得
    // 下；高度改按当前折叠态行数，收起态不空留空白区，展开时经运行期
    // setWindowSize 补高，见下方 toggle 回调）
    const std::vector<StyledMenuRow> fullRows = styledMenuFlattenAllExpanded(items);

    // 关旧开新（单实例）
    if (activeRuntime()) {
        requestDismiss(activeRuntime());
    }

    // 宽度自适应（2026-09-24 DevDesk 反馈：固定 220 装不下较长中文菜单
    // 文本）：最长行文本实宽 + 行内边距（rowPaddingX，12-16 档）×2 + 行首
    // 图标列（任一行有图标即整列保留）+ 尾部勾选/箭头位 + 缩进 + 双侧 inset，
    // 下限 style.panelWidth。按全展开行算宽——折叠子行文本也要装得下
    const bool anyIcon = styledMenuAnyIcon(fullRows);
    float panelWidth = style.panelWidth;
    for (const StyledMenuRow& row : fullRows) {
        if (row.kind == StyledMenuRowKind::Separator) continue;
        const float textW = core::TextPrimitive::measureTextWidth(
            row.text, {}, style.fontSize);
        const float rowNeed = (style.inset + style.rowPaddingX) * 2.0f +
                              (anyIcon ? style.iconArea : 0.0f) +
                              static_cast<float>(row.depth) * style.indent +
                              textW + style.iconArea;
        panelWidth = std::max(panelWidth, rowNeed);
    }
    // U3：高度按当前折叠态行数——收起态不空留（原按全展开预留，折叠时组头
    // 下方留白一整块）；展开态经组头 toggle 运行时补高（窗口尺寸不再定死）
    const float panelHeight = styledMenuPanelHeight(currentRows, style);
    // 逻辑单位 → 物理像素（显示器缩放 × 用户缩放）：窗尺寸是后端客户区物理
    // 像素，布局却按 framebuffer / (dpiScale × uiScale) 折算逻辑空间，不折算
    // 高 DPI 下窗比内容小一个缩放比。位置/工作区本身已是物理像素，不参与。
    const float windowScale = core::window::windowScaleForPoint(screenX, screenY) * app::uiScale();
    const StyledMenuWindowSize windowSize = styledMenuWindowSize(
        panelWidth, panelHeight, style.margin, windowScale);
    const float windowWidth = windowSize.width;
    const float windowHeight = windowSize.height;

    // 位置钳制：选包含期望点的工作区，查不到（无头）则原样
    float clampedX = screenX;
    float clampedY = screenY;
    {
        const std::vector<core::window::MonitorWorkArea> areas =
            core::window::getMonitorWorkAreas();
        core::window::MonitorWorkArea chosen;
        bool haveArea = false;
        for (const core::window::MonitorWorkArea& area : areas) {
            if (area.width <= 0 || area.height <= 0) {
                continue;
            }
            if (screenX >= static_cast<float>(area.x) &&
                screenX < static_cast<float>(area.x + area.width) &&
                screenY >= static_cast<float>(area.y) &&
                screenY < static_cast<float>(area.y + area.height)) {
                chosen = area;
                haveArea = true;
                break;
            }
        }
        if (!haveArea) {
            for (const core::window::MonitorWorkArea& area : areas) {
                if (area.width > 0 && area.height > 0) {
                    chosen = area;
                    haveArea = true;
                    break;
                }
            }
        }
        if (haveArea) {
            const StyledMenuRect rect = styledMenuClampPosition(
                screenX, screenY, windowWidth, windowHeight,
                static_cast<float>(chosen.x), static_cast<float>(chosen.y),
                static_cast<float>(chosen.width), static_cast<float>(chosen.height));
            clampedX = rect.x;
            clampedY = rect.y;
        }
    }

    auto runtime = std::make_shared<StyledMenuRuntime>();
    runtime->items = std::move(items);  // 原始菜单树（组头切换时重算 rows）
    runtime->rows = currentRows;        // 初始折叠态显示行
    runtime->style = style;
    runtime->onSelect = std::move(onSelect);
    runtime->anyIcon = anyIcon;         // 图标列恒定（全展开判定，见注释）
    runtime->panelWidth = panelWidth;   // U3：toggle 重测窗高时复用
    runtime->windowScale = windowScale; // U3：toggle 重测窗高时复用
    activeRuntime() = runtime;

    const std::string prefix = "styledMenu." + (id.empty() ? std::string("menu") : id);

    app::openWindow(app::DslWindowConfig{}
                        .title("EUI Styled Menu")
                        .pageId(prefix)
                        // 与上面钳制用的是同一份（已折算 + 已取整）尺寸
                        .windowSize(static_cast<int>(windowSize.width),
                                    static_cast<int>(windowSize.height))
                        .windowPosition(static_cast<int>(std::floor(clampedX)),
                                        static_cast<int>(std::floor(clampedY)))
                        .decorated(false)               // 无边框
                        .alwaysOnTop(true)              // 盖住宿主
                        .resizable(false)
                        .focusOnShow(true)              // 抢焦点：失焦自关的前提
                        .hideFromTaskbar(true)          // 不进任务栏/Alt+Tab
                        .transparentFramebuffer(true)   // 圆角外逐像素透明
                        .ignoreClearColorOverride()     // 透明底不被主题广播冲掉
                        .windowEffect(core::platform::WindowEffect::None)  // 不叠系统材质
                        .clearColor({0.0f, 0.0f, 0.0f, 0.0f})
                        .onWindowCreated([runtime](core::window::Handle handle) {
                            runtime->handle = handle;
                        })
                        // 句柄清空只在仍指向本窗时做（换肤式重开：新窗先建后销毁旧窗）
                        .onWindowDestroyed([runtime](core::window::Handle destroyed) {
                            if (runtime->handle == destroyed) {
                                runtime->handle = nullptr;
                            }
                            if (activeRuntime() == runtime) {
                                activeRuntime().reset();
                            }
                        })
                        .onKeyEvent([runtime](const eui::KeyEvent& key) {
                            if (key.isDown() && key.key == eui::InputKey::Escape) {
                                requestDismiss(runtime);
                            }
                        })
#if defined(_WIN32)
                        // 失焦直连关闭（真机缺陷：托盘驻留/后台进程下菜单窗
                        // 可能拿不到前台、compose 轮询被饿死，失焦后像独立
                        // 悬浮窗赖着不走）：WM_KILLFOCUS / WM_ACTIVATE(
                        // deactivate) 当拍同步 requestDismiss，走与选中/Esc
                        // 同一条关闭路径。其他平台不布线（windowIsFocused 恒
                        // 真的 compose 降级语义保持不变，macOS 行为零变化）。
                        .onWindowFocusChanged([runtime](core::window::Handle, bool focused) {
                            if (!focused) {
                                requestDismiss(runtime);
                            }
                        })
#endif
                        ,  // DslWindowConfig 建造链结束，下一实参 = compose 闭包
                    [runtime, prefix](eui::Ui& ui, const eui::Screen& screen) {
                        if (runtime->dismissed || runtime->rows.empty()) {
                            return;
                        }
                        // 失焦自关（点别处 = 关菜单，对齐原生直觉）；
                        // 创建初期几帧焦点未稳，跳过检查
                        if (runtime->focusGraceFrames > 0) {
                            --runtime->focusGraceFrames;
                        } else if (runtime->handle != nullptr &&
                                   !core::window::windowIsFocused(runtime->handle)) {
                            requestDismiss(runtime);
                            return;
                        }

                        const StyledMenuStyle& style = runtime->style;
                        const float panelX = style.margin;
                        const float panelY = style.margin;
                        const float panelWidth = std::max(0.0f, screen.width - style.margin * 2.0f);
                        const float panelHeight = std::max(0.0f, screen.height - style.margin * 2.0f);
                        // 微信行几何派生量：hover 药丸（行命中区）贴面板内缘留
                        // inset 空隙；文字/图标在药丸内再留 rowPaddingX 内边距；
                        // 任一行有图标时全菜单保留行首图标列（无图标行空列，
                        // 文字跨行对齐）；组头/勾选行的尾部图标位在文本宽度里
                        // 显式预留（不再与长文本争位）
                        const float pillX = panelX + style.inset;
                        const float pillWidth = std::max(0.0f, panelWidth - style.inset * 2.0f);
                        const float pillRadius = std::max(2.0f, style.radius - 2.0f);
                        const float iconColumn = runtime->anyIcon ? style.iconArea : 0.0f;

                        ui.stack(prefix)
                            .size(screen.width, screen.height)
                            .content([&] {
                                // 全窗 dismiss（类 12 click-away 结构落点，
                                // 仿 components/contextmenu.h:166-173）：透明
                                // 命中层先于面板建、尺寸=整窗、z 垫底——点
                                // 菜单窗内任何非行/非面板区（含阴影留白）都
                                // 视为点击外部 → 关菜单。失焦自关的兜底
                                //（Windows 点桌面不换前台窗时菜单会常驻）
                                ui.rect(prefix + ".dismiss")
                                    .size(screen.width, screen.height)
                                    .states(theme::color(0.0f, 0.0f, 0.0f, 0.0f),
                                            theme::color(0.0f, 0.0f, 0.0f, 0.0f),
                                            theme::color(0.0f, 0.0f, 0.0f, 0.0f))
                                    .onClick([runtime] { requestDismiss(runtime); })
                                    .build();

                                // 圆角面板 + 边框 + 阴影
                                ui.rect(prefix + ".panel")
                                    .position(panelX, panelY)
                                    .size(panelWidth, panelHeight)
                                    .color(style.background)
                                    .radius(style.radius)
                                    .border(1.0f, style.border)
                                    .shadow(style.shadow)
                                    .build();

                                float rowY = panelY + style.inset;
                                for (std::size_t index = 0; index < runtime->rows.size(); ++index) {
                                    const StyledMenuRow& row = runtime->rows[index];
                                    const std::string rowId = prefix + ".row." + std::to_string(index);
                                    if (row.kind == StyledMenuRowKind::Separator) {
                                        ui.rect(rowId)
                                            .position(pillX, rowY + (style.separatorHeight - 1.0f) * 0.5f)
                                            .size(pillWidth, 1.0f)
                                            .color(style.separator)
                                            .build();
                                        rowY += style.separatorHeight;
                                        continue;
                                    }

                                    // 行首内容锚点：药丸内 rowPaddingX 内边距 + 子项缩进
                                    const float textX = pillX + style.rowPaddingX +
                                                        static_cast<float>(row.depth) * style.indent;
                                    // 尾部图标位（组头 chevron / 勾选 check）在文本宽度里预留
                                    const float trailing =
                                        (row.kind == StyledMenuRowKind::GroupHeader || row.checked)
                                            ? style.iconArea
                                            : 0.0f;
                                    const float labelX = textX + iconColumn;
                                    const float labelWidth = std::max(
                                        0.0f, pillX + pillWidth - style.rowPaddingX - trailing - labelX);

                                    if (row.kind == StyledMenuRowKind::Item && !row.disabled) {
                                        // hover / pressed 反馈行 + 点击选中
                                        ui.rect(rowId + ".hit")
                                            .position(pillX, rowY)
                                            .size(pillWidth, style.rowHeight)
                                            .states(theme::color(0.0f, 0.0f, 0.0f, 0.0f), style.hover, style.pressed)
                                            .radius(pillRadius)
                                            .instantStates()
                                            .onClick([runtime, path = row.path] {
                                                const std::function<void(const std::vector<int>&)> onSelect =
                                                    runtime->onSelect;
                                                requestDismiss(runtime);
                                                if (onSelect) {
                                                    onSelect(path);
                                                }
                                                app::requestUpdate();
                                            })
                                            .build();
                                    }

                                    if (row.kind == StyledMenuRowKind::GroupHeader && row.collapsible) {
                                        // 折叠组头 hit：点击/chevron 切换展开态，不 dismiss
                                        // （菜单保持打开，子行画进预留高度区）。窗口生命期内
                                        // expandedSet 持久——切回折叠态也走此路径
                                        ui.rect(rowId + ".hit")
                                            .position(pillX, rowY)
                                            .size(pillWidth, style.rowHeight)
                                            .states(theme::color(0.0f, 0.0f, 0.0f, 0.0f), style.hover, style.hover)
                                            .radius(pillRadius)
                                            .instantStates()
                                            .onClick([runtime, path = row.path] {
                                                if (runtime->expandedSet.count(path) > 0) {
                                                    runtime->expandedSet.erase(path);
                                                } else {
                                                    runtime->expandedSet.insert(path);
                                                }
                                                runtime->rows = styledMenuFlatten(
                                                    runtime->items, runtime->expandedSet);
                                                // U3：收起态高度回收——按最新行数重算
                                                // 面板高并即时改窗尺寸（折叠回收、展开
                                                // 补高，双向都重测；宽不随折叠变化）。
                                                // 窗口 handle 未创建（headless/降级）时
                                                // 跳过，布局仍按 screen 尺寸自动跟随。
                                                if (runtime->handle != nullptr &&
                                                    runtime->panelWidth > 0.0f) {
                                                    const components::StyledMenuWindowSize compact =
                                                        styledMenuWindowSize(
                                                            runtime->panelWidth,
                                                            styledMenuPanelHeight(
                                                                runtime->rows, runtime->style),
                                                            runtime->style.margin,
                                                            runtime->windowScale);
                                                    core::window::setWindowSize(
                                                        runtime->handle,
                                                        static_cast<int>(compact.width),
                                                        static_cast<int>(compact.height));
                                                }
                                                app::requestUpdate();
                                            })
                                            .build();
                                    }

                                    const core::Color labelColor =
                                        row.disabled || row.kind == StyledMenuRowKind::GroupHeader
                                            ? style.mutedText
                                            : style.text;

                                    if (row.iconCodepoint != 0) {
                                        ui.text(rowId + ".icon")
                                            .position(textX, rowY)
                                            .size(style.iconArea, style.rowHeight)
                                            .icon(row.iconCodepoint)
                                            .fontSize(style.iconSize)
                                            .lineHeight(style.rowHeight)
                                            .color(labelColor)
                                            .horizontalAlign(core::HorizontalAlign::Center)
                                            .verticalAlign(core::VerticalAlign::Center)
                                            .build();
                                    }

                                    // 文字起点恒在图标列之后（anyIcon 时无图标行空列，
                                    // 跨行对齐——微信菜单规则），尾部图标位已在
                                    // labelWidth 里预留
                                    ui.text(rowId + ".label")
                                        .position(labelX, rowY)
                                        .size(labelWidth, style.rowHeight)
                                        .text(row.text)
                                        .fontSize(style.fontSize).fontWeight(400)
                                        .lineHeight(style.lineHeight)
                                        .color(labelColor)
                                        .verticalAlign(core::VerticalAlign::Center)
                                        .build();

                                    if (row.kind == StyledMenuRowKind::GroupHeader) {
                                        // chevron 随折叠态：折叠 ▸(0xF054)/展开 ▾(0xF078)；
                                        // 非折叠组恒右箭头（旧行为，总展平）
                                        const int chevronIcon = row.collapsible
                                            ? (runtime->expandedSet.count(row.path) > 0
                                               ? 0xF078   // ▾ fa-chevron-down（展开态）
                                               : 0xF054)  // ▸ fa-chevron-right（折叠态）
                                            : 0xF054;
                                        ui.text(rowId + ".chevron")
                                            .position(pillX + pillWidth - style.rowPaddingX - style.iconArea, rowY)
                                            .size(style.iconArea, style.rowHeight)
                                            .icon(chevronIcon)
                                            .fontSize(style.iconSize - 2.0f)
                                            .lineHeight(style.rowHeight)
                                            .color(style.mutedText)
                                            .horizontalAlign(core::HorizontalAlign::Center)
                                            .verticalAlign(core::VerticalAlign::Center)
                                            .build();
                                    } else if (row.checked) {
                                        ui.text(rowId + ".check")
                                            .position(pillX + pillWidth - style.rowPaddingX - style.iconArea, rowY)
                                            .size(style.iconArea, style.rowHeight)
                                            .icon(0xF00C)  // fa-check
                                            .fontSize(style.iconSize)
                                            .lineHeight(style.rowHeight)
                                            .color(style.accent)
                                            .horizontalAlign(core::HorizontalAlign::Center)
                                            .verticalAlign(core::VerticalAlign::Center)
                                            .build();
                                    }

                                    rowY += style.rowHeight;
                                }
                            })
                            .build();
                    });

    return true;
}

// 主动关闭当前 styled 菜单（无则无操作）。幂等。
inline void closeStyledMenu() {
    styled_menu_detail::requestDismiss(styled_menu_detail::activeRuntime());
}

} // namespace components
