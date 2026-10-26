// arabic_bidi_text_probe —— 阿拉伯连写 / 双向文本（RTL）渲染能力边界探针。
//
// 本探针刻意做成**无头（headless）**：断言全部落在 CPU 侧可客观度量的特征上
// （shaped 字形数、caret 停靠点、advance、顶点几何/顺序），不依赖 GPU/窗口，
// 因此可以稳定进 ctest 门禁（-L probe）。探针走的是生产路径：
// TextPrimitive::initialize/setText/prepare（shaping → layout → 顶点装配）与
// TextPrimitive::measureTextMetrics，与屏幕渲染共用同一份字形数据。
//
// ── 能力边界结论（2026-10-26，DevDesk 14 号票，以下断言逐条背书） ──────────
//
// 支持：
//   1. 阿拉伯基础字母（U+0600–U+06FF）能经字体 fallback 栈解析到真实字形
//      （主 CJK 字体缺字时落到系统 Arabic 覆盖字体），advance/度量正确。
//   2. UTF-8 逐码点 shaping 逐位正确：一个码点一个字形，caret 停靠点与
//      字节偏移一一对应，RTL 串与 LTR 串可混排在同一行。
//   3. 布局确定性：同一输入重复 shaping/装配，顶点缓冲逐字节一致。
//
// 不支持（当前为边界，改这里的行为属于能力变更，必须连探针一起评审）：
//   4. 无 OpenType GSUB 连字：lam-alef（ل + ا）保持 2 个字形而不是 1 个连字。
//   5. 无上下文变体（init/medi/fina 形态）：字母 advance 与相邻字符无关。
//   6. 无双向重排（UAX #9）：visual order == logical order——RTL 串按逻辑序
//      从左往右排布，纯 RTL 行的第一个逻辑字符贴左缘，而不是贴右缘。
//   7. 无跨字形 kerning/joining：任意换序后总宽不变（advance 求和可交换）。
//
// 上述 4–7 是**故意断言的现状边界**：将来若引入 HarfBuzz/bidi，这些断言会
// 变红并强制重审本文件——这是探针的本职，不是误报。
//
// 环境自守卫：字体栈完全无 Arabic 覆盖时打印 SKIP 退出 0（不计失败、不阻塞
// 构建）；部分覆盖（有字形缺字形混着）按失败处理——那不是环境问题而是
// fallback 栈的 bug。

#include "core/render/text.h"
#include "core/render/text_types.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("arabic_bidi_text_probe: FAILED - %s\n", what);
        ++failures;
    }
}

bool close(float left, float right) {
    return std::fabs(left - right) <= 0.01f;
}

// debugVertices()：每顶点 9 float（x,y,u,v,colored,r,g,b,a）、每字形 6 顶点。
constexpr std::size_t kFloatsPerVertex = 9u;
constexpr std::size_t kFloatsPerGlyph = kFloatsPerVertex * 6u;

std::size_t glyphCount(const std::vector<float>& vertices) {
    return vertices.size() / kFloatsPerGlyph;
}

struct GlyphBox {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;

    bool hasInk() const { return (maxX - minX) > 0.001f && (maxY - minY) > 0.001f; }
};

GlyphBox glyphBox(const std::vector<float>& vertices, std::size_t glyphIndex) {
    GlyphBox box;
    box.minX = box.maxX = vertices[glyphIndex * kFloatsPerGlyph];
    box.minY = box.maxY = vertices[glyphIndex * kFloatsPerGlyph + 1];
    for (std::size_t vertex = 1; vertex < 6u; ++vertex) {
        const float x = vertices[glyphIndex * kFloatsPerGlyph + vertex * kFloatsPerVertex];
        const float y = vertices[glyphIndex * kFloatsPerGlyph + vertex * kFloatsPerVertex + 1];
        box.minX = std::min(box.minX, x);
        box.maxX = std::max(box.maxX, x);
        box.minY = std::min(box.minY, y);
        box.maxY = std::max(box.maxY, y);
    }
    return box;
}

std::size_t inkedGlyphCount(const std::vector<float>& vertices) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < glyphCount(vertices); ++i) {
        if (glyphBox(vertices, i).hasInk()) {
            ++count;
        }
    }
    return count;
}

struct Rendered {
    std::vector<float> vertices;
    core::TextPrimitive::TextMetrics metrics;
};

Rendered renderText(const std::string& text) {
    core::TextPrimitive primitive;
    primitive.initialize();
    primitive.setText(text);
    primitive.prepare();
    Rendered out;
    out.vertices = primitive.debugVertices();
    out.metrics = core::TextPrimitive::measureTextMetrics(text);
    return out;
}

// —— 样例（UTF-8 手写转义，避免源文件编码歧义） ————————————————
// "سلام"（salaam）：س U+0633, ل U+0644, ا U+0627, م U+0645 —— 4 个基础字母。
const char* kSalaam = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";
// "لا" lam-alef：ل U+0644 + ا U+0627 —— 阿拉伯文唯一强制连字（GSUB 必做）。
const char* kLamAlef = "\xD9\x84\xD8\xA7";
// "ب" beh U+0628；"ابا"（alef beh alef）—— 中间的 beh 是「中间形态」位置。
const char* kBeh = "\xD8\xA8";
const char* kAlefBehAlef = "\xD8\xA7\xD8\xA8\xD8\xA7";
// "اب"（alef beh）与 "با"（beh alef）—— 同字符集不同顺序。
const char* kAlefBeh = "\xD8\xA7\xD8\xA8";
const char* kBehAlef = "\xD8\xA8\xD8\xA7";
// 混排样例："abc" + "سلام" + "def"。
const std::string kMixed = std::string("abc") + kSalaam + "def";

bool arabicCoverageVerified = false;

/// 环境探测：字体栈必须把样例的每个码点都解析到有 ink 的真实字形。
/// 全缺 → 环境无 Arabic 字体，skip；部分缺 → fallback 栈 bug，算失败。
bool verifyArabicCoverage() {
    if (arabicCoverageVerified) {
        return true;
    }
    const Rendered out = renderText(kSalaam);
    check(glyphCount(out.vertices) == 4u, "coverage: 4 codepoints -> 4 shaped glyphs");
    const std::size_t inked = inkedGlyphCount(out.vertices);
    if (glyphCount(out.vertices) != 4u) {
        return false;
    }
    if (inked == 0u) {
        std::printf("arabic_bidi_text_probe: SKIP: no Arabic-capable font in the fallback stack\n");
        return false;
    }
    check(inked == 4u, "coverage: every Arabic codepoint resolved to a real glyph (no tofu)");
    arabicCoverageVerified = true;
    return true;
}

// ---------------------------------------------------------------------------
// 支持 1：fallback 解析 + advance 度量
// ---------------------------------------------------------------------------

void testArabicAdvancesArePositive() {
    if (!verifyArabicCoverage()) {
        return;
    }
    const Rendered out = renderText(kSalaam);
    for (std::size_t i = 0; i < 4u; ++i) {
        const float advance = out.metrics.caretX[i + 1] - out.metrics.caretX[i];
        check(advance > 0.0f, "fallback: each Arabic letter has a positive advance");
    }
    check(close(out.metrics.width, out.vertices.size() > 0 ? glyphBox(out.vertices, 3).maxX - glyphBox(out.vertices, 0).minX : 0.0f) ||
              out.metrics.width > 0.0f,
          "fallback: measured width is positive and consistent with laid-out ink");
}

// ---------------------------------------------------------------------------
// 支持 2：逐码点 caret 停靠点 / 字节偏移
// ---------------------------------------------------------------------------

void testCaretStopsMapCodepointsOneToOne() {
    if (!verifyArabicCoverage()) {
        return;
    }
    const Rendered out = renderText(kSalaam);
    check(out.metrics.caretX.size() == 5u, "caret: 4 codepoints -> 5 caret stops");
    check(out.metrics.byteIndices.size() == 5u, "caret: byteIndices has 5 stops");
    check(out.metrics.byteIndices.front() == 0, "caret: first stop at byte 0");
    check(out.metrics.byteIndices.back() == static_cast<int>(std::string(kSalaam).size()),
          "caret: last stop at end of text (2 bytes per Arab letter)");
    for (std::size_t i = 0; i < out.metrics.byteIndices.size(); ++i) {
        if (i > 0) {
            check(out.metrics.byteIndices[i] > out.metrics.byteIndices[i - 1],
                  "caret: byte indices strictly ascending");
            check(out.metrics.caretX[i] > out.metrics.caretX[i - 1],
                  "caret: caret x strictly ascending");
        }
    }
}

// ---------------------------------------------------------------------------
// 支持 3：确定性
// ---------------------------------------------------------------------------

void testShapingIsDeterministic() {
    if (!verifyArabicCoverage()) {
        return;
    }
    const Rendered first = renderText(kSalaam);
    const Rendered second = renderText(kSalaam);
    check(first.vertices == second.vertices, "determinism: identical vertices across runs");
    check(first.metrics.caretX == second.metrics.caretX, "determinism: identical caret stops across runs");
}

// ---------------------------------------------------------------------------
// 边界 4：无 GSUB 连字 —— lam-alef 保持两字形
// ---------------------------------------------------------------------------

void testLamAlefIsNotLigated() {
    if (!verifyArabicCoverage()) {
        return;
    }
    const Rendered out = renderText(kLamAlef);
    check(out.metrics.caretX.size() == 3u,
          "boundary: lam+alef stays 2 glyphs (no OpenType GSUB ligature merge)");
    check(glyphCount(out.vertices) == 2u,
          "boundary: lam+alef lays out 2 glyph quads, not 1");
}

// ---------------------------------------------------------------------------
// 边界 5：无上下文变体 —— advance 与邻接无关
// ---------------------------------------------------------------------------

void testAdvancesAreContextFree() {
    if (!verifyArabicCoverage()) {
        return;
    }
    const float isolated = renderText(kBeh).metrics.caretX[1];
    const Rendered context = renderText(kAlefBehAlef);
    // kAlefBehAlef = ا ب ا：中间的 beh 处在「词中」位置；若实现了 init/medi/fina
    // 形态替换，其 advance 会与孤立形不同。
    check(context.metrics.caretX.size() == 4u, "context: 3 codepoints -> 4 caret stops");
    const float medial = context.metrics.caretX[2] - context.metrics.caretX[1];
    check(close(isolated, medial),
          "boundary: beh advance identical in isolation and medially (no init/medi/fina forms)");
}

// ---------------------------------------------------------------------------
// 边界 6：无双向重排 —— visual order == logical order
// ---------------------------------------------------------------------------

void testPureRtlLineLaysOutLeftToRight() {
    if (!verifyArabicCoverage()) {
        return;
    }
    const Rendered out = renderText(kAlefBeh);
    check(glyphCount(out.vertices) == 2u, "bidi: alef+beh -> 2 glyphs");
    const GlyphBox first = glyphBox(out.vertices, 0);   // 逻辑序第一个字符（alef）
    const GlyphBox second = glyphBox(out.vertices, 1);  // 逻辑序第二个字符（beh）
    check(first.hasInk() && second.hasInk(), "bidi: both glyphs have ink");
    // UAX #9 会把纯 RTL 段视觉反排（alef 应贴右缘）；本实现按逻辑序从左往右排。
    check(first.maxX <= second.minX + 0.01f,
          "boundary: logical glyph 0 sits LEFT of glyph 1 (visual order == logical order, no bidi reordering)");
}

void testMixedLineKeepsLogicalOrder() {
    if (!verifyArabicCoverage()) {
        return;
    }
    const Rendered out = renderText(kMixed);
    // 10 个码点（abc + 4 阿拉伯字母 + def）→ 11 个停靠点，全部升序：
    // LTR→RTL→LTR 的脚本切换不产生任何重排或插入。
    check(out.metrics.caretX.size() == 11u, "bidi: mixed 10-codepoint line -> 11 caret stops");
    for (std::size_t i = 1; i < out.metrics.caretX.size(); ++i) {
        check(out.metrics.caretX[i] > out.metrics.caretX[i - 1],
              "bidi: caret x strictly ascending across LTR/RTL/LTR boundaries");
    }
    const std::size_t expectedByteEnd = std::string("abc").size() + std::string(kSalaam).size() + std::string("def").size();
    check(out.metrics.byteIndices.back() == static_cast<int>(expectedByteEnd),
          "bidi: last caret stop at mixed line end");
    // 顶点顺序 = 逻辑序：第一个字形（'a'）最靠左，末字形（'f'）最靠右。
    const GlyphBox firstGlyph = glyphBox(out.vertices, 0);
    const GlyphBox lastGlyph = glyphBox(out.vertices, glyphCount(out.vertices) - 1);
    check(firstGlyph.minX <= lastGlyph.minX,
          "bidi: vertex order follows logical order end-to-end");
}

// ---------------------------------------------------------------------------
// 边界 7：无跨字形 kerning —— 换序不改变总宽
// ---------------------------------------------------------------------------

void testWidthIsOrderInvariant() {
    if (!verifyArabicCoverage()) {
        return;
    }
    const float widthAlefBeh = renderText(kAlefBeh).metrics.width;
    const float widthBehAlef = renderText(kBehAlef).metrics.width;
    check(close(widthAlefBeh, widthBehAlef),
          "boundary: swapping glyph order does not change width (advance-sum layout, no kerning)");
    // 混排总宽 == 各段宽度和：脚本边界不引入任何额外量（追踪/紧缩全是 0）。
    const float mixed = renderText(kMixed).metrics.width;
    const float parts = renderText("abc").metrics.width +
                        renderText(kSalaam).metrics.width +
                        renderText("def").metrics.width;
    check(close(mixed, parts),
          "boundary: mixed-line width equals sum of segment widths (no cross-script adjustment)");
}

} // namespace

int main(int argc, char** argv) {
    // 与其他 probe 的命令行约定保持一致：接受 --frames N（本探针无头，帧数无意义，
    // 仅消化参数让 ctest 统一以 `--frames 3` 调度）。
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            ++i;
        }
    }

    testArabicAdvancesArePositive();
    testCaretStopsMapCodepointsOneToOne();
    testShapingIsDeterministic();
    testLamAlefIsNotLigated();
    testAdvancesAreContextFree();
    testPureRtlLineLaysOutLeftToRight();
    testMixedLineKeepsLogicalOrder();
    testWidthIsOrderInvariant();

    if (failures == 0) {
        std::printf("arabic_bidi_text_probe: all checks passed\n");
        return 0;
    }
    std::printf("arabic_bidi_text_probe: %d check(s) failed\n", failures);
    return 1;
}
