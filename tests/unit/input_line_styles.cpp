// input_line_styles: styled-runs Phase B——input 行级高亮回调。
// 覆盖（设计 2026-09-23-eui-styled-runs-design.md §5.2 / §4.3）：
//   1. lineLocalRuns：全文档坐标 runs → 行内坐标的裁剪/平移/归一化
//   2. lineRuns 行缓存：(lineNo, textRevision, styleRevision) 三元组命中/失效
//   3. UI 集成：仅可视行回调、行元素 runs/dirtyKey、无 provider 时与旧路径一致
//   4. 缓存窗口：滚出视口逐出、滚回重算、styleRevision 触发全量重高亮
//   5. IME 预编辑快照缓存隔离 + styleRevision 同步
//   6. 性能探针：万行文档击键全链（重排 + 高亮 + 指纹），只记录不设门槛

#include "components/input.h"
#include "components/input_model.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

using Model = components::input_detail::InputModel;
using LineStylesProvider = components::input_detail::LineStylesProvider;
using Runs = std::vector<core::TextRun>;

const core::Color kRed{1.0f, 0.2f, 0.2f, 1.0f};
const core::Color kGreen{0.2f, 1.0f, 0.3f, 1.0f};
const core::Color kBlue{0.3f, 0.5f, 1.0f, 1.0f};

int testLineLocalRuns() {
    // 裁剪 + 平移：只有与 [6,11) 相交的 run 幸存，坐标减 lineStart。
    {
        const Runs local = Model::lineLocalRuns(
            {{0, 5, kRed}, {5, 11, kGreen}, {12, 20, kRed}}, "world", 6, 11);
        if (local.size() != 1 || local[0].byteStart != 0 || local[0].byteEnd != 5 ||
            local[0].color.g != kGreen.g) {
            std::cerr << "lineLocalRuns: clip/shift failed\n";
            return 1;
        }
    }
    // 行内重叠：后写覆盖前写（归一化），行尾 clamp 到行长（"world" = 5 字节）。
    {
        const Runs local = Model::lineLocalRuns({{6, 9, kRed}, {7, 12, kGreen}}, "world", 6, 12);
        if (local.size() != 2 ||
            local[0].byteStart != 0 || local[0].byteEnd != 1 || local[0].color.r != kRed.r ||
            local[1].byteStart != 1 || local[1].byteEnd != 5 || local[1].color.g != kGreen.g) {
            std::cerr << "lineLocalRuns: overlap normalize failed\n";
            return 2;
        }
    }
    // 越过行尾的 run 裁到行长；空行上的 run 全部丢弃。
    {
        const Runs local = Model::lineLocalRuns({{6, 999, kBlue}}, "world", 6, 11);
        if (local.size() != 1 || local[0].byteEnd != 5) {
            std::cerr << "lineLocalRuns: tail clamp failed\n";
            return 3;
        }
        const Runs none = Model::lineLocalRuns({{6, 12, kBlue}}, "", 6, 6);
        if (!none.empty()) {
            std::cerr << "lineLocalRuns: empty line failed\n";
            return 4;
        }
    }
    // 相邻同色合并（归一化）。
    {
        const Runs local = Model::lineLocalRuns({{6, 8, kRed}, {8, 11, kRed}}, "world", 6, 11);
        if (local.size() != 1 || local[0].byteStart != 0 || local[0].byteEnd != 5) {
            std::cerr << "lineLocalRuns: same-color merge failed\n";
            return 5;
        }
    }
    return 0;
}

int testLineRunsCache() {
    Model::InputState state;
    state.text = "a\nb\nc";
    ++state.textRevision;
    int calls = 0;
    LineStylesProvider provider = [&](const std::string& text, int start, int) {
        ++calls;
        return Runs{{start, start + static_cast<int>(text.size()), kRed}};
    };
    // 未命中 → 回调一次；重复请求同 (lineNo, textRevision, styleRevision) → 命中。
    Model::lineRuns(state, provider, "a", 0, 0);
    Model::lineRuns(state, provider, "a", 0, 0);
    if (calls != 1) {
        std::cerr << "lineRuns: cache hit failed (" << calls << " calls)\n";
        return 1;
    }
    // textRevision 变化 → 失效重算。
    ++state.textRevision;
    Model::lineRuns(state, provider, "a", 0, 0);
    if (calls != 2) {
        std::cerr << "lineRuns: textRevision invalidation failed\n";
        return 2;
    }
    // styleRevision 变化 → 失效重算（使用方改高亮规则）。
    ++state.styleRevision;
    Model::lineRuns(state, provider, "a", 0, 0);
    if (calls != 3) {
        std::cerr << "lineRuns: styleRevision invalidation failed\n";
        return 3;
    }
    // 不同行号各自独立缓存。
    Model::lineRuns(state, provider, "b", 2, 1);
    if (calls != 4) {
        std::cerr << "lineRuns: per-line isolation failed\n";
        return 4;
    }
    // provider 为空 → 恒空 runs，不回调。
    Model::InputState bare;
    if (!Model::lineRuns(bare, nullptr, "x", 0, 0).empty()) {
        std::cerr << "lineRuns: null provider must yield empty runs\n";
        return 5;
    }
    // 指纹：同 runs 同指纹、异 runs 异指纹。
    const Runs a{{0, 3, kRed}};
    const Runs b{{0, 3, kGreen}};
    const Runs c{{0, 3, kRed}};
    if (Model::runsFingerprint(a) != Model::runsFingerprint(c) ||
        Model::runsFingerprint(a) == Model::runsFingerprint(b) ||
        Model::runsFingerprint(a) == Model::runsFingerprint({})) {
        std::cerr << "runsFingerprint: discrimination failed\n";
        return 6;
    }
    // 逐出：窗口外的行被清除。
    Model::pruneLineRuns(state, 1, 2);
    if (state.cachedLineRuns.count(0) != 0 || state.cachedLineRuns.count(1) != 1) {
        std::cerr << "pruneLineRuns: eviction failed\n";
        return 7;
    }
    return 0;
}

int testUiIntegration() {
    core::dsl::Ui ui;
    std::string value;
    for (int i = 0; i < 40; ++i) {
        value += "line " + std::to_string(i) + " padded text\n";
    }
    int calls = 0;
    std::vector<int> calledLines;
    LineStylesProvider provider = [&](const std::string& text, int start, int lineNo) {
        ++calls;
        calledLines.push_back(lineNo);
        // 行首 2 字节红 + 跨行尾的蓝（验证裁剪）；短行/空行自然裁空。
        return Runs{{start, start + 2, kRed}, {start + 5, start + 999, kBlue}};
    };
    bool withProvider = true;
    const auto compose = [&] {
        ui.begin("page");
        auto builder = components::input(ui, "field")
                            .size(320.0f, 160.0f)
                            .inset(10.0f)
                            .fontSize(16.0f)
                            .multiline()
                            .value(value);
        if (withProvider) {
            builder.lineStyles(provider);
        }
        builder.build();
        ui.end();
        ui.layout(320.0f, 160.0f);
    };

    compose();
    Model::InputState& state = ui.state<Model::InputState>("field");
    // 首帧 followCaret 把视口滚到末行；钉住窗口后再开始计数。
    state.followCaret = false;
    state.verticalScroll = 0.0f;
    calls = 0;
    calledLines.clear();
    compose();
    // textHeight = 140, lineH = 19.2 → firstLine=0, endLine=ceil(140/19.2)+1=9，
    // y 裁剪后可视行 = 0..7（8 行）。
    if (calls != 8) {
        std::cerr << "ui: visible-line callback count = " << calls << " (want 8)\n";
        return 1;
    }
    for (int i = 0; i < 8; ++i) {
        if (i >= static_cast<int>(calledLines.size()) || calledLines[static_cast<size_t>(i)] != i) {
            std::cerr << "ui: callback line order/content mismatch\n";
            return 2;
        }
    }
    // 行元素：text = 行文本；runs = 裁剪平移后的行内坐标；dirtyKey 带 runs 指纹。
    const core::dsl::Element* line0 = ui.find("field.text.0");
    if (!line0 || line0->text != "line 0 padded text") {
        std::cerr << "ui: line element text mismatch\n";
        return 3;
    }
    if (line0->textRuns.size() != 2 ||
        line0->textRuns[0].byteStart != 0 || line0->textRuns[0].byteEnd != 2 ||
        line0->textRuns[1].byteStart != 5 || line0->textRuns[1].byteEnd != 18) {
        std::cerr << "ui: line element runs not shifted/clipped to line-local coords\n";
        return 4;
    }
    if (line0->dirtyKey.find("|r") == std::string::npos) {
        std::cerr << "ui: line dirtyKey missing runs fingerprint\n";
        return 5;
    }
    // 无变化重组合 → 全部缓存命中，零回调。
    calls = 0;
    compose();
    if (calls != 0) {
        std::cerr << "ui: recompose should hit cache, calls = " << calls << "\n";
        return 6;
    }
    // 滚动 2 行：可视窗口变 [2, 10)，只有滚入的新行 8/9 重算，2..7 命中。
    state.verticalScroll = 2.0f * 19.2f;
    calls = 0;
    calledLines.clear();
    compose();
    if (calls != 2 || calledLines.front() != 8 || calledLines.back() != 9) {
        std::cerr << "ui: scroll should only recompute newly visible lines, calls = " << calls << "\n";
        return 7;
    }
    // 缓存被逐出限制在可视窗口：滚出视口的 0/1 已清除。
    if (state.cachedLineRuns.count(0) != 0 || state.cachedLineRuns.count(1) != 0 ||
        state.cachedLineRuns.size() > 9) {
        std::cerr << "ui: cache not pruned to visible window\n";
        return 8;
    }
    // 滚回顶部 → 原行被逐出过，重新回调（改行→滚走→滚回序列）。
    state.verticalScroll = 0.0f;
    calls = 0;
    compose();
    if (calls != 2) {  // 0/1 重算，2..7 命中
        std::cerr << "ui: scroll-back should only recompute evicted lines, calls = " << calls << "\n";
        return 9;
    }
    // 编辑文本（value 变化 → textRevision++）→ 全部可视行重算。
    value.replace(0, 4, "LINE");
    state.verticalScroll = 0.0f;
    calls = 0;
    compose();
    if (calls != 8) {
        std::cerr << "ui: text edit should invalidate all visible lines, calls = " << calls << "\n";
        return 10;
    }
    // 行文本变了 → 行元素 runs 跟随新行文本。
    if (ui.find("field.text.0")->text.substr(0, 4) != "LINE") {
        std::cerr << "ui: edited line text not reflected\n";
        return 11;
    }
    // styleRevision 自增 → 全部可视行重算（规则变化）。
    calls = 0;
    ++state.styleRevision;
    compose();
    if (calls != 8) {
        std::cerr << "ui: styleRevision bump should recompute all visible lines\n";
        return 12;
    }
    // 无 provider：runs 为空、dirtyKey 与旧路径一致（无 "|r" 成分）。
    withProvider = false;
    compose();
    const core::dsl::Element* plain = ui.find("field.text.0");
    if (!plain || !plain->textRuns.empty() ||
        plain->dirtyKey.find("|r") != std::string::npos) {
        std::cerr << "ui: no-provider path must be byte-identical to legacy\n";
        return 13;
    }
    // provider 返回空：runs 为空（回默认色），但 key 含指纹成分。
    // 换 provider 等价于换高亮规则，需自增 styleRevision 使缓存失效。
    withProvider = true;
    LineStylesProvider emptyProvider = [](const std::string&, int, int) { return Runs{}; };
    ++state.styleRevision;
    ui.begin("page");
    components::input(ui, "field")
        .size(320.0f, 160.0f).inset(10.0f).fontSize(16.0f).multiline()
        .value(value).lineStyles(emptyProvider).build();
    ui.end();
    ui.layout(320.0f, 160.0f);
    const core::dsl::Element* empty = ui.find("field.text.0");
    if (!empty || !empty->textRuns.empty() ||
        empty->dirtyKey.find("|r") == std::string::npos) {
        std::cerr << "ui: empty-runs provider should keep legacy rendering path\n";
        return 14;
    }
    return 0;
}

int testPreeditIsolation() {
    Model::InputState state;
    state.text = "alpha beta";
    state.cursor = 6;
    state.selectionStart = 6;
    state.selectionEnd = 6;
    state.compositionText = "中文";
    int mainCalls = 0;
    int displayCalls = 0;
    LineStylesProvider mainProvider = [&](const std::string&, int, int) {
        ++mainCalls;
        return Runs{};
    };
    LineStylesProvider displayProvider = [&](const std::string&, int, int) {
        ++displayCalls;
        return Runs{};
    };
    Model::InputState& display = Model::displayState(state, true);
    if (display.text != "alpha 中文beta") {
        std::cerr << "preedit: unexpected display text '" << display.text << "'\n";
        return 1;
    }
    // 正文与预编辑快照各自独立缓存（display 文本坐标系不同，不能共享）。
    Model::lineRuns(state, mainProvider, "alpha 中文beta", 0, 0);
    Model::lineRuns(display, displayProvider, "alpha 中文beta", 0, 0);
    Model::lineRuns(state, mainProvider, "alpha 中文beta", 0, 0);
    Model::lineRuns(display, displayProvider, "alpha 中文beta", 0, 0);
    if (mainCalls != 1 || displayCalls != 1) {
        std::cerr << "preedit: caches must be independent per snapshot\n";
        return 2;
    }
    // 同 revision 重复请求 → 双方命中。
    Model::lineRuns(state, mainProvider, "alpha 中文beta", 0, 0);
    Model::lineRuns(*state.preedit, displayProvider, "alpha 中文beta", 0, 0);
    if (mainCalls != 1 || displayCalls != 1) {
        std::cerr << "preedit: same-revision should hit\n";
        return 4;
    }
    // styleRevision 随正文快照同步到预编辑，并使其缓存失效（规则变化）。
    state.styleRevision = 5;
    Model::displayState(state, true);
    if (state.preedit->styleRevision != 5) {
        std::cerr << "preedit: styleRevision not synced\n";
        return 3;
    }
    displayCalls = 0;
    Model::lineRuns(*state.preedit, displayProvider, "alpha 中文测试", 0, 0);
    if (displayCalls != 1) {
        std::cerr << "preedit: synced styleRevision must invalidate display cache\n";
        return 6;
    }
    // 组合串变化 → display.textRevision 变化 → 缓存失效。
    state.compositionText = "中文测试";
    Model::InputState& next = Model::displayState(state, true);
    if (&next != state.preedit.get()) {
        std::cerr << "preedit: snapshot should be reused while composing\n";
        return 5;
    }
    displayCalls = 0;
    Model::lineRuns(*state.preedit, displayProvider, "alpha 中文测试beta", 0, 0);
    if (displayCalls != 1) {
        std::cerr << "preedit: composition change must invalidate display cache\n";
        return 7;
    }
    return 0;
}

int testPerformanceProbe() {
    using Clock = std::chrono::steady_clock;
    Model::InputState state;
    for (int i = 0; i < 10000; ++i) {
        state.text += "# heading line " + std::to_string(i) + " with `code span` and **bold** 中文混排\n";
    }
    ++state.textRevision;
    const float lineHeight = 19.2f;
    const float viewportHeight = 500.0f;
    state.cursor = Model::clampUtf8Boundary(state.text, static_cast<int>(state.text.size() / 2));
    Model::clearSelection(state);
    state.followCaret = false;
    state.verticalScroll = 5000.0f * lineHeight;  // 视口钉在文档中部

    LineStylesProvider provider = [](const std::string& text, int start, int) {
        Runs runs;
        const auto push = [&](int a, int b, const core::Color& color) {
            if (a < b) runs.push_back({start + a, start + b, color});
        };
        if (!text.empty() && text[0] == '#') {
            const size_t marks = text.find_first_not_of('#');
            const size_t markEnd = marks == std::string::npos ? text.size() : marks;
            push(0, static_cast<int>(markEnd), kRed);
            push(static_cast<int>(markEnd) + 1, static_cast<int>(text.size()), kGreen);
        }
        for (size_t i = 0; i + 1 < text.size(); ++i) {
            if (text[i] == '`') {
                const size_t close = text.find('`', i + 1);
                if (close != std::string::npos) {
                    push(static_cast<int>(i), static_cast<int>(i + 1), kBlue);
                    push(static_cast<int>(i + 1), static_cast<int>(close), kRed);
                    push(static_cast<int>(close), static_cast<int>(close + 1), kBlue);
                    i = close;
                }
            }
        }
        return runs;
    };

    const auto visibleRun = [&](Model::InputState& snapshot) {
        const std::vector<Model::TextLine>& lines = snapshot.cachedLines;
        const auto firstLine = static_cast<std::size_t>(std::max(0.0f, std::floor(snapshot.verticalScroll / lineHeight)));
        const auto endLine = std::min(lines.size(), static_cast<std::size_t>(std::ceil((snapshot.verticalScroll + viewportHeight) / lineHeight)) + 1);
        int highlighted = 0;
        for (std::size_t index = firstLine; index < endLine; ++index) {
            const auto& line = lines[index];
            const std::string lineText = snapshot.text.substr(static_cast<std::size_t>(line.start),
                                                              static_cast<std::size_t>(line.end - line.start));
            const Runs& docRuns = Model::lineRuns(snapshot, provider, lineText, line.start, static_cast<int>(index));
            const Runs local = Model::lineLocalRuns(docRuns, lineText, line.start, line.end);
            const unsigned long long fingerprint = Model::runsFingerprint(local);
            (void)fingerprint;
            ++highlighted;
        }
        return highlighted;
    };

    auto start = Clock::now();
    Model::InputLayout::build(state, 480.0f, viewportHeight, 500.0f, 10.0f, 10.0f, lineHeight, "monospace", 16.0f, true);
    const double initialLayoutMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    start = Clock::now();
    const int highlighted = visibleRun(state);
    const double initialHighlightMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    double totalMs = 0.0;
    double worstMs = 0.0;
    double editMs = 0.0;
    double layoutMs = 0.0;
    double highlightMs = 0.0;
    constexpr int kKeystrokes = 200;
    for (int i = 0; i < kKeystrokes; ++i) {
        start = Clock::now();
        Model::pushUndoState(state);
        Model::insertAtCursor(state, "x");
        const double editStep = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        start = Clock::now();
        Model::InputLayout::build(state, 480.0f, viewportHeight, 500.0f, 10.0f, 10.0f, lineHeight, "monospace", 16.0f, true);
        const double layoutStep = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        start = Clock::now();
        const int lines = visibleRun(state);
        const double highlightStep = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        if (lines != highlighted) {
            std::cerr << "probe: visible line count changed across keystrokes\n";
            return 1;
        }
        const double ms = editStep + layoutStep + highlightStep;
        totalMs += ms;
        worstMs = std::max(worstMs, ms);
        editMs += editStep;
        layoutMs += layoutStep;
        highlightMs += highlightStep;
    }
    std::cout << "[perf] input_line_styles: 10000 lines, " << highlighted
              << " visible highlighted/keystroke | initial layout " << initialLayoutMs
              << " ms, initial highlight " << initialHighlightMs << " ms | keystroke avg "
              << totalMs / kKeystrokes << " ms, max " << worstMs << " ms (budget 8 ms, design §7)"
              << " | breakdown avg: edit(undo+insert) " << editMs / kKeystrokes
              << " ms, relayout " << layoutMs / kKeystrokes
              << " ms, highlight(callback+shift+fingerprint) " << highlightMs / kKeystrokes << " ms\n";
    return 0;
}

} // namespace

int main() {
    if (const int code = testLineLocalRuns()) return code;
    if (const int code = testLineRunsCache()) return code;
    if (const int code = testUiIntegration()) return code;
    if (const int code = testPreeditIsolation()) return code;
    if (const int code = testPerformanceProbe()) return code;
    std::cout << "input_line_styles: all checks passed\n";
    return 0;
}
