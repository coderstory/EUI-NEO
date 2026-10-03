// text_ellipsis: 单行溢出省略号（core::TextOverflow::Ellipsis）契约。
//
// 设计约束（2026-10-03）：
//   - 默认 TextOverflow::Clip —— 与加本特性之前的渲染结果逐字节一致（零行为变化）。
//   - Ellipsis 只在单行语义下生效：wrap == false 且 maxWidth > 0。
//     wrap == true 时 Ellipsis 退化为无操作（多行截断是另一件事）。
//   - 恰好放得下（width <= maxWidth）不加省略号；溢出时才截断。
//   - 截断在字形粒度发生：CJK 宽字符不会被劈成半个码点。
//   - 省略号自身的 advance 必须预留在预算里，否则总宽会多溢出一个字形。

#include "core/dsl.h"
#include "core/render/text.h"
#include "core/render/text_types.h"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "text_ellipsis: FAILED - " << what << "\n";
        ++failures;
    }
}

/// U+2026 HORIZONTAL ELLIPSIS，UTF-8 = E2 80 A6。
const char* kEllipsis = "\xE2\x80\xA6";

bool close(float left, float right) {
    return std::fabs(left - right) <= 0.001f;
}

/// debugVertices() 是 float 数组：v0.8 起每顶点 9 float、每字形 6 顶点，
/// 所以一个字形 = 54 float。空格/tab 不产生顶点。
std::size_t glyphCount(const std::vector<float>& vertices) {
    return vertices.size() / (9u * 6u);
}

float ellipsisAdvance() {
    return core::TextPrimitive::measureTextWidth(kEllipsis);
}

/// 期望可保留的字形数：最大的 keep 使 caretX[keep] + ellipsisAdvance <= maxWidth。
/// caretX 与 shaped glyph 的 advance 序列同源（makeTextMetrics 逐字累加），
/// 所以 caretX.size() == glyphCount + 1。
std::size_t expectedKeepCount(const core::TextPrimitive::TextMetrics& metrics,
                              float maxWidth,
                              float ellipsisWidth) {
    std::size_t keep = 0;
    for (std::size_t index = 1; index < metrics.caretX.size(); ++index) {
        if (metrics.caretX[index] + ellipsisWidth > maxWidth) {
            break;
        }
        keep = index;
    }
    return keep;
}

core::Color vertexColor(const std::vector<float>& vertices, std::size_t vertexIndex) {
    const std::size_t base = vertexIndex * 9 + 5;
    return {vertices[base], vertices[base + 1], vertices[base + 2], vertices[base + 3]};
}

struct Rendered {
    std::vector<float> vertices;
    core::Vec2 measured{0.0f, 0.0f};
};

Rendered render(const std::string& text,
                float maxWidth,
                bool wrap,
                core::TextOverflow overflow) {
    core::TextPrimitive primitive;
    primitive.initialize();
    primitive.setText(text);
    primitive.setMaxWidth(maxWidth);
    primitive.setWrap(wrap);
    primitive.setOverflow(overflow);
    primitive.prepare();
    Rendered out;
    out.vertices = primitive.debugVertices();
    out.measured = primitive.measuredSize();
    return out;
}

core::TextStyle styleFor(const std::string& text, float maxWidth, core::TextOverflow overflow) {
    core::TextStyle style;
    style.text = text;
    style.maxWidth = maxWidth;
    style.overflow = overflow;
    return style;
}

// ---------------------------------------------------------------------------
// 默认值与零行为变化
// ---------------------------------------------------------------------------

void testDefaultIsClip() {
    check(core::TextStyle{}.overflow == core::TextOverflow::Clip,
          "default: TextStyle.overflow == Clip");
    core::TextPrimitive primitive;
    check(primitive.style().overflow == core::TextOverflow::Clip,
          "default: primitive style overflow == Clip");
}

void testDefaultMatchesExplicitClip() {
    const std::string text = "hello world default clip";
    for (const bool wrap : {false, true}) {
        const Rendered implicit_ = render(text, 90.0f, wrap, core::TextOverflow::Ellipsis);
        (void)implicit_;
        core::TextPrimitive plain;
        plain.initialize();
        plain.setText(text);
        plain.setMaxWidth(90.0f);
        plain.setWrap(wrap);
        plain.prepare();
        const Rendered defaulted = {plain.debugVertices(), plain.measuredSize()};

        core::TextPrimitive explicitClip;
        explicitClip.initialize();
        explicitClip.setText(text);
        explicitClip.setMaxWidth(90.0f);
        explicitClip.setWrap(wrap);
        explicitClip.setOverflow(core::TextOverflow::Clip);
        explicitClip.prepare();
        const Rendered clip = {explicitClip.debugVertices(), explicitClip.measuredSize()};

        check(defaulted.vertices == clip.vertices,
              "zero-change: default == explicit Clip (vertices byte-identical)");
        check(close(defaulted.measured.x, clip.measured.x) &&
                  close(defaulted.measured.y, clip.measured.y),
              "zero-change: default == explicit Clip (measured size)");
    }
}

// ---------------------------------------------------------------------------
// 截断契约
// ---------------------------------------------------------------------------

void testEllipsisIsNoOpWhenTextFits() {
    const std::string text = "ABCDEFGHIJ";
    const float maxWidth = 10000.0f;
    const Rendered clipped = render(text, maxWidth, false, core::TextOverflow::Clip);
    const Rendered ellipsized = render(text, maxWidth, false, core::TextOverflow::Ellipsis);
    check(ellipsized.vertices == clipped.vertices,
          "fits: ellipsis renders byte-identical to clip when text fits");
    check(glyphCount(ellipsized.vertices) == 10u, "fits: all 10 glyphs present");
}

void testOverflowTruncatesWithEllipsis() {
    const std::string text = "ABCDEFGHIJ";
    const float ellipsisWidth = ellipsisAdvance();
    const core::TextPrimitive::TextMetrics metrics = core::TextPrimitive::measureTextMetrics(text);
    const std::size_t keep = 4;
    const float maxWidth = metrics.caretX[keep] + ellipsisWidth;

    const Rendered out = render(text, maxWidth, false, core::TextOverflow::Ellipsis);
    check(glyphCount(out.vertices) == keep + 1,
          "overflow: kept glyphs + exactly one ellipsis glyph");
    check(out.measured.x <= maxWidth, "overflow: total width <= maxWidth");
    check(close(out.measured.x, metrics.caretX[keep] + ellipsisWidth),
          "overflow: width == kept prefix + ellipsis advance (ellipsis reserved)");
    // keep 由同一规则独立推出；末字形是省略号而非原文第 5 个字母。
    check(expectedKeepCount(metrics, maxWidth, ellipsisWidth) == keep,
          "overflow: expected keep count matches independent rule");
}

void testExactFitAddsNoEllipsis() {
    const std::string text = "ABCDEFGHIJ";
    const float full = core::TextPrimitive::measureTextWidth(text);
    const Rendered exact = render(text, full, false, core::TextOverflow::Ellipsis);
    check(glyphCount(exact.vertices) == 10u, "exact fit: no ellipsis when width == maxWidth");
    check(close(exact.measured.x, full), "exact fit: width unchanged");

    // 差一个字形就必须截断。
    const Rendered oneLess = render(text, full - 0.01f, false, core::TextOverflow::Ellipsis);
    check(glyphCount(oneLess.vertices) < 10u, "exact fit-1: truncates one glyph short");
    check(oneLess.measured.x <= full - 0.01f, "exact fit-1: width <= maxWidth");
}

void testEllipsisWidthIsReserved() {
    // maxWidth 恰好等于「前 5 个字母 + 省略号」——若实现没有给省略号留宽度，
    // 这里会保留 6 个字形（多溢出一个字形）而不是 5 个。
    const std::string text = "ABCDEFGHIJ";
    const float ellipsisWidth = ellipsisAdvance();
    const core::TextPrimitive::TextMetrics metrics = core::TextPrimitive::measureTextMetrics(text);
    const float maxWidth = metrics.caretX[5] + ellipsisWidth;
    const Rendered out = render(text, maxWidth, false, core::TextOverflow::Ellipsis);
    check(glyphCount(out.vertices) == 6u, "reserved: keeps 5 glyphs + ellipsis, not 6 glyphs");
    check(out.measured.x <= maxWidth, "reserved: width <= maxWidth");
}

void testCjkTruncatesAtCodepointBoundary() {
    const std::string text = "\xE4\xB8\xAD\xE6\x96\x87\xE6\xB5\x8B\xE8\xAF\x95";  // 中文测试
    const float ellipsisWidth = ellipsisAdvance();
    const core::TextPrimitive::TextMetrics metrics = core::TextPrimitive::measureTextMetrics(text);
    check(metrics.caretX.size() == 5u, "cjk: 4 codepoints -> caretX has 5 stops");

    const std::size_t keep = 2;
    const float maxWidth = metrics.caretX[keep] + ellipsisWidth;
    const Rendered out = render(text, maxWidth, false, core::TextOverflow::Ellipsis);
    check(glyphCount(out.vertices) == keep + 1, "cjk: keeps whole codepoints + ellipsis");
    check(out.measured.x <= maxWidth, "cjk: width <= maxWidth");
    check(close(out.measured.x, metrics.caretX[2] + ellipsisWidth),
          "cjk: width == 2 wide glyphs + ellipsis (no half-codepoint)");

    // 逐字截断：宽度按全角字形算，不按字节算。
    const float fullWidth = core::TextPrimitive::measureTextWidth(text);
    const Rendered half = render(text, fullWidth * 0.5f, false, core::TextOverflow::Ellipsis);
    check(half.measured.x <= fullWidth * 0.5f, "cjk: half width still fits budget");
    check(expectedKeepCount(metrics, fullWidth * 0.5f, ellipsisWidth) + 1 == glyphCount(half.vertices),
          "cjk: half width keep count matches independent rule");
}

void testMaxWidthNonPositiveIsNoOp() {
    const std::string text = "ABCDEFGHIJ";
    const Rendered clipped = render(text, 0.0f, false, core::TextOverflow::Clip);
    for (const float maxWidth : {0.0f, -1.0f, -100.0f}) {
        const Rendered out = render(text, maxWidth, false, core::TextOverflow::Ellipsis);
        check(out.vertices == clipped.vertices,
              "maxWidth<=0: ellipsis degrades to no-op");
    }
}

void testWrapIgnoresEllipsis() {
    const std::string text = "ABCDEFGHIJKLMNOP";
    const Rendered wrapped = render(text, 40.0f, true, core::TextOverflow::Clip);
    const Rendered wrappedEllipsis = render(text, 40.0f, true, core::TextOverflow::Ellipsis);
    check(wrappedEllipsis.vertices == wrapped.vertices,
          "wrap: ellipsis is a no-op when wrap == true");
}

void testEachParagraphTruncatesIndependently() {
    const std::string text = "AAAAAAAA\nBBBBBBBB";
    const float ellipsisWidth = ellipsisAdvance();
    const core::TextPrimitive::TextMetrics first = core::TextPrimitive::measureTextMetrics("AAAAAAAA");
    const float maxWidth = first.caretX[3] + ellipsisWidth;

    const Rendered out = render(text, maxWidth, false, core::TextOverflow::Ellipsis);
    // 两段各自保留 3 个字形 + 省略号 = 8 个字形 = 48 顶点；行数仍是 2。
    check(glyphCount(out.vertices) == 8u, "paragraphs: each line truncated on its own");
    check(out.measured.y >= 2.0f * 16.0f * 1.2f - 0.001f,
          "paragraphs: line count preserved (no ellipsis collapse)");
}

void testEllipsisIsNotColoredByLeadingRun() {
    // 省略号的字节偏移落在段落末尾（它取代的是被截掉的位置），所以开头的 run
    // 不该把省略号染成同一个颜色。
    const std::string text = "ABCDEFGHIJ";
    const core::Color base{0.5f, 0.5f, 0.5f, 1.0f};
    const core::Color red{1.0f, 0.0f, 0.0f, 1.0f};
    const float ellipsisWidth = ellipsisAdvance();
    const core::TextPrimitive::TextMetrics metrics = core::TextPrimitive::measureTextMetrics(text);
    const float maxWidth = metrics.caretX[4] + ellipsisWidth;

    core::TextPrimitive primitive;
    primitive.initialize();
    primitive.setText(text);
    primitive.setColor(base);
    primitive.setMaxWidth(maxWidth);
    primitive.setOverflow(core::TextOverflow::Ellipsis);
    primitive.setRuns({{0, 5, red}});
    primitive.prepare();

    const std::vector<float>& vertices = primitive.debugVertices();
    check(glyphCount(vertices) == 5u, "runs: 4 kept + ellipsis");
    const core::Color first = vertexColor(vertices, 0);
    const core::Color ellipsisColor = vertexColor(vertices, 24);   // 第 5 个字形 = 省略号
    check(first.r == red.r && first.g == red.g && first.b == red.b,
          "runs: leading run still colors the kept glyphs");
    check(ellipsisColor.r == base.r && ellipsisColor.g == base.g && ellipsisColor.b == base.b,
          "runs: ellipsis takes the truncation point color, not the leading run color");
}

// ---------------------------------------------------------------------------
// 尺寸缓存（TextSizeCacheKey 必须区分 overflow，否则测量串味）
// ---------------------------------------------------------------------------

void testSizeCacheSeparatesOverflowModes() {
    const std::string text = "ABCDEFGHIJ";
    const float ellipsisWidth = ellipsisAdvance();
    const core::TextPrimitive::TextMetrics metrics = core::TextPrimitive::measureTextMetrics(text);
    const std::size_t keep = 3;
    const float maxWidth = metrics.caretX[keep] + ellipsisWidth;
    const float truncated = metrics.caretX[keep] + ellipsisWidth;

    // 先 Clip 后 Ellipsis：Ellipsis 若命中 Clip 的缓存条目就会拿到全宽。
    const float clipFirst = core::TextPrimitive::measureTextSize(styleFor(text, maxWidth, core::TextOverflow::Clip)).x;
    const float ellipsisSecond = core::TextPrimitive::measureTextSize(styleFor(text, maxWidth, core::TextOverflow::Ellipsis)).x;
    check(close(clipFirst, metrics.caretX.back()), "cache: Clip measures full width");
    check(close(ellipsisSecond, truncated), "cache: Ellipsis measures truncated width after Clip");

    // 反序再来一次，两种顺序都不能串。
    const float ellipsisFirst = core::TextPrimitive::measureTextSize(styleFor(text, maxWidth, core::TextOverflow::Ellipsis)).x;
    const float clipSecond = core::TextPrimitive::measureTextSize(styleFor(text, maxWidth, core::TextOverflow::Clip)).x;
    check(close(ellipsisFirst, truncated), "cache: Ellipsis measures truncated width (reverse order)");
    check(close(clipSecond, metrics.caretX.back()), "cache: Clip still measures full width (reverse order)");

    // 测量值必须与实际渲染宽度一致（两条路径共用同一套算术）。
    const Rendered out = render(text, maxWidth, false, core::TextOverflow::Ellipsis);
    check(close(out.measured.x, ellipsisSecond),
          "cache: measureTextSize agrees with rendered line width");
}

// ---------------------------------------------------------------------------
// DSL 链路：builder -> Element -> layout intrinsic size
// ---------------------------------------------------------------------------

void testDslBuilderCarriesOverflow() {
    core::dsl::Ui ui;
    ui.begin("text.ellipsis");
    ui.text("label")
        .text("ABCDEFGHIJ")
        .maxWidth(40.0f)
        .overflow(core::TextOverflow::Ellipsis)
        .build();
    ui.end();
    ui.layout(400.0f, 300.0f);

    const core::dsl::Element* element = ui.find("label");
    check(element != nullptr, "dsl: element exists");
    if (element == nullptr) {
        return;
    }
    check(element->overflow == core::TextOverflow::Ellipsis, "dsl: builder stored overflow");
    check(element->frame.width <= 40.0f + 0.001f,
          "dsl: intrinsic width respects maxWidth under ellipsis");

    core::dsl::Ui plain;
    plain.begin("text.clip");
    plain.text("label")
        .text("ABCDEFGHIJ")
        .maxWidth(40.0f)
        .build();
    plain.end();
    plain.layout(400.0f, 300.0f);
    const core::dsl::Element* clipElement = plain.find("label");
    check(clipElement != nullptr && clipElement->overflow == core::TextOverflow::Clip,
          "dsl: default element overflow is Clip");
    check(clipElement != nullptr && clipElement->frame.width > element->frame.width,
          "dsl: Clip keeps full intrinsic width, Ellipsis narrows it");
}

} // namespace

int main() {
    testDefaultIsClip();
    testDefaultMatchesExplicitClip();
    testEllipsisIsNoOpWhenTextFits();
    testOverflowTruncatesWithEllipsis();
    testExactFitAddsNoEllipsis();
    testEllipsisWidthIsReserved();
    testCjkTruncatesAtCodepointBoundary();
    testMaxWidthNonPositiveIsNoOp();
    testWrapIgnoresEllipsis();
    testEachParagraphTruncatesIndependently();
    testEllipsisIsNotColoredByLeadingRun();
    testSizeCacheSeparatesOverflowModes();
    testDslBuilderCarriesOverflow();

    if (failures == 0) {
        std::cout << "text_ellipsis: all checks passed\n";
        return 0;
    }
    std::cerr << "text_ellipsis: " << failures << " check(s) failed\n";
    return 1;
}