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

/// 单行溢出行为。
///
/// Clip（默认）= 加本枚举之前的全部语义：文本侧不做任何截断，超宽文本照旧渲染，
/// 超出部分由调用方自己 clip。Ellipsis 是单行语义：仅在 wrap == false 且
/// maxWidth > 0 时，把行尾换成 U+2026，并保证整行 advance 之和 <= maxWidth
/// （省略号自身的 advance 预留在预算里）。wrap == true 时 Ellipsis 退化为
/// 无操作——多行 + 省略号是另一件事（按 CSS 的形状，text-overflow 也是
/// 挂在 block 上的单值属性，将来加第三种模式时形状不变）。
enum class TextOverflow {
    Clip,
    Ellipsis
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
    /// 单行溢出行为，默认 Clip（与本字段引入前逐字节一致）。
    TextOverflow overflow = TextOverflow::Clip;
    HorizontalAlign horizontalAlign = HorizontalAlign::Left;
    VerticalAlign verticalAlign = VerticalAlign::Top;
    float lineHeight = 0.0f;
    /// 空向量 ⇒ 与旧单色路径完全一致；颜色不改 layout/测量，只影响顶点色。
    std::vector<TextRun> runs;
};

} // namespace core
