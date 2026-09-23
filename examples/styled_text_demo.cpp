// styled_text_demo: TextBuilder.runs() 行内多色文本（styled-runs Phase A）
// + input.lineStyles() 编辑器行级高亮回调（Phase B）。
// 人工点验项：一行内多色不换批、色块边界精确落字符、input 行级高亮模拟
//（编辑器场景：标记弱化色 + 行内代码色 + 链接色）；编辑器内光标/选区/中文
// IME 与高亮共存，滚动只重算新进入视口的行，切换配色按钮验证 styleRevision。
#include "eui_neo.h"

#include <string>
#include <vector>

namespace app {

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("Styled Text Runs")
        .pageId("styled-text")
        .clearColor({0.07f, 0.08f, 0.10f, 1.0f})
        .windowSize(900, 740)
        .fps(60.0);
    return config;
}

namespace {

const eui::Color kTitle{0.94f, 0.97f, 1.0f, 1.0f};
const eui::Color kLabel{0.62f, 0.68f, 0.78f, 1.0f};
const eui::Color kBody{0.90f, 0.92f, 0.95f, 1.0f};
// 行级高亮模拟（markdown 编辑器色系）
const eui::Color kMark{0.55f, 0.60f, 0.70f, 1.0f};      // 标记符号（# ** ` 等）弱化
const eui::Color kCode{0.86f, 0.74f, 0.98f, 1.0f};      // 行内代码
const eui::Color kLink{0.52f, 0.76f, 1.00f, 1.0f};      // 链接
const eui::Color kHeading{0.99f, 0.84f, 0.55f, 1.0f};   // 标题文字
const eui::Color kQuote{0.65f, 0.85f, 0.70f, 1.0f};     // 引用正文
const eui::Color kPlain{0.90f, 0.92f, 0.95f, 1.0f};     // 未命中 run 的默认色

// helper：按 UTF-8 字节长度推进 codepoint 起点（本 demo 文案硬编码 run 偏移，
// 也可以在运行时用同样规则计算）。
int advanceUtf8(const std::string& text, int from) {
    int index = from;
    while (index < static_cast<int>(text.size())) {
        const unsigned char byte = static_cast<unsigned char>(text[static_cast<std::size_t>(index)]);
        if (index > from && (byte & 0xC0) != 0x80) {
            break;
        }
        ++index;
    }
    return index;
}

int byteOffset(const std::string& text, int codepointIndex) {
    int offset = 0;
    for (int i = 0; i < codepointIndex; ++i) {
        offset = advanceUtf8(text, offset);
    }
    return offset;
}

// ---- Phase B：input.lineStyles() 编辑器行级高亮 ----

struct HighlightPalette {
    eui::Color mark;     // 标记符号（# ** ` > -）
    eui::Color heading;  // 标题文字
    eui::Color quote;    // 引用正文
    eui::Color code;     // 行内代码
    eui::Color bold;     // 强调文字
    eui::Color list;     // 列表符号
};

int gPaletteIndex = 0;

const HighlightPalette& currentPalette() {
    static const HighlightPalette kPalettes[] = {
        {{0.55f, 0.60f, 0.70f, 1.0f}, {0.99f, 0.84f, 0.55f, 1.0f}, {0.65f, 0.85f, 0.70f, 1.0f},
         {0.86f, 0.74f, 0.98f, 1.0f}, {0.94f, 0.97f, 1.00f, 1.0f}, {0.95f, 0.63f, 0.45f, 1.0f}},
        {{0.48f, 0.52f, 0.62f, 1.0f}, {0.55f, 0.85f, 0.95f, 1.0f}, {0.60f, 0.80f, 0.95f, 1.0f},
         {0.75f, 0.90f, 0.55f, 1.0f}, {0.90f, 0.95f, 0.85f, 1.0f}, {0.90f, 0.60f, 0.75f, 1.0f}},
    };
    return kPalettes[gPaletteIndex % 2];
}

// 简易 markdown 行级高亮（模拟 DevDesk Phase C 高亮器的形态）：
// # 标题 / > 引用 / - 列表 / **强调** / `行内代码`。返回【全文档坐标】runs，
// 由 input 裁剪平移到行内坐标；未命中 run 的部分用编辑器默认文本色。
std::vector<eui::TextRun> markdownLineRuns(const std::string& text, int start, int /*lineNo*/) {
    std::vector<eui::TextRun> runs;
    const auto push = [&](size_t a, size_t b, const eui::Color& color) {
        if (a < b) {
            runs.push_back({start + static_cast<int>(a), start + static_cast<int>(b), color});
        }
    };
    const HighlightPalette& palette = currentPalette();
    size_t i = 0;
    if (!text.empty() && text[0] == '#') {
        const size_t marks = text.find_first_not_of('#');
        const size_t markEnd = marks == std::string::npos ? text.size() : marks;
        push(0, markEnd, palette.mark);
        push(markEnd + 1, text.size(), palette.heading);
        return runs;
    }
    if (!text.empty() && text[0] == '>') {
        push(0, 1, palette.mark);
        push(1, text.size(), palette.quote);
        return runs;
    }
    if (text.size() >= 2 && text[0] == '-' && text[1] == ' ') {
        push(0, 1, palette.list);
        i = 2;
    }
    for (; i + 1 < text.size(); ++i) {
        if (text.compare(i, 2, "**") == 0) {
            const size_t close = text.find("**", i + 2);
            if (close != std::string::npos) {
                push(i, i + 2, palette.mark);
                push(i + 2, close, palette.bold);
                push(close, close + 2, palette.mark);
                i = close + 1;
            }
        } else if (text[i] == '`') {
            const size_t close = text.find('`', i + 1);
            if (close != std::string::npos) {
                push(i, i + 1, palette.mark);
                push(i + 1, close, palette.code);
                push(close, close + 1, palette.mark);
                i = close;
            }
        }
    }
    return runs;
}

eui::Signal<std::string>& editorText() {
    static eui::Signal<std::string> text{
        "# Styled runs editor\n"
        "input.lineStyles(provider) 回调按可视行返回颜色 runs。\n"
        "> 引用行整行着色，光标和选区照常工作。\n"
        "\n"
        "- 列表符号弱化，`行内代码` 紫色，**强调文字** 亮色\n"
        "- 改一行只重扫该行（其余行走行缓存），滚动纯位移不重扫\n"
        "中文与 emoji 🙂 混排：光标定位使用 UTF-8 字节偏移，与 runs 同坐标系。\n"
        "```cpp\n"
        "auto runs = highlighter.runsForLine(lineNo);\n"
        "```\n"};
    return text;
}

} // namespace

void compose(eui::Ui& ui, const eui::Screen& screen) {
    ui.stack("root")
        .size(screen.width, screen.height)
        .content([&] {
            ui.text("title")
                .position(34.0f, 28.0f)
                .size(screen.width - 68.0f, 40.0f)
                .text("ui.text().runs() - per-run color")
                .fontSize(26.0f)
                .lineHeight(32.0f)
                .color(kTitle)
                .build();

            ui.text("subtitle")
                .position(34.0f, 72.0f)
                .size(screen.width - 68.0f, 22.0f)
                .text("one element per line, colors are byte-offset runs - no flush between colors")
                .fontSize(13.0f)
                .lineHeight(18.0f)
                .color(kLabel)
                .build();

            float y = 120.0f;

            // 1. 一行多色（ASCII）：标记弱化 + 行内代码 + 链接
            {
                const std::string line = "# Release v2 **ready** - see `CHANGELOG.md` and issues";
                // offsets: '#'=0 .. ' '=12, 'ready'=13..20, '`'=21..23, 'CHANGELOG.md'=22..34, ...
                ui.text("md.heading")
                    .position(34.0f, y)
                    .size(screen.width - 68.0f, 30.0f)
                    .text(line)
                    .fontSize(16.0f)
                    .lineHeight(24.0f)
                    .color(kPlain)
                    .run(0, 1, kMark)                                  // '#'
                    .run(2, 12, kHeading)                              // "Release v2"
                    .run(13, 15, kMark)                                // "**"
                    .run(15, 20, kBody)                                // "ready"
                    .run(20, 22, kMark)                                // "**"
                    .run(24, 25, kMark)                                // '`'
                    .run(25, 37, kCode)                                // "CHANGELOG.md"
                    .run(37, 38, kMark)                                // '`'
                    .run(42, 48, kLink)                                // "issues"
                    .build();
                y += 42.0f;
            }

            // 2. CJK 多色：run 偏移按 UTF-8 字节，不能劈开汉字
            {
                const std::string line = "标题 与 正文 混排 —— 强调色";
                const int a = byteOffset(line, 0);    // "标题"
                const int b = byteOffset(line, 4);    // "与"
                const int c = byteOffset(line, 6);    // "正文"
                const int d = byteOffset(line, 10);   // "——"
                const int e = byteOffset(line, 13);   // "强调色"
                const int end = static_cast<int>(line.size());
                ui.text("md.cjk")
                    .position(34.0f, y)
                    .size(screen.width - 68.0f, 30.0f)
                    .text(line)
                    .fontSize(16.0f)
                    .lineHeight(24.0f)
                    .color(kPlain)
                    .run(a, a + 6, kHeading)
                    .run(b, b + 3, kMark)
                    .run(c, c + 6, kBody)
                    .run(d, d + 6, kMark)
                    .run(e, end, kLink)
                    .build();
                y += 42.0f;
            }

            // 3. 行级高亮模拟：引用行 + 空行 + 代码围栏行（编辑器整行一色也走 runs）
            {
                ui.text("md.quote")
                    .position(34.0f, y)
                    .size(screen.width - 68.0f, 26.0f)
                    .text("> quoted line: whole-line color is just one run")
                    .fontSize(15.0f)
                    .lineHeight(22.0f)
                    .color(kPlain)
                    .run(0, 1, kMark)
                    .run(2, static_cast<int>(40), kQuote)
                    .build();
                y += 30.0f;

                ui.text("md.fence")
                    .position(34.0f, y)
                    .size(screen.width - 68.0f, 26.0f)
                    .text("```cpp")
                    .fontSize(15.0f)
                    .fontFamily("monospace")
                    .lineHeight(22.0f)
                    .color(kPlain)
                    .run(0, 3, kMark)
                    .run(3, 6, kCode)
                    .build();
                y += 30.0f;

                ui.text("md.code")
                    .position(34.0f, y)
                    .size(screen.width - 68.0f, 26.0f)
                    .text("    auto runs = highlighter.runsForLine(42);")
                    .fontSize(15.0f)
                    .fontFamily("monospace")
                    .lineHeight(22.0f)
                    .color(kPlain)
                    .run(4, 8, kLink)
                    .run(27, 31, kCode)
                    .build();
                y += 42.0f;
            }

            // 4. 未命中 run 的部分回落元素色；乱序/重叠的 runs 由框架归一化
            {
                ui.text("md.fallback")
                    .position(34.0f, y)
                    .size(screen.width - 68.0f, 26.0f)
                    .text("default color text with one RED run inside")
                    .fontSize(15.0f)
                    .lineHeight(22.0f)
                    .color(kPlain)
                    .run(27, 30, eui::Color{1.0f, 0.35f, 0.35f, 1.0f})
                    .run(26, 33, eui::Color{1.0f, 0.35f, 0.35f, 1.0f})   // 乱序 + 重叠，后写覆盖
                    .build();
                y += 40.0f;
            }

            // 5. input + lineStyles：编辑器行级高亮（可视行回调 + 行缓存 + styleRevision）
            {
                ui.text("editor.caption")
                    .position(34.0f, y)
                    .size(screen.width - 68.0f, 22.0f)
                    .text("components::input().lineStyles(provider) - per visible line, cached by (lineNo, textRevision, styleRevision)")
                    .fontSize(13.0f)
                    .lineHeight(18.0f)
                    .color(kLabel)
                    .build();
                y += 26.0f;

                auto& editorState = ui.state<components::input_detail::InputModel::InputState>("editor");
                components::button(ui, "editor.palette")
                    .position(34.0f, y)
                    .size(240.0f, 32.0f)
                    .text("switch palette (styleRevision++)")
                    .fontSize(11.0f)
                    .radius(8.0f)
                    .onClick([&editorState] {
                        gPaletteIndex += 1;
                        editorState.styleRevision += 1;   // 触发全部可视行重新回调
                    })
                    .build();

                components::input(ui, "editor")
                    .position(34.0f, y + 44.0f)
                    .size(screen.width - 68.0f, 240.0f)
                    .multiline()
                    .scrollbar()
                    .fontSize(15.0f)
                    .inset(10.0f)
                    .bind(editorText())
                    .lineStyles(markdownLineRuns)
                    .build();
                y += 300.0f;
            }

            ui.text("footer")
                .position(34.0f, screen.height - 44.0f)
                .size(screen.width - 68.0f, 22.0f)
                .text("empty runs == legacy single-color path, byte-identical vertices")
                .fontSize(12.0f)
                .lineHeight(16.0f)
                .color(kLabel)
                .build();
        })
        .build();
}

} // namespace app
