#pragma once

#include "components/theme.h"
#include "core/dsl.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

namespace components {

// -- S0#3 按钮变体枚举 ----------------------------------------------------------
enum class ButtonVariant { Filled, Outline, Ghost, Danger };

struct ButtonStyle {
    ButtonStyle() : ButtonStyle(theme::dark()) {}

    explicit ButtonStyle(const theme::ThemeColorTokens& tokens, bool primary = true) {
        const core::Color base = primary ? tokens.primary : tokens.surface;
        normal = base;
        hover = theme::buttonHover(tokens, base);
        pressed = theme::buttonPressed(tokens, base);
        text = primary || tokens.dark ? core::Color{0.94f, 0.97f, 1.0f, 1.0f} : tokens.text;
        icon = text;
        border = theme::buttonBorder(tokens, primary);
        shadow = {};  // flat — no shadow (WeChat flat design)
        radius = tokens.metrics.radius.overlay;
    }

    // S0#3: 基于变体构造 ButtonStyle
    explicit ButtonStyle(const theme::ThemeColorTokens& tokens, ButtonVariant variant) {
        shadow = {};
        radius = tokens.metrics.radius.control;
        switch (variant) {
        case ButtonVariant::Filled:
            *this = ButtonStyle(tokens, true);
            break;
        case ButtonVariant::Outline:
            // W2 合约: primarySoftBg/primaryActive 需由 DevDesk 主题工厂填充;
            // 未填时回退到 12%/22% alpha 混合近似值（视觉偏离 S2 但不崩）。
            normal = tokens.surface;
            hover = tokens.primarySoftBg.a > 0.0f
                        ? tokens.primarySoftBg
                        : theme::withAlpha(tokens.primary, 0.12f);
            pressed = tokens.primaryActive.a > 0.0f
                          ? theme::withAlpha(tokens.primaryActive, 0.15f)
                          : theme::withAlpha(tokens.primary, 0.22f);
            text = tokens.primary;
            icon = text;
            border = {1.0f, tokens.primary};
            break;
        case ButtonVariant::Ghost:
            normal = core::Color{0.0f, 0.0f, 0.0f, 0.0f};
            hover = tokens.primarySoftBg.a > 0.0f
                        ? tokens.primarySoftBg
                        : theme::withAlpha(tokens.primary, 0.12f);
            pressed = theme::withAlpha(tokens.primary, 0.22f);
            text = tokens.primary;
            icon = text;
            border = {};
            break;
        case ButtonVariant::Danger: {
            const core::Color err = tokens.error;
            normal = err.a > 0.0f ? theme::withAlpha(err, 0.20f)  // S2.1: #FEE2E2 = error 20% alpha
                                  : core::Color{0.996f, 0.886f, 0.886f, 1.0f};
            hover = err.a > 0.0f ? err : core::Color{0.937f, 0.267f, 0.267f, 1.0f};
            pressed = core::Color{0.863f, 0.149f, 0.149f, 1.0f};  // S2.1: #DC2626
            text = err.a > 0.0f ? err : core::Color{0.937f, 0.267f, 0.267f, 1.0f};
            textHover = {1.0f, 1.0f, 1.0f, 1.0f};       // S2.1 Danger hover: #FFFFFF
            textPressed = {1.0f, 1.0f, 1.0f, 1.0f};     // S2.1 Danger pressed: #FFFFFF
            icon = text;
            border = {};
            break;
        }
        }
    }

    core::Color normal;
    core::Color hover;
    core::Color pressed;
    core::Color text;
    core::Color textHover = {};    // 非零时 hover 状态用此色替代 text（旧代码设 {} = 无效果）
    core::Color textPressed = {};  // 非零时 pressed 状态用此色替代 textHover（无此字段则沿用 textHover）
    core::Color icon;
    core::Border border;
    core::Shadow shadow;
    float radius = 16.0f;
    float opacity = 1.0f;
    float pressScale = 0.965f;
};

class ButtonBuilder {
public:
    ButtonBuilder(core::dsl::Ui& ui, std::string id)
        : ui_(ui), id_(std::move(id)) {}

    ButtonBuilder& x(float value) { x_ = value; hasX_ = true; return *this; }
    ButtonBuilder& y(float value) { y_ = value; hasY_ = true; return *this; }
    ButtonBuilder& position(float xValue, float yValue) { return x(xValue).y(yValue); }
    ButtonBuilder& size(float width, float height) { width_ = width; height_ = height; return *this; }
    ButtonBuilder& scale(float value) { scale_ = value; return *this; }
    ButtonBuilder& text(const std::string& value) { text_ = value; return *this; }
    ButtonBuilder& icon(unsigned int codepoint) { icon_ = core::dsl::utf8(codepoint); return *this; }
    ButtonBuilder& icon(const std::string& value) { icon_ = value; return *this; }
    ButtonBuilder& fontSize(float value) { fontSize_ = value; return *this; }
    ButtonBuilder& iconSize(float value) { iconSize_ = value; return *this; }
    ButtonBuilder& textColor(const core::Color& value) { style_.text = value; return *this; }
    ButtonBuilder& iconColor(const core::Color& value) { style_.icon = value; return *this; }
    ButtonBuilder& style(const ButtonStyle& value) { style_ = value; return *this; }
    ButtonBuilder& theme(const theme::ThemeColorTokens& tokens, bool primary = true) {
        if (hasExplicitVariant_) {
            style_ = ButtonStyle(tokens, defaultVariant_);
        } else {
            style_ = ButtonStyle(tokens, primary);
        }
        metrics_ = tokens.metrics;
        return *this;
    }
    ButtonBuilder& theme(const theme::ThemeColorTokens& tokens, ButtonVariant variant) {
        style_ = ButtonStyle(tokens, variant);
        metrics_ = tokens.metrics;
        return *this;
    }
    /** @brief 预设变体——当后续 .theme(tokens) 不带 variant 参数时生效；显式传 variant 参数则覆盖此值。 */
    ButtonBuilder& variant(ButtonVariant v) {
        defaultVariant_ = v;
        hasExplicitVariant_ = true;
        return *this;
    }
    ButtonBuilder& radius(float value) { style_.radius = value; return *this; }
    ButtonBuilder& opacity(float value) { style_.opacity = std::clamp(value, 0.0f, 1.0f); return *this; }
    ButtonBuilder& disabled(bool value = true) { disabled_ = value; return *this; }
    /** @brief disabled 时的文字颜色；不传则自动用现有 text 色的 40% 透明度置灰。 */
    ButtonBuilder& disabledTextColor(const core::Color& value) {
        disabledTextColor_ = value;
        hasDisabledTextColor_ = true;
        return *this;
    }
    ButtonBuilder& preserveFocusOnPress(bool value = true) { preserveFocusOnPress_ = value; return *this; }
    ButtonBuilder& translate(float x, float y) { translateX_ = x; translateY_ = y; return *this; }
    ButtonBuilder& translateX(float value) { translateX_ = value; return *this; }
    ButtonBuilder& translateY(float value) { translateY_ = value; return *this; }
    ButtonBuilder& pressScale(float value) { style_.pressScale = std::clamp(value, 0.80f, 1.0f); return *this; }
    ButtonBuilder& border(float width, const core::Color& color) { style_.border = {width, color}; return *this; }
    ButtonBuilder& shadow(float blur, float offsetX, float offsetY, const core::Color& color) {
        style_.shadow = {true, {offsetX, offsetY}, blur, 0.0f, color};
        return *this;
    }
    ButtonBuilder& colors(const core::Color& normal, const core::Color& hover, const core::Color& pressed) {
        style_.normal = normal;
        style_.hover = hover;
        style_.pressed = pressed;
        return *this;
    }
    ButtonBuilder& transition(const core::Transition& value) { transition_ = value; return *this; }
    ButtonBuilder& transition(float duration, core::Ease ease = core::Ease::OutCubic) {
        transition_ = core::Transition::make(duration, ease);
        return *this;
    }
    ButtonBuilder& onClick(std::function<void()> callback) { onClick_ = std::move(callback); return *this; }
    ButtonBuilder& onPress(std::function<void()> callback) { onPress_ = std::move(callback); return *this; }
    ButtonBuilder& onRelease(std::function<void()> callback) { onRelease_ = std::move(callback); return *this; }
    ButtonBuilder& onFrame(std::function<void(float)> callback) { onFrame_ = std::move(callback); return *this; }
    ButtonBuilder& onContextMenu(std::function<void(const core::PointerEvent&, const core::Rect&)> callback) {
        onContextMenu_ = std::move(callback);
        return *this;
    }

    void build() {
        const float w = width_ * scale_;
        const float h = height_ * scale_;
        const float font = fontSize_ > 0.0f ? fontSize_ * scale_ : h * 0.46f;
        const float iconFont = iconSize_ > 0.0f ? iconSize_ * scale_ : font * 0.92f;
        const bool hasIcon = !icon_.empty();
        const bool hasText = !text_.empty();
        const float iconWidth = hasIcon ? iconFont * 1.15f : 0.0f;
        const float gap = hasIcon && hasText ? std::max(metrics_.spacing.small * scale_, h * 0.12f) : 0.0f;
        const float labelWidth = hasIcon && hasText
            ? std::max(0.0f, w - iconWidth - gap - metrics_.spacing.section * 2.0f * scale_)
            : w;
        core::Border border = style_.border;
        border.width *= scale_;

        core::Shadow shadow = style_.shadow;
        shadow.offset.x *= scale_;
        shadow.offset.y *= scale_;
        shadow.blur *= scale_;
        shadow.spread *= scale_;
        core::Color textColor = style_.text;
        core::Color iconColor = style_.icon;
        if (disabled_) {
            // disabled 自动置灰：默认现有色的 40% 透明度，disabledTextColor 可覆盖。
            textColor = hasDisabledTextColor_ ? disabledTextColor_
                                              : theme::withOpacity(style_.text, 0.40f);
            iconColor = theme::withOpacity(style_.icon, 0.40f);
        }
        textColor.a *= style_.opacity;
        iconColor.a *= style_.opacity;
        const std::function<void()> onPress = onPress_;
        const std::function<void()> onRelease = onRelease_;

        auto root = ui_.stack(id_)
            .size(w, h)
            .visualStateFrom(id_ + ".bg", style_.pressScale);
        if (hasX_) {
            root.x(x_);
        }
        if (hasY_) {
            root.y(y_);
        }
        root.content([&] {
                auto bg = ui_.rect(id_ + ".bg")
                    .size(w, h)
                    .states(style_.normal, style_.hover, style_.pressed)
                    .radius(style_.radius * scale_)
                    .opacity(style_.opacity)
                    .border(border)
                    .shadow(shadow)
                    .translate(translateX_, translateY_)
                    .transition(transition_)
                    .disabled(disabled_)
                    .preserveFocusOnPress(preserveFocusOnPress_)
                    .onClick(onClick_)
                    .onContextMenu(onContextMenu_);
                if (onPress) {
                    bg.onPress([onPress](const core::PointerEvent&, const core::Rect&) { onPress(); });
                }
                if (onRelease) {
                    bg.onRelease([onRelease](const core::PointerEvent&, const core::Rect&) { onRelease(); });
                }
                if (onFrame_) {
                    bg.onFrame(onFrame_);
                }
                bg.build();

                ui_.row(id_ + ".content")
                    .size(w, h)
                    .gap(gap)
                    .justifyContent(core::Align::CENTER)
                    .alignItems(core::Align::CENTER)
                    .content([&] {
                        if (hasIcon) {
                            ui_.text(id_ + ".icon")
                                .size(iconWidth, h)
                                .icon(icon_)
                                .fontSize(iconFont)
                                .lineHeight(iconFont)
                                .color(iconColor)
                                .horizontalAlign(core::HorizontalAlign::Center)
                                .verticalAlign(core::VerticalAlign::Center)
                                .transition(transition_)
                                .build();
                        }

                        if (hasText) {
                            core::Color textHoverClr = style_.textHover;
                            core::Color textPressedClr = style_.textPressed;
                            if (textHoverClr.a > 0.0f || textPressedClr.a > 0.0f) {
                                // 逐态文字: 正常 / hover 双 text + hoverOpacityFrom 切换
                                core::Color hoverClr = textHoverClr.a > 0.0f ? textHoverClr : textColor;
                                auto textStack = ui_.stack(id_ + ".text.states")
                                    .size(labelWidth, h);
                                textStack.content([&] {
                                    // 正常态文字 (hover 时渐隐)
                                    ui_.text(id_ + ".text.normal")
                                        .size(labelWidth, h)
                                        .text(text_)
                                        .fontSize(font)
                                        .lineHeight(font)
                                        .color(textColor)
                                        .horizontalAlign(core::HorizontalAlign::Center)
                                        .verticalAlign(core::VerticalAlign::Center)
                                        .transition(transition_)
                                        .hoverOpacityFrom(id_ + ".bg", 1.0f, 0.0f)
                                        .build();
                                    // hover/pressed 态文字（正常态时不可见，hover 时渐显）
                                    ui_.text(id_ + ".text.hover")
                                        .size(labelWidth, h)
                                        .text(text_)
                                        .fontSize(font)
                                        .lineHeight(font)
                                        .color(hoverClr)
                                        .horizontalAlign(core::HorizontalAlign::Center)
                                        .verticalAlign(core::VerticalAlign::Center)
                                        .transition(transition_)
                                        .hoverOpacityFrom(id_ + ".bg", 0.0f, 1.0f)
                                        .build();
                                });
                                textStack.build();
                            } else {
                                ui_.text(id_ + ".text")
                                    .size(labelWidth, h)
                                    .text(text_)
                                    .fontSize(font)
                                    .lineHeight(font)
                                    .color(textColor)
                                    .horizontalAlign(core::HorizontalAlign::Center)
                                    .verticalAlign(core::VerticalAlign::Center)
                                    .transition(transition_)
                                    .build();
                            }
                        }
                    })
                    .build();
            })
            .build();
    }

private:
    core::dsl::Ui& ui_;
    std::string id_;
    std::string text_ = "Button";
    std::string icon_;
    ButtonStyle style_;
    theme::ThemeMetricTokens metrics_;
    core::Transition transition_;
    std::function<void()> onClick_;
    std::function<void()> onPress_;
    std::function<void()> onRelease_;
    std::function<void(float)> onFrame_;
    std::function<void(const core::PointerEvent&, const core::Rect&)> onContextMenu_;
    float width_ = 200.0f;
    float height_ = 54.0f;
    float scale_ = 1.0f;
    float fontSize_ = 0.0f;
    float iconSize_ = 0.0f;
    ButtonVariant defaultVariant_ = ButtonVariant::Filled;
    bool hasExplicitVariant_ = false;
    float translateX_ = 0.0f;
    float translateY_ = 0.0f;
    float x_ = 0.0f;
    float y_ = 0.0f;
    bool disabled_ = false;
    bool hasDisabledTextColor_ = false;
    core::Color disabledTextColor_{1.0f, 1.0f, 1.0f, 1.0f};
    bool preserveFocusOnPress_ = false;
    bool hasX_ = false;
    bool hasY_ = false;
};

inline ButtonBuilder button(core::dsl::Ui& ui, const std::string& id) {
    return ButtonBuilder(ui, id);
}

inline ButtonBuilder button(core::dsl::Ui& ui, const std::string& id, ButtonVariant variant) {
    return ButtonBuilder(ui, id).variant(variant);
}

} // namespace components
