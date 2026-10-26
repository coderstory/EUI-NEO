#pragma once

// text 内部分割头（「成员函数分 TU、类定义进 internal 头」两步法第一步）：
// TextPrimitive::Impl 的类定义与跨 TU 共享的 shaping/字体基础设施声明集中于此。
// 公开 API 仍以 core/render/text.h 为唯一入口；本头不是公开文档的一部分，
// 仅供 core/render/text*.cpp 内部分割使用（与 image_source.h 同层的仓内私有头）。
//
// TU 分工（2026-10-26，DevDesk 14 号票）：
//   text.cpp          —— 生命周期/状态位/图集栅格化/渲染命令/公共 API 转发
//   text_shaping.cpp  —— 字体发现/解析/加载/fallback 栈 + 逐码点 shaping
//   text_measure.cpp  —— 测量（TextMetrics/caret 停靠点/TextSizeCache/measure* 静态成员）
//   text_layout.cpp   —— 布局/断行/省略号装配/顶点装配

#include "core/render/text.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

namespace text_detail {

constexpr FT_Int32 kGlyphLoadFlags = FT_LOAD_DEFAULT | FT_LOAD_COLOR | FT_LOAD_NO_SVG | FT_LOAD_TARGET_LIGHT;

/// U+2026 HORIZONTAL ELLIPSIS，UTF-8 = E2 80 A6。省略号与正文同字体同字号、
/// 走同一条 fallback，所以它的 advance 也用 measureTextWidth 量出来。
constexpr const char* kEllipsisText = "\xE2\x80\xA6";
constexpr int kEllipsisByteLength = 3;   // kEllipsisText 的 UTF-8 字节数（不含 NUL）

struct FontFace {
    std::string path;
    FT_Face face = nullptr;
    float size = 16.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
    float lineGap = 0.0f;
    float glyphScale = 1.0f;
    bool colored = false;

    FontFace() = default;
    FontFace(const FontFace&) = delete;
    FontFace& operator=(const FontFace&) = delete;

    FontFace(FontFace&& other) noexcept {
        *this = std::move(other);
    }

    FontFace& operator=(FontFace&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        if (face) {
            FT_Done_Face(face);
        }
        path = std::move(other.path);
        face = other.face;
        size = other.size;
        ascent = other.ascent;
        descent = other.descent;
        lineGap = other.lineGap;
        glyphScale = other.glyphScale;
        colored = other.colored;
        other.face = nullptr;
        return *this;
    }

    ~FontFace() {
        if (face) {
            FT_Done_Face(face);
        }
    }
};

struct FontInfoHolder {
    std::vector<FontFace> faces;
    std::vector<std::string> lazyFallbackPaths;
};

constexpr std::size_t kFontStackCacheCapacity = 16;

struct FontStackCacheEntry {
    std::shared_ptr<FontInfoHolder> holder;
    std::uint64_t lastUsed = 0;
};

struct FontStackCache {
    std::unordered_map<std::string, FontStackCacheEntry> entries;
    std::uint64_t accessTick = 0;
};

FontStackCache& sharedFontStackCache();

// —— 以下由 text_shaping.cpp 提供 ————————————————————————————————

unsigned int readUtf8Codepoint(const std::string& text, size_t& index);

std::shared_ptr<FontInfoHolder> loadSharedFontStack(const std::string& fontPath, float fontSize, int fontWeight);

std::vector<TextPrimitive::ShapedGlyph> shapeTextWithFontStack(FontInfoHolder& holder,
                                                               const std::string& text,
                                                               float fontSize);

// Pick a platform-specific UI font variant that matches the requested weight.
std::string resolveSystemUiFontPathForWeightImpl(int fontWeight);

// Impl::resolveFontPath 的本体（fontFamily 语义表驱动）。
std::string resolveFontPathForFamily(const std::string& fontFamily, int fontWeight);

std::string& defaultUiFontFileOverride();
std::string& defaultIconFontFileOverride();

// —— 以下由 text_measure.cpp 提供 ————————————————————————————————
// 清两套共享缓存（字体栈 + 文本尺寸）。名字沿用拆分前的内部函数名，
// 语义不变：setDefaultFontFiles 换默认字体时两者都必须失效。
void clearSharedFontStackCache();

/// 单行省略号截断：返回可保留的字形数——满足 prefixAdvance(keep) + ellipsisAdvance
/// <= maxWidth 的最大 keep。省略号的 advance 已从预算里预扣，所以返回值直接就是
/// 「该行保留几个原字形」。advanceAt(i) 给第 i 个字形的 advance：渲染路径传
/// ShapedGlyph::advance，测量路径传 caretX 差分（makeTextMetrics 里 caretX 就是
/// 逐字累加的，两条路径同源，因此同一套算术给出同一答案）。
template <typename AdvanceAt>
std::size_t ellipsisKeepCount(std::size_t glyphCount,
                              float maxWidth,
                              float ellipsisAdvance,
                              AdvanceAt advanceAt) {
    const float budget = maxWidth - ellipsisAdvance;
    float width = 0.0f;
    std::size_t keep = 0;
    for (std::size_t index = 0; index < glyphCount; ++index) {
        const float next = width + advanceAt(index);
        if (next > budget) {
            break;
        }
        width = next;
        ++keep;
    }
    return keep;
}

} // namespace text_detail

// TextPrimitive::Impl —— 完整类定义。成员函数实现分落在 text*.cpp 四个 TU，
// 本结构体的数据成员与成员声明是对外唯一的事实来源。
struct TextPrimitive::Impl {
    struct LaidOutGlyph {
        Glyph glyph;
        float x = 0.0f;
        float y = 0.0f;
        int byteStart = 0;   // 全文 UTF-8 字节偏移（run 着色查找用）
    };

    struct Line {
        std::vector<LaidOutGlyph> glyphs;
        float width = 0.0f;
        float inkTop = 0.0f;
        float inkBottom = 0.0f;
        bool hasInk = false;
    };

    Impl() = default;
    Impl(float x, float y) : position_{x, y} {}

    bool initialize();
    void destroy();

    void setPosition(float x, float y);
    void setText(const std::string& text);
    void setFontFamily(const std::string& fontFamily);
    void setFontSize(float fontSize);
    void setFontWeight(int fontWeight);
    void setColor(const Color& color);
    void setMaxWidth(float maxWidth);
    void setWrap(bool wrap);
    void setOverflow(TextOverflow overflow);
    void setHorizontalAlign(HorizontalAlign align);
    void setVerticalAlign(VerticalAlign align);
    void setLineHeight(float lineHeight);
    void setStyle(const TextStyle& style);
    void setRuns(const std::vector<TextRun>& runs);
    void setVisualScale(float originX, float originY, float scale);
    void setTransform(const Transform& transform, const Rect& frame);
    void setTransformMatrix(const TransformMatrix& matrix);

    const TextStyle& style() const;
    Vec2 position() const;
    Vec2 measuredSize();
    static float measureTextWidth(const std::string& text,
                                  const std::string& fontFamily = {},
                                  float fontSize = 16.0f,
                                  int fontWeight = 400);
    static TextMetrics measureTextMetrics(const std::string& text,
                                          const std::string& fontFamily = {},
                                          float fontSize = 16.0f,
                                          int fontWeight = 400);
    static Vec2 measureTextSize(const TextStyle& style);
    static void setDefaultFontFiles(const std::string& textFontFile, const std::string& iconFontFile);

    void prepare();
    void render(int windowWidth, int windowHeight);

    bool loadFont();
    bool ensureGlyph(const ShapedGlyph& shaped);
    Glyph* findGlyph(std::uint64_t key);
    void cacheGlyph(std::uint64_t key, const Glyph& glyph);
    void invalidateLayout();
    void invalidateVertices();
    void rebuildLayout();
    void rebuildVertices();
    std::vector<ShapedGlyph> shapeText(const std::string& text);
    void appendShapedGlyphToLine(Line& line, const ShapedGlyph& shaped, float& cursorX, int paragraphOffset);
    void appendEllipsisToLine(Line& line, float& cursorX, int paragraphOffset, int paragraphByteEnd);

    static unsigned int readCodepoint(const std::string& text, size_t& index);
    static std::string resolveFontPath(const std::string& fontFamily, int fontWeight);

    Vec2 position_;
    Vec2 visualScaleOrigin_;
    float visualScale_ = 1.0f;
    Transform transform_;
    Rect transformFrame_;
    TransformMatrix transformMatrix_;
    bool hasTransformMatrix_ = false;
    TextStyle style_;
    std::shared_ptr<void> fontInfoStorage_;
    float scale_ = 1.0f;
    float ascent_ = 0.0f;
    float descent_ = 0.0f;
    float lineGap_ = 0.0f;

    std::unordered_map<std::uint64_t, Glyph> glyphs_;

    std::vector<Line> lines_;
    std::vector<float> vertices_;
    Vec2 measuredSize_;
    bool layoutDirty_ = true;
    bool verticesDirty_ = true;
    bool fontDirty_ = true;
};

} // namespace core
