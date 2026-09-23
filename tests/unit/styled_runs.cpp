// styled_runs: TextRun 归一化（normalizeTextRuns）与顶点着色回归锚。
// 归一化契约（设计 2026-09-23-eui-styled-runs-design.md §3.1）：
//   clamp 到 [0, text.size()] 的 UTF-8 边界、后写覆盖前写、
//   合并相邻同色、丢弃空/反转 run；输出按 start 升序、互不重叠。

#include "core/render/text.h"
#include "core/render/text_types.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "styled_runs: FAILED - " << what << "\n";
        ++failures;
    }
}

bool sameRun(const core::TextRun& run, int start, int end, float r, float g, float b) {
    return run.byteStart == start && run.byteEnd == end &&
           run.color.r == r && run.color.g == g && run.color.b == b;
}

const core::Color kRed{1.0f, 0.2f, 0.2f, 1.0f};
const core::Color kGreen{0.2f, 1.0f, 0.3f, 1.0f};
const core::Color kBlue{0.3f, 0.5f, 1.0f, 1.0f};

void testNormalizeBasics() {
    // 空 runs
    check(core::normalizeTextRuns("hello", {}).empty(), "empty input -> empty output");

    // 越界 clamp
    {
        const auto out = core::normalizeTextRuns("hello", {{-3, 2, kRed}, {4, 99, kGreen}});
        check(out.size() == 2, "clamp: run count");
        check(sameRun(out[0], 0, 2, kRed.r, kRed.g, kRed.b), "clamp: first run start");
        check(sameRun(out[1], 4, 5, kGreen.r, kGreen.g, kGreen.b), "clamp: second run end");
    }

    // 空 run / 反转 run 丢弃
    {
        const auto out = core::normalizeTextRuns("hello", {{2, 2, kRed}, {4, 1, kGreen}, {0, 1, kBlue}});
        check(out.size() == 1, "empty/reversed dropped");
        check(sameRun(out[0], 0, 1, kBlue.r, kBlue.g, kBlue.b), "surviving run");
    }

    // 乱序输入 -> 升序输出
    {
        const auto out = core::normalizeTextRuns("hello", {{3, 5, kRed}, {0, 2, kGreen}});
        check(out.size() == 2, "unsorted: run count");
        check(sameRun(out[0], 0, 2, kGreen.r, kGreen.g, kGreen.b), "unsorted: order[0]");
        check(sameRun(out[1], 3, 5, kRed.r, kRed.g, kRed.b), "unsorted: order[1]");
    }

    // 重叠：后写覆盖前写
    {
        const auto out = core::normalizeTextRuns("hello world", {{0, 5, kRed}, {3, 8, kGreen}});
        check(out.size() == 2, "overlap: run count");
        check(sameRun(out[0], 0, 3, kRed.r, kRed.g, kRed.b), "overlap: earlier clipped");
        check(sameRun(out[1], 3, 8, kGreen.r, kGreen.g, kGreen.b), "overlap: later wins");
    }

    // 相邻同色合并（跨 run 边界）
    {
        const auto out = core::normalizeTextRuns("hello", {{0, 2, kRed}, {2, 4, kRed}, {4, 5, kGreen}});
        check(out.size() == 2, "merge: run count");
        check(sameRun(out[0], 0, 4, kRed.r, kRed.g, kRed.b), "merge: adjacent same color");
        check(sameRun(out[1], 4, 5, kGreen.r, kGreen.g, kGreen.b), "merge: different color kept");
    }

    // 完全同范围：后写全胜
    {
        const auto out = core::normalizeTextRuns("hello", {{0, 5, kRed}, {0, 5, kGreen}});
        check(out.size() == 1, "same range: run count");
        check(sameRun(out[0], 0, 5, kGreen.r, kGreen.g, kGreen.b), "same range: later wins");
    }
}

void testNormalizeUtf8() {
    // "中a" ：中 = 3 字节。劈开 codepoint 的 run 边界向下取整到 codepoint 起点。
    const std::string text = "\xE4\xB8\xAD"
                             "a";
    check(text.size() == 4, "utf8: fixture size");

    // 边界落在 中 的中间（byte 1）→ 向下取整到 0
    {
        const auto out = core::normalizeTextRuns(text, {{1, 4, kRed}});
        check(out.size() == 1 && sameRun(out[0], 0, 4, kRed.r, kRed.g, kRed.b),
              "utf8: mid-codepoint start floors to codepoint start");
    }
    // 结束边界落在 codepoint 中间 → 取整到该 codepoint 之前（该 codepoint 不入 run）
    {
        const auto out = core::normalizeTextRuns(text, {{0, 2, kRed}});
        check(out.empty(), "utf8: mid-codepoint end floors below codepoint");
    }
    // 两个 CJK codepoint 各自完整命中
    {
        const std::string two = "\xE4\xB8\xAD\xE6\x96\x87";   // 中文
        const auto out = core::normalizeTextRuns(two, {{0, 3, kRed}, {3, 6, kGreen}});
        check(out.size() == 2, "utf8: cjk run count");
        check(sameRun(out[0], 0, 3, kRed.r, kRed.g, kRed.b), "utf8: cjk first");
        check(sameRun(out[1], 3, 6, kGreen.r, kGreen.g, kGreen.b), "utf8: cjk second");
    }
}

// ---------------------------------------------------------------------------
// 顶点着色（v0.8 顶点格式 9 float：x,y,u,v,colored,r,g,b,a）
// ---------------------------------------------------------------------------

bool sameColor(const core::Color& left, const core::Color& right) {
    return left.r == right.r && left.g == right.g &&
           left.b == right.b && left.a == right.a;
}

core::Color vertexColor(const std::vector<float>& vertices, std::size_t vertexIndex) {
    const std::size_t base = vertexIndex * 9 + 5;
    return {vertices[base], vertices[base + 1], vertices[base + 2], vertices[base + 3]};
}

void testEmptyRunsMatchLegacyColor() {
    const core::Color base{0.9f, 0.85f, 0.8f, 1.0f};
    core::TextPrimitive plain;
    plain.initialize();
    plain.setText("hello styled world");
    plain.setColor(base);
    plain.prepare();
    const std::vector<float>& vertices = plain.debugVertices();

    check(vertices.size() % 9 == 0, "vertices: 9-float stride");
    check(!vertices.empty(), "vertices: non-empty");
    const std::size_t vertexCount = vertices.size() / 9;
    bool allBase = true;
    for (std::size_t i = 0; i < vertexCount; ++i) {
        if (!sameColor(vertexColor(vertices, i), base)) {
            allBase = false;
            break;
        }
    }
    check(allBase, "empty runs: every vertex color == style color");

    // 覆盖全文且同色的 run == 空 runs（顶点逐字节一致）
    core::TextPrimitive covered;
    covered.initialize();
    covered.setText("hello styled world");
    covered.setColor(base);
    covered.setRuns({{0, 18, base}});
    covered.prepare();
    check(covered.debugVertices() == vertices,
          "full-cover same-color run: byte-identical to empty runs");

    // setRuns 只动顶点色，不动 layout：位置/uv/colored（每顶点前 5 float）不变
    core::TextPrimitive styled;
    styled.initialize();
    styled.setText("hello styled world");
    styled.setColor(base);
    styled.setRuns({{0, 5, kRed}, {6, 11, kGreen}});
    styled.prepare();
    const std::vector<float>& styledVertices = styled.debugVertices();
    bool layoutIdentical = styledVertices.size() == vertices.size();
    if (layoutIdentical) {
        for (std::size_t i = 0; i < vertexCount && layoutIdentical; ++i) {
            for (int c = 0; c < 5; ++c) {
                if (styledVertices[i * 9 + static_cast<std::size_t>(c)] !=
                    vertices[i * 9 + static_cast<std::size_t>(c)]) {
                    layoutIdentical = false;
                    break;
                }
            }
        }
    }
    check(layoutIdentical, "runs: position/uv/colored floats unchanged");
}

void testPerRunVertexColors() {
    const core::Color base{0.7f, 0.7f, 0.7f, 1.0f};
    core::TextPrimitive text;
    text.initialize();
    text.setText("hello world");   // 10 个可见 glyph（空格不入 line.glyphs）
    text.setColor(base);
    text.setRuns({{0, 5, kRed}});   // "hello" 红，其余默认色
    text.prepare();
    const std::vector<float>& vertices = text.debugVertices();

    const std::size_t vertexCount = vertices.size() / 9;
    check(vertexCount == 10 * 6, "per-run: vertex count (10 glyphs x 6)");
    bool colorsCorrect = true;
    for (std::size_t i = 0; i < vertexCount; ++i) {
        const core::Color expected = i < 5 * 6 ? kRed : base;
        if (!sameColor(vertexColor(vertices, i), expected)) {
            colorsCorrect = false;
            break;
        }
    }
    check(colorsCorrect, "per-run: run glyphs colored, others default");

    // setColor 现在必须触发顶点重建（颜色烘焙进顶点）
    const core::Color recolored{0.1f, 0.2f, 0.3f, 1.0f};
    text.setColor(recolored);
    text.prepare();
    const std::vector<float>& rebuilt = text.debugVertices();
    const bool rebuiltColor = !rebuilt.empty() &&
        sameColor(vertexColor(rebuilt, 0), kRed) &&
        sameColor(vertexColor(rebuilt, 5 * 6), recolored);
    check(rebuiltColor, "setColor invalidates vertices (baked color rebuilt)");
}

void testCjkRunBoundaries() {
    const std::string text = "\xE4\xB8\xAD\xE6\x96\x87"
                             "abc";   // 中文abc
    const core::Color white{1.0f, 1.0f, 1.0f, 1.0f};
    core::TextPrimitive primitive;
    primitive.initialize();
    primitive.setText(text);
    primitive.setColor(white);
    primitive.setRuns({{0, 6, kRed}});   // 覆盖 "中文"
    primitive.prepare();
    const std::vector<float>& vertices = primitive.debugVertices();
    const std::size_t vertexCount = vertices.size() / 9;
    // 5 个可见 glyph（中文abc）；前 2 个 glyph（12 顶点）应为红色。
    check(vertexCount == 5 * 6, "cjk: vertex count");
    const bool cjkColored = sameColor(vertexColor(vertices, 0), kRed) &&
                            sameColor(vertexColor(vertices, 11), kRed) &&
                            sameColor(vertexColor(vertices, 12), white);
    check(cjkColored, "cjk: multibyte run colors exactly its codepoints");
}

} // namespace

int main() {
    testNormalizeBasics();
    testNormalizeUtf8();
    testEmptyRunsMatchLegacyColor();
    testPerRunVertexColors();
    testCjkRunBoundaries();
    if (failures == 0) {
        std::cout << "styled_runs: all checks passed\n";
        return 0;
    }
    std::cerr << "styled_runs: " << failures << " check(s) failed\n";
    return 1;
}
