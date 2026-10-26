// text_measure —— 测量：TextMetrics/caret 停靠点/TextSizeCache/measure* 静态成员。
// TU 分工见 core/render/text_internal.h 头注。逐字平移自拆分前的 text.cpp。

#include "core/render/text_internal.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

namespace core {

namespace {

constexpr std::size_t kTextSizeCacheCapacity = 1024;

struct TextSizeCacheKey {
    std::string text;
    std::string fontFamily;
    float fontSize = 0.0f;
    float maxWidth = 0.0f;
    float lineHeight = 0.0f;
    int fontWeight = 0;
    bool wrap = false;
    /// overflow 必须进 key：Ellipsis 会改变测量宽度，漏了就命中旧条目拿到错尺寸。
    /// 与本字段的构造、operator==、hash 三处必须同步。
    TextOverflow overflow = TextOverflow::Clip;

    bool operator==(const TextSizeCacheKey& other) const {
        return text == other.text &&
               fontFamily == other.fontFamily &&
               fontSize == other.fontSize &&
               maxWidth == other.maxWidth &&
               lineHeight == other.lineHeight &&
               fontWeight == other.fontWeight &&
               wrap == other.wrap &&
               overflow == other.overflow;
    }
};

struct TextSizeCacheKeyHash {
    std::size_t operator()(const TextSizeCacheKey& key) const {
        std::size_t value = std::hash<std::string>{}(key.text);
        const auto combine = [&](std::size_t part) {
            value ^= part + 0x9e3779b9u + (value << 6u) + (value >> 2u);
        };
        combine(std::hash<std::string>{}(key.fontFamily));
        combine(std::hash<float>{}(key.fontSize));
        combine(std::hash<float>{}(key.maxWidth));
        combine(std::hash<float>{}(key.lineHeight));
        combine(std::hash<int>{}(key.fontWeight));
        combine(std::hash<bool>{}(key.wrap));
        combine(std::hash<int>{}(static_cast<int>(key.overflow)));
        return value;
    }
};

struct TextSizeCacheEntry {
    Vec2 size;
    std::uint64_t lastUsed = 0;
};

struct TextSizeCache {
    std::unordered_map<TextSizeCacheKey, TextSizeCacheEntry, TextSizeCacheKeyHash> entries;
    std::uint64_t accessTick = 0;
};

TextSizeCache& sharedTextSizeCache() {
    static TextSizeCache cache;
    return cache;
}

TextPrimitive::TextMetrics makeTextMetrics(const std::string& text,
                                           const std::vector<TextPrimitive::ShapedGlyph>& shaped) {
    TextPrimitive::TextMetrics metrics;
    metrics.byteIndices.reserve(shaped.size() + 1);
    metrics.caretX.reserve(shaped.size() + 1);
    auto addStop = [&](int byteIndex, float x) {
        byteIndex = std::clamp(byteIndex, 0, static_cast<int>(text.size()));
        if (!metrics.byteIndices.empty() && metrics.byteIndices.back() == byteIndex) {
            metrics.caretX.back() = x;
            return;
        }
        metrics.byteIndices.push_back(byteIndex);
        metrics.caretX.push_back(x);
    };

    addStop(0, 0.0f);

    float cursorX = 0.0f;
    for (const TextPrimitive::ShapedGlyph& glyph : shaped) {
        const float startX = cursorX;
        cursorX += glyph.advance;
        addStop(glyph.byteStart, startX);
        addStop(glyph.byteEnd, cursorX);
    }

    metrics.width = cursorX;
    addStop(static_cast<int>(text.size()), metrics.width);
    return metrics;
}

} // namespace

namespace text_detail {

void clearSharedFontStackCache() {
    FontStackCache& cache = sharedFontStackCache();
    cache.entries.clear();
    cache.accessTick = 0;

    TextSizeCache& sizeCache = sharedTextSizeCache();
    sizeCache.entries.clear();
    sizeCache.accessTick = 0;
}

} // namespace text_detail

float TextPrimitive::Impl::measureTextWidth(const std::string& text,
                                      const std::string& fontFamily,
                                      float fontSize,
                                      int fontWeight) {
    return measureTextMetrics(text, fontFamily, fontSize, fontWeight).width;
}

TextPrimitive::TextMetrics TextPrimitive::Impl::measureTextMetrics(const std::string& text,
                                                             const std::string& fontFamily,
                                                             float fontSize,
                                                             int fontWeight) {
    TextMetrics empty;
    empty.byteIndices = {0};
    empty.caretX = {0.0f};
    if (text.empty()) {
        return empty;
    }

    const float size = std::max(1.0f, fontSize);
    const std::string fontPath = resolveFontPath(fontFamily, fontWeight);
    auto holder = text_detail::loadSharedFontStack(fontPath, size, fontWeight);
    if (!holder || holder->faces.empty()) {
        return empty;
    }

    return makeTextMetrics(text, text_detail::shapeTextWithFontStack(*holder, text, size));
}

Vec2 TextPrimitive::Impl::measureTextSize(const TextStyle& style) {
    TextSizeCache& cache = sharedTextSizeCache();
    // Ellipsis 下即使 wrap == false，maxWidth 也参与测量（Clip 保持旧语义）。
    const bool ellipsisActive = style.overflow == TextOverflow::Ellipsis && !style.wrap && style.maxWidth > 0.0f;
    TextSizeCacheKey cacheKey{
        style.text,
        style.fontFamily,
        style.fontSize,
        (style.wrap || ellipsisActive) ? style.maxWidth : 0.0f,
        style.lineHeight,
        style.fontWeight,
        style.wrap,
        style.overflow
    };
    const auto cached = cache.entries.find(cacheKey);
    if (cached != cache.entries.end()) {
        cached->second.lastUsed = ++cache.accessTick;
        return cached->second.size;
    }

    const float lineHeight = style.lineHeight > 0.0f ? style.lineHeight : style.fontSize * 1.2f;
    const bool constrainWidth = style.wrap || ellipsisActive;
    const float maxWidth = constrainWidth && style.maxWidth > 0.0f ? style.maxWidth : 0.0f;
    float measuredWidth = 0.0f;
    int lineCount = 0;

    size_t paragraphStart = 0;
    while (paragraphStart <= style.text.size()) {
        const size_t newline = style.text.find('\n', paragraphStart);
        const size_t paragraphEnd = newline == std::string::npos ? style.text.size() : newline;
        std::string paragraph = style.text.substr(paragraphStart, paragraphEnd - paragraphStart);
        if (!paragraph.empty() && paragraph.back() == '\r') {
            paragraph.pop_back();
        }

        const TextMetrics metrics = measureTextMetrics(paragraph, style.fontFamily, style.fontSize, style.fontWeight);
        float lineWidth = 0.0f;
        ++lineCount;
        const std::size_t glyphCount = metrics.caretX.empty() ? 0u : metrics.caretX.size() - 1u;
        // 恰好等于 maxWidth 不加省略号。与 rebuildLayout 共用 ellipsisKeepCount，
        // 且 caretX 就是 shaped glyph 的逐字 advance 累加，因此测量宽度与渲染
        // 宽度逐位一致。
        if (ellipsisActive && !metrics.caretX.empty() && metrics.caretX.back() > maxWidth) {
            const float ellipsisWidth =
                measureTextWidth(text_detail::kEllipsisText, style.fontFamily, style.fontSize, style.fontWeight);
            const std::size_t keep = text_detail::ellipsisKeepCount(
                glyphCount, maxWidth, ellipsisWidth,
                [&](std::size_t index) { return metrics.caretX[index + 1] - metrics.caretX[index]; });
            lineWidth = metrics.caretX[keep] + ellipsisWidth;
        } else {
            for (size_t index = 1; index < metrics.caretX.size(); ++index) {
                const float advance = metrics.caretX[index] - metrics.caretX[index - 1];
                if (maxWidth > 0.0f && lineWidth > 0.0f && lineWidth + advance > maxWidth) {
                    measuredWidth = std::max(measuredWidth, lineWidth);
                    lineWidth = 0.0f;
                    ++lineCount;
                }
                lineWidth += advance;
            }
        }
        measuredWidth = std::max(measuredWidth, lineWidth);

        if (newline == std::string::npos) {
            break;
        }
        paragraphStart = newline + 1;
    }

    const Vec2 measuredSize{measuredWidth, static_cast<float>(lineCount) * lineHeight};
    if (cache.entries.size() >= kTextSizeCacheCapacity) {
        const auto oldest = std::min_element(cache.entries.begin(), cache.entries.end(),
                                             [](const auto& left, const auto& right) {
                                                 return left.second.lastUsed < right.second.lastUsed;
                                             });
        if (oldest != cache.entries.end()) {
            cache.entries.erase(oldest);
        }
    }
    cache.entries.emplace(std::move(cacheKey), TextSizeCacheEntry{measuredSize, ++cache.accessTick});
    return measuredSize;
}

} // namespace core
