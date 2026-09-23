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

} // namespace

int main() {
    testNormalizeBasics();
    testNormalizeUtf8();
    if (failures == 0) {
        std::cout << "styled_runs: all checks passed\n";
        return 0;
    }
    std::cerr << "styled_runs: " << failures << " check(s) failed\n";
    return 1;
}
