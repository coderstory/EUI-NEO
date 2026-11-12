#pragma once

#include "components/scroll.h"
#include "core/dsl.h"
#include "core/platform/platform.h"
#include "eui/signal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace components {

class VirtualListBuilder {
public:
    using RowCompose = std::function<void(core::dsl::Ui&, const std::string&, std::int64_t, float, float)>;

    VirtualListBuilder(core::dsl::Ui& ui, std::string id)
        : ui_(ui), id_(std::move(id)) {}

    VirtualListBuilder& x(float value) { x_ = value; hasX_ = true; return *this; }
    VirtualListBuilder& y(float value) { y_ = value; hasY_ = true; return *this; }
    VirtualListBuilder& position(float xValue, float yValue) {
        x_ = xValue;
        y_ = yValue;
        hasX_ = true;
        hasY_ = true;
        return *this;
    }
    VirtualListBuilder& size(float width, float height) { width_ = width; height_ = height; return *this; }
    VirtualListBuilder& itemCount(std::int64_t value) { itemCount_ = std::max<std::int64_t>(0, value); return *this; }
    VirtualListBuilder& rowHeight(float value) { rowHeight_ = std::max(1.0f, value); return *this; }
    VirtualListBuilder& offset(float value) { offset_ = std::max(0.0f, value); return *this; }
    VirtualListBuilder& bind(eui::Signal<float>& signal) {
        offset(signal.get());
        onChange([&signal](float value) { signal.set(value); });
        return *this;
    }
    VirtualListBuilder& step(float value) { step_ = std::max(1.0f, value); return *this; }
    VirtualListBuilder& overscanViewports(float value) { overscanViewports_ = std::max(0.0f, value); return *this; }
    VirtualListBuilder& scrollbarWidth(float value) { scrollbarWidth_ = std::max(0.0f, value); return *this; }
    VirtualListBuilder& scrollbarGap(float value) { scrollbarGap_ = std::max(0.0f, value); return *this; }
    // ---- 横向滚动（DevDesk git 管理器 F-07⑥ 真横滚，2026-11-13）----
    // contentWidth：每行的虚拟内容宽。> 行区宽时列表底部出横向滚动条；
    // 行闭包收到的 contentWidth 参数不变（行区宽），行内自行按消费方持有的
    // 横向偏移（hOffset/onHChange）平移超宽内容——列表只负责裁剪（根 clip）
    // 与滚动条状态管线。0（默认）=无横向滚动，行为与既有完全一致。
    VirtualListBuilder& contentWidth(float value) { contentWidth_ = std::max(0.0f, value); return *this; }
    VirtualListBuilder& hOffset(float value) { hOffset_ = std::max(0.0f, value); return *this; }
    VirtualListBuilder& onHChange(std::function<void(float)> callback) {
        onHChange_ = std::move(callback);
        return *this;
    }
    VirtualListBuilder& zIndex(int value) { zIndex_ = value; return *this; }
    VirtualListBuilder& style(const ScrollStyle& value) { scrollStyle_ = value; return *this; }
    VirtualListBuilder& theme(const theme::ThemeColorTokens& tokens) {
        scrollStyle_ = ScrollStyle(tokens);
        metrics_ = tokens.metrics;
        tokens_ = tokens;
        return *this;
    }
    VirtualListBuilder& transition(const core::Transition& value) { transition_ = value; return *this; }
    VirtualListBuilder& transition(float duration, core::Ease ease = core::Ease::OutCubic) {
        transition_ = core::Transition::make(duration, ease);
        return *this;
    }
    VirtualListBuilder& onChange(std::function<void(float)> callback) {
        onChange_ = std::move(callback);
        return *this;
    }
    VirtualListBuilder& row(RowCompose compose) {
        row_ = std::move(compose);
        return *this;
    }

    void build() {
        const float viewportWidth = std::max(0.0f, width_);
        const float viewportHeight = std::max(0.0f, height_);
        const float rowHeight = std::max(1.0f, rowHeight_);
        const double totalHeightValue = static_cast<double>(itemCount_) * static_cast<double>(rowHeight);
        const float totalHeight = static_cast<float>(totalHeightValue);
        const float maxOffset = std::max(0.0f, totalHeight - viewportHeight);
        const float currentOffset = std::clamp(offset_, 0.0f, maxOffset);
        const bool scrollable = maxOffset > 0.0f;
        const float scrollbarWidth = scrollbarWidth_ >= 0.0f ? scrollbarWidth_ : metrics_.control.scrollbar;
        const float scrollbarGap = scrollbarGap_ >= 0.0f ? scrollbarGap_ : metrics_.spacing.section;
        const float scrollWidth = scrollable ? scrollbarWidth : 0.0f;
        const float scrollGap = scrollable ? scrollbarGap : 0.0f;
        const float contentWidth = std::max(0.0f, viewportWidth - scrollWidth - scrollGap);
        const std::function<void(float)> onChange = onChange_;
        const float scrollStep = step_ > 0.0f ? step_ : metrics_.spacing.overlay;

        const float overscanPixels = viewportHeight * overscanViewports_;
        const double firstPixel = std::max(0.0, static_cast<double>(currentOffset) - static_cast<double>(overscanPixels));
        const double lastPixel = std::min(totalHeightValue,
                                          static_cast<double>(currentOffset) +
                                              static_cast<double>(viewportHeight) +
                                              static_cast<double>(overscanPixels));
        const std::int64_t firstIndex = itemCount_ > 0
            ? std::clamp<std::int64_t>(
                  static_cast<std::int64_t>(std::floor(firstPixel / static_cast<double>(rowHeight))),
                  0,
                  itemCount_ - 1)
            : 0;
        const std::int64_t lastIndex = itemCount_ > 0
            ? std::clamp<std::int64_t>(
                  static_cast<std::int64_t>(std::ceil(lastPixel / static_cast<double>(rowHeight))) + 1,
                  firstIndex,
                  itemCount_)
            : 0;
        const std::int64_t itemCount = itemCount_;
        // 横向：行区宽 = 视口宽 -（纵向可滚时滚动条占位）；超宽内容出横向条
        const float rowAreaWidth = contentWidth;
        const float hMaxOffset = std::max(0.0f, contentWidth_ - rowAreaWidth);
        const float hCurrent = std::clamp(hOffset_, 0.0f, hMaxOffset);
        const std::function<void(float)> onHChange = onHChange_;
        const auto firstIndexForOffset = [rowHeight, overscanPixels, itemCount](float offset) {
            const double totalHeight = static_cast<double>(itemCount) * static_cast<double>(rowHeight);
            const double firstPixel = std::max(0.0, static_cast<double>(offset) - static_cast<double>(overscanPixels));
            if (itemCount <= 0 || totalHeight <= 0.0) {
                return static_cast<std::int64_t>(0);
            }
            return std::clamp<std::int64_t>(
                static_cast<std::int64_t>(std::floor(firstPixel / static_cast<double>(rowHeight))),
                0,
                itemCount - 1);
        };
        auto root = ui_.stack(id_)
            .size(viewportWidth, viewportHeight)
            .zIndex(zIndex_)
            .clip()
            .scrollState(id_, currentOffset, maxOffset, scrollStep)
            .onScrollOffsetChanged([onChange, firstIndex, firstIndexForOffset](float value) {
                if (onChange) {
                    onChange(value);
                }
                // Runtime can move the content window every frame. Recompose
                // only when the visible slot range changes.
                if (firstIndexForOffset(value) != firstIndex) {
                    core::platform::requestUiUpdate();
                }
            });
        if (hasX_) {
            root.x(x_);
        }
        if (hasY_) {
            root.y(y_);
        }
        root.content([&] {
                ui_.stack(id_ + ".window")
                    .size(contentWidth, viewportHeight)
                    .scrollContentFrom(id_)
                    .dirtyKey(id_ + ".virtual")
                    .content([&] {
                        if (!row_) {
                            return;
                        }
                        std::int64_t slot = 0;
                        for (std::int64_t index = firstIndex; index < lastIndex; ++index) {
                            const std::string rowId = id_ + ".slot." + std::to_string(slot);
                            const double absoluteY = static_cast<double>(index) * static_cast<double>(rowHeight);
                            ui_.stack(rowId)
                                .y(static_cast<float>(absoluteY))
                                .size(contentWidth, rowHeight)
                                .content([&] {
                                    row_(ui_, rowId, index, contentWidth, rowHeight);
                                })
                                .build();
                            ++slot;
                        }
                    })
                    .build();

                if (scrollable) {
                    components::scroll(ui_, id_ + ".scroll")
                        .theme(tokens_)
                        .style(scrollStyle_)
                        .scrollStateId(id_)
                        .x(std::max(0.0f, viewportWidth - scrollWidth))
                        .size(scrollWidth, viewportHeight)
                        .viewport(viewportHeight)
                        .content(totalHeight)
                        .offset(currentOffset)
                        .step(scrollStep)
                        .zIndex(zIndex_ + 1)
                        .transition(transition_)
                        .build();
                }

                if (hMaxOffset > 0.0f) {
                    // 横向滚动条：底部通栏（不含纵向滚动条列），覆盖式悬浮于内容上。
                    // 根元素 id = stateId = <id>.h——Runtime::setScrollOffset 经
                    // ui_.find(stateId) 找 owner 回调 onScrollOffsetChanged（与纵向
                    // virtualList 根同构），id 与 stateId 必须一致。
                    components::scroll(ui_, id_ + ".h")
                        .theme(tokens_)
                        .style(scrollStyle_)
                        .horizontal()
                        .position(0.0f, std::max(0.0f, viewportHeight - metrics_.control.scrollbar))
                        .size(rowAreaWidth, metrics_.control.scrollbar)
                        .viewport(rowAreaWidth)
                        .content(contentWidth_)
                        .offset(hCurrent)
                        .step(scrollStep)
                        .zIndex(zIndex_ + 2)
                        .transition(transition_)
                        .onChange([onHChange](float value) {
                            if (onHChange) {
                                onHChange(value);
                            }
                        })
                        .build();
                }
            })
            .build();
    }

private:
    core::dsl::Ui& ui_;
    std::string id_;
    ScrollStyle scrollStyle_;
    theme::ThemeMetricTokens metrics_;
    theme::ThemeColorTokens tokens_ = theme::dark();
    core::Transition transition_ = core::Transition::make(0.12f, core::Ease::OutCubic);
    std::function<void(float)> onChange_;
    std::function<void(float)> onHChange_;
    RowCompose row_;
    std::int64_t itemCount_ = 0;
    float width_ = 320.0f;
    float height_ = 220.0f;
    float rowHeight_ = 36.0f;
    float offset_ = 0.0f;
    float step_ = 0.0f;
    float contentWidth_ = 0.0f;
    float hOffset_ = 0.0f;
    float overscanViewports_ = 1.0f;
    float scrollbarWidth_ = -1.0f;
    float scrollbarGap_ = -1.0f;
    float x_ = 0.0f;
    float y_ = 0.0f;
    bool hasX_ = false;
    bool hasY_ = false;
    int zIndex_ = 0;
};

inline VirtualListBuilder virtualList(core::dsl::Ui& ui, const std::string& id) {
    return VirtualListBuilder(ui, id);
}

} // namespace components
