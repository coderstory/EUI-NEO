#include "eui_neo.h"

#include <string>

namespace app {

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("Font Size em Semantics")
        .pageId("font-em-size")
        .clearColor({0.07f, 0.08f, 0.10f, 1.0f})
        .windowSize(760, 640)
        .fps(60.0);
    return config;
}

namespace {

constexpr float kSampleSize = 24.0f;

struct FontRow {
    const char* id;
    const char* label;
    const char* fontFamily;
    int fontWeight;
};

// Same fontSize, different fonts. Since v0.7.0 fontSize is the em size
// (CSS font-size semantics): every row below must look the same visual
// height, no matter which font or hhea metrics it has.
const FontRow kRows[] = {
    {"system", "system UI font (default)", "", 400},
    {"mono", "monospace", "monospace", 400},
    {"light", "system UI light (w300)", "", 300},
    {"bold", "system UI bold (w700)", "", 700},
    {"yahei", "Microsoft YaHei", "Microsoft YaHei", 400},
    {"display", "YouSheBiaoTiHei (bundled)", "YouSheBiaoTiHei", 400},
};

} // namespace

void compose(eui::Ui& ui, const eui::Screen& screen) {
    const eui::Color title{0.94f, 0.97f, 1.0f, 1.0f};
    const eui::Color label{0.62f, 0.68f, 0.78f, 1.0f};
    const eui::Color sample{0.92f, 0.94f, 0.98f, 1.0f};

    ui.stack("root")
        .size(screen.width, screen.height)
        .content([&] {
            ui.text("title")
                .position(34.0f, 28.0f)
                .size(screen.width - 68.0f, 40.0f)
                .text("fontSize = em size (CSS semantics)")
                .fontSize(26.0f)
                .lineHeight(32.0f)
                .color(title)
                .build();

            ui.text("subtitle")
                .position(34.0f, 70.0f)
                .size(screen.width - 68.0f, 24.0f)
                .text("All rows below are fontSize 24 - they must match visually")
                .fontSize(13.0f)
                .lineHeight(18.0f)
                .color(label)
                .build();

            float y = 110.0f;
            for (const FontRow& row : kRows) {
                ui.text(std::string("label.") + row.id)
                    .position(34.0f, y)
                    .size(210.0f, 30.0f)
                    .text(row.label)
                    .fontSize(12.0f)
                    .lineHeight(16.0f)
                    .fontFamily("monospace")
                    .color(label)
                    .verticalAlign(eui::VerticalAlign::Center)
                    .build();

                ui.text(std::string("sample.") + row.id)
                    .position(254.0f, y)
                    .size(screen.width - 288.0f, 30.0f)
                    .text("Hamburgefonstiv 0123 中文字号")
                    .fontSize(kSampleSize)
                    .lineHeight(30.0f)
                    .fontFamily(row.fontFamily)
                    .fontWeight(row.fontWeight)
                    .color(sample)
                    .verticalAlign(eui::VerticalAlign::Center)
                    .build();
                y += 40.0f;
            }

            // Em ramp of one font: each step is exactly one em tall.
            const float sizes[] = {12.0f, 16.0f, 20.0f, 24.0f, 32.0f};
            float x = 34.0f;
            for (const float size : sizes) {
                ui.text("ramp." + std::to_string(static_cast<int>(size)))
                    .position(x, y + 10.0f)
                    .size(120.0f, 44.0f)
                    .text(std::to_string(static_cast<int>(size)) + "px")
                    .fontSize(size)
                    .lineHeight(44.0f)
                    .color(sample)
                    .build();
                x += 130.0f;
            }
        })
        .build();
}

} // namespace app
