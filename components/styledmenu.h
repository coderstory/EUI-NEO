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
//   3. 失焦（点击其他窗口/桌面）→ 下一帧 compose 经 core::window::windowIsFocused
//      检测后自关（仅 Windows 同步查询，其他平台恒"有焦点"= 不自动关，
//      仍可靠选中/Esc/再次打开时的单实例关闭）；
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
};

namespace styled_menu_detail {

// 菜单树 → 扁平行列表（顺序渲染序）。路径语义与
// core::platform::contextMenuCommandPaths 对齐：向量的原始下标，分隔线
// 不产生可选中 path。子项组 = 组头行（path={i}，不可选）+ 子项行
// （path={i,j}）。
inline void flattenRecursive(const std::vector<StyledMenuItem>& items,
                             const std::vector<int>& prefix, int depth,
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
            out.push_back(std::move(header));
            flattenRecursive(item.children, path, depth + 1, out);
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

inline std::vector<StyledMenuRow> styledMenuFlatten(const std::vector<StyledMenuItem>& items) {
    std::vector<StyledMenuRow> rows;
    styled_menu_detail::flattenRecursive(items, {}, 0, rows);
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

struct StyledMenuStyle {
    StyledMenuStyle() : StyledMenuStyle(theme::dark()) {}

    // 与 ContextMenuStyle 同色模型（主题 token 派生），外加独立窗专属几何
    explicit StyledMenuStyle(const theme::ThemeColorTokens& tokens) {
        const theme::ThemeMetricTokens& metrics = tokens.metrics;
        background = tokens.dark
            ? core::mixColor(tokens.surface, theme::color(0.0f, 0.0f, 0.0f), 0.16f)
            : tokens.surface;
        hover = tokens.surfaceHover;
        pressed = tokens.surfaceActive;
        text = tokens.text;
        mutedText = theme::withOpacity(tokens.text, 0.54f);
        border = theme::withOpacity(tokens.border, 0.82f);
        separator = theme::withOpacity(tokens.border, 0.60f);
        accent = tokens.primary;
        shadow = theme::popupShadow(tokens);
        radius = metrics.radius.popup;
        panelWidth = 220.0f;
        rowHeight = metrics.control.menuItem;
        separatorHeight = metrics.spacing.compact + metrics.spacing.hairline;
        inset = metrics.spacing.small;
        indent = metrics.spacing.panel;      // 子项缩进
        iconArea = 24.0f;                    // 行首图标位宽
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
    float radius = 10.0f;
    float panelWidth = 220.0f;
    float rowHeight = 34.0f;
    float separatorHeight = 9.0f;
    float inset = 6.0f;
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
    std::vector<StyledMenuRow> rows;
    StyledMenuStyle style;
    std::function<void(const std::vector<int>&)> onSelect;
    core::window::Handle handle = nullptr;
    int focusGraceFrames = 3;  // 创建初期焦点未稳，跳过失焦检查
    bool dismissed = false;
};

// 当前活跃的 styled 菜单（单实例；新开窗前关闭旧窗）
inline std::shared_ptr<StyledMenuRuntime>& activeRuntime() {
    static std::shared_ptr<StyledMenuRuntime> runtime;
    return runtime;
}

inline void requestDismiss(const std::shared_ptr<StyledMenuRuntime>& runtime) {
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

    const std::vector<StyledMenuRow> rows = styledMenuFlatten(items);
    if (rows.empty() || styledMenuSelectableCount(rows) == 0) {
        return false;
    }

    // 关旧开新（单实例）
    if (activeRuntime()) {
        requestDismiss(activeRuntime());
    }

    const float panelHeight = styledMenuPanelHeight(rows, style);
    const float windowWidth = style.panelWidth + style.margin * 2.0f;
    const float windowHeight = panelHeight + style.margin * 2.0f;

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
    runtime->rows = rows;
    runtime->style = style;
    runtime->onSelect = std::move(onSelect);
    activeRuntime() = runtime;

    const std::string prefix = "styledMenu." + (id.empty() ? std::string("menu") : id);

    app::openWindow(app::DslWindowConfig{}
                        .title("EUI Styled Menu")
                        .pageId(prefix)
                        .windowSize(static_cast<int>(std::ceil(windowWidth)),
                                    static_cast<int>(std::ceil(windowHeight)))
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
                        }),
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

                        ui.stack(prefix)
                            .size(screen.width, screen.height)
                            .content([&] {
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
                                            .position(panelX + style.inset, rowY + (style.separatorHeight - 1.0f) * 0.5f)
                                            .size(std::max(0.0f, panelWidth - style.inset * 2.0f), 1.0f)
                                            .color(style.separator)
                                            .build();
                                        rowY += style.separatorHeight;
                                        continue;
                                    }

                                    const float textX = panelX + style.inset +
                                                        static_cast<float>(row.depth) * style.indent;
                                    const float textWidth = std::max(
                                        0.0f, panelWidth - style.inset * 2.0f -
                                                  static_cast<float>(row.depth) * style.indent);

                                    if (row.kind == StyledMenuRowKind::Item && !row.disabled) {
                                        // hover / pressed 反馈行 + 点击选中
                                        ui.rect(rowId + ".hit")
                                            .position(panelX + style.inset, rowY)
                                            .size(std::max(0.0f, panelWidth - style.inset * 2.0f), style.rowHeight)
                                            .states(theme::color(0.0f, 0.0f, 0.0f, 0.0f), style.hover, style.pressed)
                                            .radius(std::max(2.0f, style.radius - 6.0f))
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

                                    const float labelX =
                                        textX + (row.iconCodepoint != 0 ? style.iconArea : 0.0f);
                                    ui.text(rowId + ".label")
                                        .position(labelX, rowY)
                                        .size(std::max(0.0f, textWidth -
                                                  (row.iconCodepoint != 0 ? style.iconArea : 0.0f) -
                                                  (row.checked ? style.iconArea : 0.0f)),
                                              style.rowHeight)
                                        .text(row.text)
                                        .fontSize(style.fontSize)
                                        .lineHeight(style.lineHeight)
                                        .color(labelColor)
                                        .verticalAlign(core::VerticalAlign::Center)
                                        .build();

                                    if (row.kind == StyledMenuRowKind::GroupHeader) {
                                        ui.text(rowId + ".chevron")
                                            .position(panelX + panelWidth - style.inset - style.iconArea, rowY)
                                            .size(style.iconArea, style.rowHeight)
                                            .icon(0xF054)  // fa-chevron-right
                                            .fontSize(style.iconSize - 2.0f)
                                            .lineHeight(style.rowHeight)
                                            .color(style.mutedText)
                                            .horizontalAlign(core::HorizontalAlign::Center)
                                            .verticalAlign(core::VerticalAlign::Center)
                                            .build();
                                    } else if (row.checked) {
                                        ui.text(rowId + ".check")
                                            .position(panelX + panelWidth - style.inset - style.iconArea, rowY)
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
