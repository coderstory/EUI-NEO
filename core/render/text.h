#pragma once

#include "core/render/render_types.h"
#include "core/render/text_types.h"

#include <memory>
#include <cstdint>
#include <string>
#include <vector>

namespace core {

class TextPrimitive {
public:
    struct Glyph {
        float advance = 0.0f;
        float xOffset = 0.0f;
        float yOffset = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;
        bool colored = false;
    };

    struct ShapedGlyph {
        std::uint64_t key = 0;
        unsigned int codepoint = 0;
        int byteStart = 0;
        int byteEnd = 0;
        float advance = 0.0f;
        float xOffset = 0.0f;
        float yOffset = 0.0f;
    };

    struct TextMetrics {
        float width = 0.0f;
        std::vector<int> byteIndices;
        std::vector<float> caretX;
    };

    TextPrimitive();
    TextPrimitive(float x, float y);
    ~TextPrimitive();

    TextPrimitive(const TextPrimitive&) = delete;
    TextPrimitive& operator=(const TextPrimitive&) = delete;
    TextPrimitive(TextPrimitive&&) noexcept;
    TextPrimitive& operator=(TextPrimitive&&) noexcept;

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
    void setHorizontalAlign(HorizontalAlign align);
    void setVerticalAlign(VerticalAlign align);
    void setLineHeight(float lineHeight);
    void setStyle(const TextStyle& style);
    /// 颜色 runs：只重建顶点色，不动 layout（颜色不影响 shaping/advance）。
    /// 期望已归一化（normalizeTextRuns）；runtime 侧在内容变化时已归一化。
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

    // Resolve the actual font file path used for the given family + weight.
    // Returns the absolute path of the TTF/OTF/TTC file that would be opened
    // by FreeType when rendering text with these style values. Useful for
    // introspection, debugging, and tests that need to verify font selection.
    // Returns an empty string when no suitable font file is found.
    static std::string resolveFontPath(const std::string& fontFamily, int fontWeight = 400);
    // Platform-specific UI font path for the requested weight (300/400/700/800).
    // Picks an explicit weight file when the system ships one (Segoe UI Bold,
    // SFNS-Bold, NotoSans-Bold, ...); falls back to the variable / regular
    // font when no weight-specific variant exists. Returns empty string when
    // no UI font is found at all.
    static std::string resolveSystemUiFontPathForWeight(int fontWeight = 400);

    void prepare();
    void render(int windowWidth, int windowHeight);

    /// 测试/诊断用：prepare() 之后的原始顶点缓冲。
    /// 顶点格式 x,y,u,v,colored[,r,g,b,a]，每顶点 float 数由当前顶点格式决定。
    const std::vector<float>& debugVertices() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace core
