#pragma once

#include "components/theme.h"
#include "core/dsl.h"

#include <algorithm>
#include <string>
#include <utility>

namespace components {

struct ScrollStyle {
    ScrollStyle() : ScrollStyle(theme::dark()) {}

    explicit ScrollStyle(const theme::ThemeColorTokens& tokens) {
        // UAT6 M 批：轨道近不可见，thumb 品牌绿浅档半透、hover/拖动加深
        track = theme::withOpacity(tokens.surfaceHover, 0.08f);  // 近不可见
        thumb = theme::withOpacity(tokens.primary, tokens.dark ? 0.32f : 0.26f);  // 品牌绿半透
        thumbHover = theme::withOpacity(tokens.primary, tokens.dark ? 0.44f : 0.38f);
        thumbPressed = theme::withOpacity(theme::buttonPressed(tokens, tokens.primary), 0.68f);
        radius = tokens.metrics.radius.full;
    }

    core::Color track;
    core::Color thumb;
    core::Color thumbHover;
    core::Color thumbPressed;
    float radius = 999.0f;
};

class ScrollBuilder {
public:
    ScrollBuilder(core::dsl::Ui& ui, std::string id)
        : ui_(ui), id_(std::move(id)) {}

    ScrollBuilder& x(float value) { x_ = value; hasX_ = true; return *this; }
    ScrollBuilder& y(float value) { y_ = value; hasY_ = true; return *this; }
    ScrollBuilder& position(float xValue, float yValue) {
        x_ = xValue;
        y_ = yValue;
        hasX_ = true;
        hasY_ = true;
        return *this;
    }
    ScrollBuilder& size(float width, float height) { width_ = width; height_ = height; return *this; }
    ScrollBuilder& scrollStateId(const std::string& id) { stateId_ = id; return *this; }
    ScrollBuilder& offset(float value) { offset_ = std::max(0.0f, value); return *this; }
    ScrollBuilder& viewport(float value) { viewport_ = std::max(0.0f, value); return *this; }
    ScrollBuilder& content(float value) { content_ = std::max(0.0f, value); return *this; }
    ScrollBuilder& step(float value) { step_ = std::max(1.0f, value); return *this; }
    // 横向滚动条（DevDesk git 管理器 F-07⑥，2026-11-13）：thumb 沿宽度布局，
    // 拖拽映射水平位移；scrollState 偏移语义不变（调用方持有横向 offset）
    ScrollBuilder& horizontal(bool value = true) { horizontal_ = value; return *this; }
    ScrollBuilder& onChange(std::function<void(float)> callback) {
        onChange_ = std::move(callback);
        return *this;
    }
    ScrollBuilder& zIndex(int value) { zIndex_ = value; return *this; }
    ScrollBuilder& style(const ScrollStyle& value) { style_ = value; return *this; }
    ScrollBuilder& theme(const theme::ThemeColorTokens& tokens) {
        style_ = ScrollStyle(tokens);
        metrics_ = tokens.metrics;
        return *this;
    }
    ScrollBuilder& transition(const core::Transition& value) { transition_ = value; return *this; }
    ScrollBuilder& transition(float duration, core::Ease ease = core::Ease::OutCubic) {
        transition_ = core::Transition::make(duration, ease);
        return *this;
    }

    void build() {
        const float width = width_ >= 0.0f ? width_ : metrics_.control.scrollbar;
        const float maxOffset = std::max(0.0f, content_ - viewport_);
        const bool scrollable = maxOffset > 0.0f && viewport_ > 0.0f && content_ > 0.0f;
        // thumb 主轴尺寸：纵向沿高度 / 横向沿宽度（同一 viewport/content 比例）
        const float thumbMain = scrollable
            ? std::clamp((horizontal_ ? width : height_) * (viewport_ / content_),
                         std::min(horizontal_ ? width : height_, metrics_.control.switchHeight),
                         horizontal_ ? width : height_)
            : (horizontal_ ? width : height_);
        const float travel = (horizontal_ ? width : height_) - thumbMain;
        const float currentOffset = std::clamp(offset_, 0.0f, maxOffset);
        const float scrollStep = step_ > 0.0f ? step_ : metrics_.control.control;
        const std::string runtimeStateId = stateId_.empty() ? id_ : stateId_;

        auto root = ui_.stack(id_)
            .size(width, height_)
            .zIndex(zIndex_)
            .scrollState(runtimeStateId, currentOffset, maxOffset, scrollStep);
        if (onChange_) {
            root.onScrollOffsetChanged(onChange_);
        }
        if (horizontal_) {
            root.scrollHorizontal();
        }
        if (hasX_) {
            root.x(x_);
        }
        if (hasY_) {
            root.y(y_);
        }
        root.content([&] {
                ui_.rect(id_ + ".track")
                    .size(width, height_)
                    .color(style_.track)
                    .radius(style_.radius)
                    .scrollState(runtimeStateId, currentOffset, maxOffset, scrollStep)
                    .build();

                auto thumb = ui_.rect(id_ + ".thumb")
                    .states(style_.thumb, style_.thumbHover, style_.thumbPressed)
                    .radius(style_.radius)
                    .cursor(core::CursorShape::Hand)
                    .transition(transition_)
                    .animate(core::AnimProperty::Color)
                    .scrollDragFrom(runtimeStateId, travel)
                    .scrollThumbFrom(runtimeStateId, travel)
                    .transformedHitTest();
                if (horizontal_) {
                    thumb.scrollHorizontal().size(thumbMain, height_);
                } else {
                    thumb.size(width, thumbMain);
                }
                thumb.build();
            })
            .build();
    }

private:
    core::dsl::Ui& ui_;
    std::string id_;
    std::string stateId_;
    ScrollStyle style_;
    theme::ThemeMetricTokens metrics_;
    core::Transition transition_ = core::Transition::make(0.12f, core::Ease::OutCubic);
    std::function<void(float)> onChange_;
    float width_ = -1.0f;
    float height_ = 180.0f;
    float viewport_ = 180.0f;
    float content_ = 180.0f;
    float offset_ = 0.0f;
    float step_ = 0.0f;
    bool horizontal_ = false;
    float x_ = 0.0f;
    float y_ = 0.0f;
    bool hasX_ = false;
    bool hasY_ = false;
    int zIndex_ = 0;
};

inline ScrollBuilder scroll(core::dsl::Ui& ui, const std::string& id) {
    return ScrollBuilder(ui, id);
}

} // namespace components
