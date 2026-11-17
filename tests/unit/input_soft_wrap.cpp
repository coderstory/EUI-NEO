// softWrap 旋钮（need263 F-19 换行关断，DevDesk notepad 消费）：
// - softWrap=false：超宽逻辑行不切软段（整行单显示段）、横向滚动跟随光标
// - softWrap=true：回归不变（按视口切软段、horizontalScroll 恒 0）
// - 开关翻转：布局缓存按 cachedSoftWrap 失效重建
// 注意：文本元素 frame.x 是窗口绝对坐标（含 viewport 的 inset 偏移），
// 横滚断言一律相对 soft.textViewport.frame.x 比较。
#include "components/input.h"
#include "components/input_model.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[softWrap] " << message << "\n";
        ++failures;
    }
}

} // namespace

int main() {
    using Model = components::input_detail::InputModel;

    constexpr float inset = 12.0f;
    constexpr float fontSize = 17.0f;
    constexpr float width = 360.0f;
    constexpr float height = 116.0f;
    const float viewportWidth = width - inset * 2.0f;
    const float viewportHeight = height - inset * 2.0f;

    const std::string wideLine(120, 'a');
    const std::string twoLogicalLines = wideLine + "\nsecond";

    // --- softWrap=false：单逻辑行 → 单显示段 + 横向滚动跟随光标 ---
    Model::InputState noWrap;
    noWrap.text = wideLine;
    noWrap.softWrap = false;
    noWrap.textRevision = 1;
    noWrap.cursor = static_cast<int>(wideLine.size());
    noWrap.followCaret = true;
    const Model::InputLayout noWrapLayout = Model::InputLayout::build(
        noWrap, viewportWidth, viewportHeight, width, inset, inset,
        fontSize * 1.2f, "monospace", fontSize, true);
    expect(noWrap.cachedLines.size() == 1,
           "softWrap=false wide logical line must stay a single display segment, got " +
               std::to_string(noWrap.cachedLines.size()));
    expect(noWrapLayout.lines->size() == 1, "layout must reference the single segment");
    expect(noWrap.horizontalScroll > 0.0f,
           "caret at line end must drive horizontal scroll, got " + std::to_string(noWrap.horizontalScroll));
    expect(std::fabs(noWrapLayout.scroll - noWrap.horizontalScroll) < 0.001f,
           "layout.scroll must mirror state.horizontalScroll");
    expect(std::fabs(noWrapLayout.cursorX - (inset + noWrapLayout.cursorPixel - noWrapLayout.scroll)) < 0.01f,
           "cursorX must account for horizontal scroll");

    // --- softWrap=true 回归：同一文本按视口切软段、horizontalScroll 恒 0 ---
    Model::InputState wrapped;
    wrapped.text = wideLine;
    wrapped.textRevision = 1;
    wrapped.cursor = static_cast<int>(wideLine.size());
    wrapped.followCaret = true;
    const Model::InputLayout wrappedLayout = Model::InputLayout::build(
        wrapped, viewportWidth, viewportHeight, width, inset, inset,
        fontSize * 1.2f, "monospace", fontSize, true);
    expect(wrapped.cachedLines.size() > 1,
           "softWrap=true wide logical line must split into soft segments, got " +
               std::to_string(wrapped.cachedLines.size()));
    expect(wrapped.horizontalScroll == 0.0f && wrappedLayout.scroll == 0.0f,
           "softWrap=true must keep horizontalScroll at 0");

    // --- 开关翻转：缓存按 softWrap 失效重建（行切分收敛到新口径） ---
    Model::InputState toggled;
    toggled.text = twoLogicalLines;
    toggled.softWrap = false;
    toggled.textRevision = 1;
    Model::InputLayout::build(toggled, viewportWidth, viewportHeight, width, inset, inset,
                              fontSize * 1.2f, "monospace", fontSize, true);
    expect(toggled.cachedLines.size() == 2,
           "softWrap=false two logical lines must yield two display lines, got " +
               std::to_string(toggled.cachedLines.size()));
    toggled.softWrap = true;
    Model::InputLayout::build(toggled, viewportWidth, viewportHeight, width, inset, inset,
                              fontSize * 1.2f, "monospace", fontSize, true);
    expect(toggled.cachedLines.size() > 2,
           "toggling softWrap back on must re-split via cache invalidation, got " +
               std::to_string(toggled.cachedLines.size()));

    // --- UI 级：builder 旋钮接线（文本元素位置随横滚偏移；软段数量变化） ---
    {
        core::dsl::Ui ui;
        const auto frame = [&ui, &wideLine](bool wrap) {
            ui.begin("input.softwrap");
            components::input(ui, "soft")
                .size(180.0f, 100.0f)
                .fontSize(20.0f)
                .multiline()
                .softWrap(wrap)
                .value(wideLine)
                .build();
            ui.end();
            ui.layout(180.0f, 100.0f);
        };

        frame(false);
        const core::dsl::Element* viewport = ui.find("soft.textViewport");
        const core::dsl::Element* firstLine = ui.find("soft.text.0");
        expect(viewport != nullptr && firstLine != nullptr,
               "softWrap=false must render the logical line");
        expect(ui.find("soft.text.1") == nullptr,
               "softWrap=false wide logical line must not spawn soft segments");
        expect(firstLine != nullptr && viewport != nullptr &&
                   std::fabs(firstLine->frame.x - viewport->frame.x) < 0.01f,
               "caret at 0 must keep horizontal scroll at 0");

        auto& state = ui.state<Model::InputState>("soft");
        state.cursor = static_cast<int>(wideLine.size());
        state.followCaret = true;
        frame(false);
        viewport = ui.find("soft.textViewport");
        firstLine = ui.find("soft.text.0");
        expect(firstLine != nullptr && viewport != nullptr &&
                   firstLine->frame.x < viewport->frame.x - 1.0f,
               "caret at line end must shift the line left via horizontal scroll, got " +
                   std::to_string(firstLine ? firstLine->frame.x : 0.0f));

        state.cursor = 0;
        frame(true);
        expect(ui.find("soft.text.1") != nullptr,
               "toggling softWrap on must re-split into multiple segments");

        frame(false);
        expect(ui.find("soft.text.1") == nullptr,
               "toggling softWrap off must collapse back to a single segment");
    }

    if (failures > 0) {
        std::cerr << "input_soft_wrap: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "input_soft_wrap: all checks passed\n";
    return 0;
}
