#pragma once

#include "components/theme.h"
#include "core/dsl.h"
#include "eui/signal.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace components {

struct DropdownStyle {
    DropdownStyle() : DropdownStyle(theme::dark()) {}

    explicit DropdownStyle(const theme::ThemeColorTokens& tokens) {
        field = tokens.surface;
        fieldHover = tokens.surfaceHover;
        fieldPressed = tokens.surfaceActive;
        popup = tokens.dark
            ? core::mixColor(tokens.surface, theme::color(0.0f, 0.0f, 0.0f), 0.12f)
            : tokens.surface;
        optionHover = tokens.surfaceHover;
        optionPressed = tokens.surfaceActive;
        selected = theme::withAlpha(tokens.primary, tokens.dark ? 0.24f : 0.14f);
        text = tokens.text;
        mutedText = theme::withOpacity(tokens.text, 0.60f);
        accent = tokens.primary;
        border = theme::withOpacity(tokens.border, 0.78f);
        shadow = theme::popupShadow(tokens);
        radius = tokens.metrics.radius.card;
    }

    core::Color field;
    core::Color fieldHover;
    core::Color fieldPressed;
    core::Color popup;
    core::Color optionHover;
    core::Color optionPressed;
    core::Color selected;
    core::Color text;
    core::Color mutedText;
    core::Color accent;
    core::Color border;
    core::Shadow shadow;
    float radius = 12.0f;
};

class DropdownBuilder {
public:
    DropdownBuilder(core::dsl::Ui& ui, std::string id)
        : ui_(ui), id_(std::move(id)) {}

    DropdownBuilder& size(float width, float height) { width_ = width; height_ = height; return *this; }
    DropdownBuilder& items(std::vector<std::string> value) { items_ = std::move(value); return *this; }
    DropdownBuilder& selected(int value) { selected_ = value; return *this; }
    DropdownBuilder& bind(eui::Signal<int>& signal) {
        selected(signal.get());
        onChange([&signal](int value) { signal.set(value); });
        return *this;
    }
    DropdownBuilder& placeholder(const std::string& value) { placeholder_ = value; return *this; }
    DropdownBuilder& open(bool value = true) { open_ = value; return *this; }
    // 弹层向上展开（字段靠近窗口/容器底部时用，避免菜单被窗口边缘裁掉）；
    // 默认 false 保持向下展开，向后兼容
    DropdownBuilder& openUp(bool value = true) { openUp_ = value; return *this; }
    DropdownBuilder& bindOpen(eui::Signal<bool>& signal) {
        open(signal.get());
        onOpenChange([&signal](bool value) { signal.set(value); });
        return *this;
    }
    DropdownBuilder& itemHeight(float value) { itemHeight_ = std::max(1.0f, value); return *this; }
    DropdownBuilder& style(const DropdownStyle& value) { style_ = value; return *this; }
    DropdownBuilder& theme(const theme::ThemeColorTokens& tokens) {
        style_ = DropdownStyle(tokens);
        metrics_ = tokens.metrics;
        return *this;
    }
    DropdownBuilder& transition(const core::Transition& value) { transition_ = value; return *this; }
    DropdownBuilder& zIndex(int value) { zIndex_ = value; return *this; }
    DropdownBuilder& onChange(std::function<void(int)> callback) { onChange_ = std::move(callback); return *this; }
    DropdownBuilder& onOpenChange(std::function<void(bool)> callback) { onOpenChange_ = std::move(callback); return *this; }

    void build() {
        const int count = static_cast<int>(items_.size());
        const int selected = count > 0 ? std::clamp(selected_, 0, count - 1) : -1;
        const std::string label = selected >= 0 ? items_[selected] : placeholder_;
        const float height = height_ >= 0.0f ? height_ : metrics_.control.control;
        const float itemHeight = itemHeight_ > 0.0f
            ? std::max(metrics_.control.switchHeight, itemHeight_)
            : metrics_.control.menuItem;
        const float popupGap = metrics_.spacing.compact;
        const float popupPadding = metrics_.spacing.small;
        const float popupHeight = itemHeight * static_cast<float>(std::max(1, count)) + popupPadding * 2.0f;
        const float rootHeight = height + popupGap + popupHeight;
        const float visible = open_ ? 1.0f : 0.0f;
        // 向下：收起时从上方 -6 滑入；向上：镜像，从下方 +6 滑入、以底边为缩放锚
        const float popupOffsetY = open_ ? 0.0f : (openUp_ ? 6.0f : -6.0f);
        const float popupScale = open_ ? 1.0f : 0.96f;
        const std::function<void(int)> onChange = onChange_;
        const std::function<void(bool)> onOpenChange = onOpenChange_;

        ui_.stack(id_)
            .size(width_, rootHeight)
            .zIndex(zIndex_)
            .content([&] {
                ui_.rect(id_ + ".field")
                    .size(width_, height)
                    .states(style_.field, style_.fieldHover, style_.fieldPressed)
                    .radius(style_.radius)
                    .border(metrics_.spacing.hairline, style_.border)
                    .transition(transition_)
                    .onClick([onOpenChange, open = open_] {
                        if (onOpenChange) {
                            onOpenChange(!open);
                        }
                    })
                    .build();

                ui_.text(id_ + ".label")
                    .x(metrics_.spacing.content + metrics_.spacing.micro)
                    .size(std::max(0.0f, width_ - metrics_.spacing.overlay), height)
                    .text(label)
                    .fontSize(metrics_.typography.body)
                    .lineHeight(metrics_.typography.body + metrics_.typography.lineGap)
                    .color(selected >= 0 ? style_.text : style_.mutedText)
                    .verticalAlign(core::VerticalAlign::Center)
                    .build();

                ui_.text(id_ + ".chevron")
                    .x(std::max(0.0f, width_ - metrics_.control.menuItem))
                    .size(metrics_.spacing.large, height)
                    .icon(open_ ? 0xF077 : 0xF078)
                    .fontSize(metrics_.typography.hint)
                    .lineHeight(metrics_.typography.hint + metrics_.typography.lineGapRelaxed)
                    .color(style_.accent)
                    .horizontalAlign(core::HorizontalAlign::Center)
                    .verticalAlign(core::VerticalAlign::Center)
                    .transition(transition_)
                    .animate(core::AnimProperty::TextColor)
                    .build();

                ui_.stack(id_ + ".popup")
                    .y(openUp_ ? -(popupGap + popupHeight) : height + popupGap)
                    .size(width_, popupHeight)
                    // 弹层展开档位：open 时把 popup 子树抬到常规内容档之上
                    // （sidebar 60 / card_slider 信息层 250 / 其它 dropdown
                    // 字段 20 等；与 card_slider 输入层 900 等值，等值时退化
                    // 为 DOM 序），仍在 dialog(1000)/colorpicker、datepicker、
                    // timepicker(1000)/contextmenu(1050)/toast(1100) 之下——
                    // 模态层照常盖住展开的下拉。等值 z 碰撞背景：两个
                    // 纵向相邻 dropdown 的子树上界同为默认 zIndex_=20，兄弟
                    // 稳定排序（core/dsl.h rebuildOrderedChildren）保持 DOM
                    // 序 → 后绘的下方字段盖住上方展开弹层（DevDesk settings
                    // 桌宠卡「皮肤」弹层被新增「大小」行遮挡，2026-09-30）。
                    // subtreeMaxZIndex 逃逸只解决「高 z 盖低 z」，等值档位
                    // 必须由 open 态升档；关闭态归 0，子树上界回到字段档 20，
                    // DOM 序与既有行为逐位一致。已知边界：两个 dropdown 同时
                    // open 时同为 900，仍按 DOM 序（宿主侧普遍单开互斥）。
                    .zIndex(open_ ? kPopupOpenZIndex : 0)
                    .opacity(visible)
                    .translateY(popupOffsetY)
                    .scale(popupScale)
                    .transformOrigin(0.5f, openUp_ ? 1.0f : 0.0f)
                    .transition(transition_)
                    .animate(core::AnimProperty::Opacity | core::AnimProperty::Transform)
                    .content([&] {
                        ui_.rect(id_ + ".popup.bg")
                            .size(width_, popupHeight)
                            .color(style_.popup)
                            .radius(style_.radius)
                            .border(metrics_.spacing.hairline, style_.border)
                            .shadow(style_.shadow)
                            .build();

                        ui_.rect(id_ + ".popup.hit")
                            .size(width_, popupHeight)
                            .states(theme::color(0.0f, 0.0f, 0.0f, 0.0f),
                                    theme::color(0.0f, 0.0f, 0.0f, 0.0f),
                                    theme::color(0.0f, 0.0f, 0.0f, 0.0f))
                            .disabled(!open_)
                            .blockPointer()
                            .build();

                        for (int index = 0; index < count; ++index) {
                            const bool active = index == selected;
                            const float itemY = popupPadding + static_cast<float>(index) * itemHeight;
                            ui_.rect(id_ + ".item." + std::to_string(index))
                                .x(popupPadding)
                                .y(itemY)
                                .size(std::max(0.0f, width_ - popupPadding * 2.0f), itemHeight)
                                .states(theme::color(0.0f, 0.0f, 0.0f, 0.0f), style_.optionHover, style_.optionPressed)
                                .radius(std::max(metrics_.radius.tiny, style_.radius - metrics_.radius.tiny))
                                .instantStates()
                                .disabled(!open_)
                                .onClick([onChange, onOpenChange, index] {
                                    if (onChange) {
                                        onChange(index);
                                    }
                                    if (onOpenChange) {
                                        onOpenChange(false);
                                    }
                                })
                                .build();

                            ui_.text(id_ + ".item.label." + std::to_string(index))
                                .x(popupPadding + metrics_.spacing.content)
                                .y(itemY + std::max(0.0f, (itemHeight - metrics_.typography.option -
                                                           metrics_.typography.lineGapTight) * 0.5f))
                                .size(std::max(0.0f, width_ - popupPadding * 2.0f - metrics_.spacing.panel),
                                      metrics_.spacing.large)
                                .text(items_[index])
                                .fontSize(metrics_.typography.option)
                                .lineHeight(metrics_.typography.option + metrics_.typography.lineGapTight)
                                .color(active ? style_.accent : style_.text)
                                .transition(transition_)
                                .animate(core::AnimProperty::TextColor)
                                .build();
                        }
                    })
                    .build();
            })
            .build();
    }

private:
    core::dsl::Ui& ui_;
    std::string id_;
    std::vector<std::string> items_;
    DropdownStyle style_;
    theme::ThemeMetricTokens metrics_;
    core::Transition transition_ = core::Transition::make(0.16f, core::Ease::OutCubic);
    std::function<void(int)> onChange_;
    std::function<void(bool)> onOpenChange_;
    std::string placeholder_ = "Select";
    int selected_ = -1;
    bool open_ = false;
    bool openUp_ = false;
    float width_ = 260.0f;
    float height_ = -1.0f;
    float itemHeight_ = 0.0f;
    int zIndex_ = 20;

    // 弹层展开态的 z 档位（挂在 id+".popup" 子树，经 subtreeMaxZIndex 把整个
    // dropdown 抬到后续兄弟之上）。900：高于常规内容档（sidebar 60、
    // card_slider 信息层 250、其它 dropdown 字段 20 等），与 card_slider
    // 输入层（components/workshop/card_slider.h 的 mouseArea 900）为等值档、
    // 等值时退化为 DOM 序；低于 dialog/colorpicker/datepicker/timepicker
    // 1000 / contextmenu 1050 / toast 1100，模态层不被穿。
    // 见 build() 内 popup.stack 注释。
    static constexpr int kPopupOpenZIndex = 900;
};

inline DropdownBuilder dropdown(core::dsl::Ui& ui, const std::string& id) {
    return DropdownBuilder(ui, id);
}

} // namespace components
