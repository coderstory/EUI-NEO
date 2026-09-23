#pragma once

#include "core/render/render_types.h"

#include <string>
#include <vector>

namespace core {

enum class HorizontalAlign {
    Left,
    Center,
    Right
};

enum class VerticalAlign {
    Top,
    Center,
    Bottom
};

/// 扁平样式 run（Scintilla 式）。字节偏移是整个文本串的 UTF-8 偏移，
/// 与 TextMetrics::byteIndices、input 光标同一坐标系。
struct TextRun {
    int byteStart = 0;          // 含
    int byteEnd = 0;            // 不含；须 non-empty、按 start 升序、互不重叠（normalizeTextRuns 保证）
    Color color{1.0f, 1.0f, 1.0f, 1.0f};   // v1 唯一样式维度
};

constexpr bool operator==(const TextRun& left, const TextRun& right) {
    return left.byteStart == right.byteStart &&
           left.byteEnd == right.byteEnd &&
           left.color.r == right.color.r &&
           left.color.g == right.color.g &&
           left.color.b == right.color.b &&
           left.color.a == right.color.a;
}

/// 归一化调用方给的 runs：clamp 到 [0, text.size()] 的 UTF-8 边界（向下取整到
/// codepoint 起点，glyph 着色按 codepoint 粒度）、去重叠（后写覆盖前写）、
/// 合并相邻同色、丢弃空/反转 run。纯函数，供 DSL 消费侧与需要干净 runs 的
/// 调用方（DevDesk 高亮器）使用。
std::vector<TextRun> normalizeTextRuns(const std::string& text, std::vector<TextRun> runs);

struct TextStyle {
    std::string text;
    std::string fontFamily;
    float fontSize = 16.0f;
    int fontWeight = 400;
    Color color = {1.0f, 1.0f, 1.0f, 1.0f};
    float maxWidth = 0.0f;
    bool wrap = false;
    HorizontalAlign horizontalAlign = HorizontalAlign::Left;
    VerticalAlign verticalAlign = VerticalAlign::Top;
    float lineHeight = 0.0f;
    /// 空向量 ⇒ 与旧单色路径完全一致；颜色不改 layout/测量，只影响顶点色。
    std::vector<TextRun> runs;
};

} // namespace core
