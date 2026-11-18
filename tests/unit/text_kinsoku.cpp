// text_kinsoku: CJK 行首禁则（kinsoku）拉回法契约（DevDesk need324 波 1）。
//
// 设计约束（2026-11-18，调研 docs/research/2026-11-18-cjk-line-break-prohibition.md）：
//   - wrap 断行成立且即将起行码点 ∈ 禁则集（全角 only，14 码点）→ 拉回本行
//     行尾，行宽允许超 maxWidth ≤1 个标点宽；连续拉回钳制 ≤2 次，第 3 个
//     放行换行（接受禁则起行的极端 case）。
//   - 半角永不入集：纯 ASCII 文本任何行宽 ≤ maxWidth（无拉回溢出）。
//   - measureTextSize 与 rebuildLayout 逐位一致：同输入行数与最大行宽相同
//     （卡高估算不与渲染行数漂移，text_measure.cpp 既有契约）。

#include "core/render/text.h"
#include "core/render/text_kinsoku.h"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "text_kinsoku: FAILED - " << what << "\n";
        ++failures;
    }
}

bool close(float left, float right) {
    return std::fabs(left - right) <= 0.001f;
}

// 默认 fontSize = 16、未设 lineHeight → lineHeight = 16 * 1.2。
constexpr float kDefaultLineHeight = 16.0f * 1.2f;

std::size_t lineCountOf(const core::Vec2& size) {
    return static_cast<std::size_t>(size.y / kDefaultLineHeight + 0.5f);
}

core::Vec2 renderWrapped(const std::string& text, float maxWidth) {
    core::TextPrimitive primitive;
    primitive.initialize();
    primitive.setText(text);
    primitive.setMaxWidth(maxWidth);
    primitive.setWrap(true);
    primitive.prepare();
    return primitive.measuredSize();
}

core::Vec2 measureWrapped(const std::string& text, float maxWidth) {
    core::TextStyle style;
    style.text = text;
    style.maxWidth = maxWidth;
    style.wrap = true;
    return core::TextPrimitive::measureTextSize(style);
}

// ---------------------------------------------------------------------------
// 谓词与 UTF-8 解码
// ---------------------------------------------------------------------------

void testPredicateFullwidthSet() {
    const unsigned int fullwidth[] = {
        0x3001u, 0x3002u, 0xFF0Cu, 0xFF01u, 0xFF1Fu, 0xFF1Au, 0xFF1Bu,
        0xFF09u, 0x3009u, 0x300Bu, 0x300Du, 0x300Fu, 0x3011u, 0x2026u
    };
    for (const unsigned int cp : fullwidth) {
        check(core::text_detail::isProhibitedLineStart(cp),
              "set: fullwidth punctuation member is prohibited");
    }
}

void testPredicateHalfwidthExcluded() {
    // 半角红线：URL（:// .）与英文混排依赖这些字符可自由起行。
    const unsigned int halfwidth[] = {
        ',', '.', ';', ':', '!', '?', ')', ']', '}', '-', '/', 'a', 'Z', '0'
    };
    for (const unsigned int cp : halfwidth) {
        check(!core::text_detail::isProhibitedLineStart(cp),
              "set: halfwidth never prohibited");
    }
    // 行尾禁则（开括号/破折号）不在 need324 范围：可正常起行。
    const unsigned int notLineStart[] = {0xFF08u, 0x300Cu, 0x3010u, 0x2014u};
    for (const unsigned int cp : notLineStart) {
        check(!core::text_detail::isProhibitedLineStart(cp),
              "set: opening brackets not prohibited (no line-end kinsoku)");
    }
}

void testDecodeUtf8At() {
    // 'A' + U+3002（3 字节）+ 'Z'。
    const std::string text = "A\xE3\x80\x82Z";
    check(core::text_detail::decodeUtf8At(text, 0) == 'A', "decode: ASCII passthrough");
    check(core::text_detail::decodeUtf8At(text, 1) == 0x3002u, "decode: 3-byte CJK punctuation");
    check(core::text_detail::decodeUtf8At(text, 4) == 'Z', "decode: offset after multibyte");
    check(core::text_detail::decodeUtf8At(text, 99) == 0u, "decode: out of range returns 0");
    check(core::text_detail::decodeUtf8At(text, 2) == 0u, "decode: mid-sequence start returns 0");
}

// ---------------------------------------------------------------------------
// 拉回法行为
// ---------------------------------------------------------------------------

void testProhibitedStartPulledBack() {
    // 5 个全角「中」+ 全角逗号；maxWidth 恰容纳 5 字 → 逗号本应起行，
    // 拉回后单行、行宽 = 全文宽（悬挂超 maxWidth ≤1 标点宽）。
    const std::string text =
        "\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xEF\xBC\x8C";
    const core::TextPrimitive::TextMetrics metrics = core::TextPrimitive::measureTextMetrics(text);
    check(metrics.caretX.size() == 7u, "pull: 6 codepoints -> 7 caret stops");
    const float maxWidth = metrics.caretX[5];

    const core::Vec2 rendered = renderWrapped(text, maxWidth);
    check(lineCountOf(rendered) == 1u, "pull: comma pulled back -> single line");
    check(close(rendered.x, metrics.caretX[6]),
          "pull: line width == full text width (hangs past maxWidth)");
    check(rendered.x > maxWidth, "pull: overflow by one punctuation width is allowed");

    const core::Vec2 measured = measureWrapped(text, maxWidth);
    check(lineCountOf(measured) == 1u && close(measured.x, metrics.caretX[6]),
          "pull: measureTextSize agrees with rendered result");
}

void testConsecutivePullClamped() {
    // 5 个全角「中」+ 4 个全角逗号，maxWidth 恰容纳 5 字：拉回 2 个逗号后
    // 第 3 个起行（钳制 ≤2），不全部拉成单行。
    const std::string text =
        "\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD"
        "\xEF\xBC\x8C\xEF\xBC\x8C\xEF\xBC\x8C\xEF\xBC\x8C";
    const core::TextPrimitive::TextMetrics metrics = core::TextPrimitive::measureTextMetrics(text);
    check(metrics.caretX.size() == 10u, "clamp: 9 codepoints -> 10 caret stops");
    const float maxWidth = metrics.caretX[5];

    const core::Vec2 rendered = renderWrapped(text, maxWidth);
    check(lineCountOf(rendered) == 2u, "clamp: third comma starts line 2 (pull limit 2)");
    check(close(rendered.x, metrics.caretX[7]),
          "clamp: line 1 width == 5 glyphs + exactly 2 pulled commas");
    check(rendered.x < metrics.caretX[9],
          "clamp: not all commas pulled (single-line width would be caretX[9])");

    const core::Vec2 measured = measureWrapped(text, maxWidth);
    check(lineCountOf(measured) == 2u && close(measured.x, metrics.caretX[7]),
          "clamp: measureTextSize agrees with rendered result");
}

void testHalfwidthBreakUnpulled() {
    // 纯 ASCII（断行点落在半角标点附近）：不拉回，任何行宽 ≤ maxWidth。
    const std::string text = "abcdefgh,ijkl.mnop:qrst;uvwx";
    const core::TextPrimitive::TextMetrics metrics = core::TextPrimitive::measureTextMetrics(text);
    const float maxWidth = metrics.caretX[10];

    const core::Vec2 rendered = renderWrapped(text, maxWidth);
    check(lineCountOf(rendered) >= 2u, "halfwidth: wrapping did occur");
    check(rendered.x <= maxWidth + 0.001f,
          "halfwidth: no pull-back overflow (max line width <= maxWidth)");
}

void testUrlWrapUnaffected() {
    const std::string url = "https://example.com/some/long/path?q=1";
    for (const float maxWidth : {40.0f, 80.0f, 120.0f}) {
        const core::Vec2 rendered = renderWrapped(url, maxWidth);
        check(rendered.x <= maxWidth + 0.001f,
              "url: no line exceeds maxWidth (no halfwidth pull)");
        const core::Vec2 measured = measureWrapped(url, maxWidth);
        check(lineCountOf(rendered) == lineCountOf(measured),
              "url: measure/render line count agree");
    }
}

void testMeasureLayoutConsistency() {
    // 混合内容多段（\n、CJK、英文、URL、禁则字符）：拉回/钳制/正常断行并存
    // 时，测量与渲染的行数、最大行宽必须一致（卡高不漂移）。
    const std::string text =
        "\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xEF\xBC\x8C\xE4\xB8\xAD\xE4\xB8\xAD"
        "\xE3\x80\x82\nEN words here\xE3\x80\x82url https://x.cn/a/b";
    for (const float maxWidth : {30.0f, 60.0f, 100.0f, 200.0f}) {
        const core::Vec2 rendered = renderWrapped(text, maxWidth);
        const core::Vec2 measured = measureWrapped(text, maxWidth);
        check(lineCountOf(rendered) == lineCountOf(measured),
              "consistency: line counts equal");
        check(close(rendered.x, measured.x),
              "consistency: max line widths equal");
    }
}

} // namespace

int main() {
    testPredicateFullwidthSet();
    testPredicateHalfwidthExcluded();
    testDecodeUtf8At();
    testProhibitedStartPulledBack();
    testConsecutivePullClamped();
    testHalfwidthBreakUnpulled();
    testUrlWrapUnaffected();
    testMeasureLayoutConsistency();

    if (failures == 0) {
        std::cout << "text_kinsoku: all checks passed\n";
        return 0;
    }
    std::cerr << "text_kinsoku: " << failures << " check(s) failed\n";
    return 1;
}
