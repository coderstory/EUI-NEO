// text_layout —— 布局/断行/省略号装配/顶点装配。
// TU 分工见 core/render/text_internal.h 头注。逐字平移自拆分前的 text.cpp。

#include "core/render/text_internal.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace core {

void TextPrimitive::Impl::rebuildLayout() {
    if (fontDirty_ && !loadFont()) {
        layoutDirty_ = false;
        return;
    }

    lines_.clear();
    measuredSize_ = {};

    Line currentLine;
    float cursorX = 0.0f;
    const float lineHeight = style_.lineHeight > 0.0f ? style_.lineHeight : style_.fontSize * 1.2f;
    const float maxWidth = style_.maxWidth > 0.0f ? style_.maxWidth : 0.0f;
    // 省略号是单行语义：wrap == true 时不开，多行截断按 CSS 的形状是另一套规则。
    const bool ellipsisActive = style_.overflow == TextOverflow::Ellipsis && !style_.wrap && maxWidth > 0.0f;

    size_t paragraphStart = 0;
    while (paragraphStart <= style_.text.size()) {
        const size_t newline = style_.text.find('\n', paragraphStart);
        const size_t paragraphEnd = newline == std::string::npos ? style_.text.size() : newline;
        std::string paragraph = style_.text.substr(paragraphStart, paragraphEnd - paragraphStart);
        if (!paragraph.empty() && paragraph.back() == '\r') {
            paragraph.pop_back();
        }

        const std::vector<ShapedGlyph> shaped = shapeText(paragraph);
        std::size_t keepCount = shaped.size();
        bool truncated = false;
        if (ellipsisActive) {
            float totalWidth = 0.0f;
            for (const ShapedGlyph& glyph : shaped) {
                totalWidth += glyph.advance;
            }
            // 恰好等于 maxWidth 不加省略号，差一个字形才加。
            if (totalWidth > maxWidth) {
                keepCount = text_detail::ellipsisKeepCount(shaped.size(),
                                                           maxWidth,
                                                           measureTextWidth(text_detail::kEllipsisText, style_.fontFamily,
                                                                            style_.fontSize, style_.fontWeight),
                                                           [&](std::size_t index) { return shaped[index].advance; });
                truncated = true;
            }
        }
        for (std::size_t index = 0; index < shaped.size(); ++index) {
            if (index >= keepCount) {
                break;
            }
            const ShapedGlyph& glyph = shaped[index];
            const float advance = glyph.advance;
            if (style_.wrap && maxWidth > 0.0f && cursorX > 0.0f && cursorX + advance > maxWidth) {
                measuredSize_.x = std::max(measuredSize_.x, currentLine.width);
                lines_.push_back(currentLine);
                currentLine = {};
                cursorX = 0.0f;
            }

            appendShapedGlyphToLine(currentLine, glyph, cursorX, static_cast<int>(paragraphStart));
        }
        if (truncated) {
            appendEllipsisToLine(currentLine, cursorX, static_cast<int>(paragraphStart),
                                 static_cast<int>(paragraph.size()));
        }

        if (newline == std::string::npos) {
            break;
        }

        measuredSize_.x = std::max(measuredSize_.x, currentLine.width);
        lines_.push_back(currentLine);
        currentLine = {};
        cursorX = 0.0f;
        paragraphStart = newline + 1;
    }

    measuredSize_.x = std::max(measuredSize_.x, currentLine.width);
    lines_.push_back(currentLine);
    measuredSize_.y = lines_.empty() ? 0.0f : static_cast<float>(lines_.size()) * lineHeight;
    layoutDirty_ = false;
    invalidateVertices();
}

void TextPrimitive::Impl::rebuildVertices() {
    vertices_.clear();
    const float lineHeight = style_.lineHeight > 0.0f ? style_.lineHeight : style_.fontSize * 1.2f;
    // runs 已归一化（升序、不重叠）；命中返回 run 色，未命中返回 nullptr（用 style_.color）。
    const std::vector<TextRun>& runs = style_.runs;
    const auto runColorAt = [&runs](int byteStart) -> const Color* {
        if (runs.empty()) {
            return nullptr;
        }
        const auto it = std::upper_bound(runs.begin(), runs.end(), byteStart,
            [](int value, const TextRun& run) { return value < run.byteStart; });
        if (it == runs.begin()) {
            return nullptr;
        }
        const TextRun& run = *(it - 1);
        return byteStart < run.byteEnd ? &run.color : nullptr;
    };
    const auto close = [](float left, float right) {
        return std::fabs(left - right) <= 0.0001f;
    };
    const bool pixelAlignedMatrix = hasTransformMatrix_ &&
        close(transformMatrix_.m00, 1.0f) && close(transformMatrix_.m01, 0.0f) &&
        close(transformMatrix_.m10, 0.0f) && close(transformMatrix_.m11, 1.0f) &&
        close(transformMatrix_.px, 0.0f) && close(transformMatrix_.py, 0.0f) &&
        close(transformMatrix_.pw, 1.0f) &&
        close(transformMatrix_.tx, std::round(transformMatrix_.tx)) &&
        close(transformMatrix_.ty, std::round(transformMatrix_.ty));
    float blockYOffset = 0.0f;
    if (style_.verticalAlign == VerticalAlign::Center) {
        float inkTop = std::numeric_limits<float>::max();
        float inkBottom = std::numeric_limits<float>::lowest();
        for (size_t lineIndex = 0; lineIndex < lines_.size(); ++lineIndex) {
            const Line& line = lines_[lineIndex];
            if (!line.hasInk) {
                continue;
            }
            const float lineY = static_cast<float>(lineIndex) * lineHeight;
            inkTop = std::min(inkTop, lineY + line.inkTop);
            inkBottom = std::max(inkBottom, lineY + line.inkBottom);
        }
        if (inkTop <= inkBottom) {
            blockYOffset = -(inkTop + inkBottom) * 0.5f;
        } else {
            blockYOffset = -measuredSize_.y * 0.5f;
        }
    } else if (style_.verticalAlign == VerticalAlign::Bottom) {
        blockYOffset = -measuredSize_.y;
    }

    for (size_t lineIndex = 0; lineIndex < lines_.size(); ++lineIndex) {
        const Line& line = lines_[lineIndex];
        float lineX = position_.x;
        if (style_.horizontalAlign == HorizontalAlign::Center) {
            lineX -= line.width * 0.5f;
        } else if (style_.horizontalAlign == HorizontalAlign::Right) {
            lineX -= line.width;
        }

        const float lineY = position_.y + blockYOffset + static_cast<float>(lineIndex) * lineHeight;
        for (const LaidOutGlyph& laidOut : line.glyphs) {
            const Glyph& glyph = laidOut.glyph;
            const float x0 = lineX + laidOut.x + glyph.xOffset;
            const float y0 = lineY + laidOut.y + glyph.yOffset;
            const float x1 = x0 + glyph.width;
            const float y1 = y0 + glyph.height;
            const float colored = glyph.colored ? 1.0f : 0.0f;
            const Color* runColor = runColorAt(laidOut.byteStart);
            const Color& glyphColor = runColor != nullptr ? *runColor : style_.color;
            Vec2 p0{x0, y0};
            Vec2 p1{x1, y0};
            Vec2 p2{x1, y1};
            Vec2 p3{x0, y1};

            if (hasTransformMatrix_) {
                p0 = core::transformPoint(transformMatrix_, p0.x, p0.y);
                p1 = core::transformPoint(transformMatrix_, p1.x, p1.y);
                p2 = core::transformPoint(transformMatrix_, p2.x, p2.y);
                p3 = core::transformPoint(transformMatrix_, p3.x, p3.y);
            } else if (std::fabs(transform_.translate.x) > 0.0001f ||
                std::fabs(transform_.translate.y) > 0.0001f ||
                std::fabs(transform_.scale.x - 1.0f) > 0.0001f ||
                std::fabs(transform_.scale.y - 1.0f) > 0.0001f ||
                std::fabs(transform_.rotate) > 0.0001f) {
                const Vec2 origin{
                    transformFrame_.x + transformFrame_.width * transform_.origin.x,
                    transformFrame_.y + transformFrame_.height * transform_.origin.y
                };
                const float cosine = std::cos(transform_.rotate);
                const float sine = std::sin(transform_.rotate);
                auto transformPoint = [&](Vec2 point) {
                    const float scaledX = (point.x - origin.x) * transform_.scale.x;
                    const float scaledY = (point.y - origin.y) * transform_.scale.y;
                    return Vec2{
                        origin.x + scaledX * cosine - scaledY * sine + transform_.translate.x,
                        origin.y + scaledX * sine + scaledY * cosine + transform_.translate.y
                    };
                };
                p0 = transformPoint(p0);
                p1 = transformPoint(p1);
                p2 = transformPoint(p2);
                p3 = transformPoint(p3);
            }

            if (!hasTransformMatrix_ && std::fabs(visualScale_ - 1.0f) > 0.0001f) {
                auto scalePoint = [&](Vec2 point) {
                    return Vec2{
                        visualScaleOrigin_.x + (point.x - visualScaleOrigin_.x) * visualScale_,
                        visualScaleOrigin_.y + (point.y - visualScaleOrigin_.y) * visualScale_
                    };
                };
                p0 = scalePoint(p0);
                p1 = scalePoint(p1);
                p2 = scalePoint(p2);
                p3 = scalePoint(p3);
            }

            if (!glyph.colored && pixelAlignedMatrix) {
                const float offsetX = std::round(p0.x) - p0.x;
                const float offsetY = std::round(p0.y) - p0.y;
                p0 = {p0.x + offsetX, p0.y + offsetY};
                p1 = {p1.x + offsetX, p1.y + offsetY};
                p2 = {p2.x + offsetX, p2.y + offsetY};
                p3 = {p3.x + offsetX, p3.y + offsetY};
            }

            // 顶点格式 9 float：x, y, u, v, colored, r, g, b, a（per-vertex color）。
            // 空 runs 时 glyphColor == style_.color，与旧 uniform 路径输出等价。
            vertices_.insert(vertices_.end(), {
                p0.x, p0.y, glyph.u0, glyph.v0, colored, glyphColor.r, glyphColor.g, glyphColor.b, glyphColor.a,
                p1.x, p1.y, glyph.u1, glyph.v0, colored, glyphColor.r, glyphColor.g, glyphColor.b, glyphColor.a,
                p2.x, p2.y, glyph.u1, glyph.v1, colored, glyphColor.r, glyphColor.g, glyphColor.b, glyphColor.a,
                p0.x, p0.y, glyph.u0, glyph.v0, colored, glyphColor.r, glyphColor.g, glyphColor.b, glyphColor.a,
                p2.x, p2.y, glyph.u1, glyph.v1, colored, glyphColor.r, glyphColor.g, glyphColor.b, glyphColor.a,
                p3.x, p3.y, glyph.u0, glyph.v1, colored, glyphColor.r, glyphColor.g, glyphColor.b, glyphColor.a
            });
        }
    }
    verticesDirty_ = false;
}

void TextPrimitive::Impl::appendShapedGlyphToLine(Line& line,
                                                  const ShapedGlyph& shaped,
                                                  float& cursorX,
                                                  int paragraphOffset) {
    if (!ensureGlyph(shaped)) {
        cursorX += shaped.advance;
        line.width = cursorX;
        return;
    }

    const Glyph* glyph = findGlyph(shaped.key);
    if (glyph && shaped.key != 0 && shaped.codepoint != ' ' && shaped.codepoint != '\t') {
        line.glyphs.push_back({*glyph, cursorX + shaped.xOffset, shaped.yOffset,
                               paragraphOffset + shaped.byteStart});
        const float top = shaped.yOffset + glyph->yOffset;
        const float bottom = top + glyph->height;
        if (!line.hasInk) {
            line.inkTop = top;
            line.inkBottom = bottom;
            line.hasInk = true;
        } else {
            line.inkTop = std::min(line.inkTop, top);
            line.inkBottom = std::max(line.inkBottom, bottom);
        }
    }
    cursorX += shaped.advance;
    line.width = cursorX;
}

void TextPrimitive::Impl::appendEllipsisToLine(Line& line,
                                               float& cursorX,
                                               int paragraphOffset,
                                               int paragraphByteEnd) {
    const std::vector<ShapedGlyph> shaped = shapeText(text_detail::kEllipsisText);
    if (shaped.empty()) {
        return;
    }
    ShapedGlyph glyph = shaped.front();
    // 省略号取代的是被截掉的那个位置，所以字节偏移落在段落末尾——run 着色
    // 按「截断处所在位置」归类，而不是按段落开头。
    glyph.byteStart = paragraphByteEnd;
    glyph.byteEnd = paragraphByteEnd + text_detail::kEllipsisByteLength;
    appendShapedGlyphToLine(line, glyph, cursorX, paragraphOffset);
}

} // namespace core
