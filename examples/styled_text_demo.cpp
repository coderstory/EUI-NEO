// styled_text_demo: TextBuilder.runs() 行内多色文本（styled-runs Phase A）。
// 人工点验项：一行内多色不换批、色块边界精确落字符、行级高亮模拟
//（编辑器场景：标记弱化色 + 行内代码色 + 链接色）。
#include "eui_neo.h"

#include <string>
#include <vector>

namespace app {

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("Styled Text Runs")
        .pageId("styled-text")
        .clearColor({0.07f, 0.08f, 0.10f, 1.0f})
        .windowSize(860, 560)
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
