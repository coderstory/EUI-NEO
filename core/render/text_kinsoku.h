#pragma once

// text_kinsoku —— CJK 行首禁则（kinsoku）共享判定（DevDesk need324 波 1）。
// 三处断行决策点共用：text_layout.cpp（rebuildLayout，渲染）、text_measure.cpp
// （measureTextSize，测量，两者有「逐位一致」契约）、components/markdown.h
// （pushFittingSegment，markdown 渲染）。DevDesk 应用层旁路 wrapToolDescLines
// 波 2 直接 include 本头（DevDesk → EUI-NEO include 方向合法）。
//
// 处理法 = 拉回法（pull-in）：断行成立时即将起行的码点 ∈ 禁则集 → 并入本行
// 行尾，行尾允许超 maxWidth ≤1 个标点宽；连续拉回由调用方钳制 ≤2 次。
// 硬约束：半角字符永不入集（URL `://` `.` 与英文混排误伤面，调研 §六）；
// 行尾禁则（不可收尾字符）不在 need324 范围。
// 字符集依据：docs/research/2026-11-18-cjk-line-break-prohibition.md（UAX #14
// 类别 + JIS X 4051 / Ecma 简中表两源交叉）。

#include <cstddef>
#include <string>

namespace core {
namespace text_detail {

/// 行首禁则字符判定（true = 该码点不可起行）。最小集 = 全角句读 + 全角结束
/// 括号/引号 + 省略号，共 14 码点。扩集前先过调研 §六半角红线。
inline bool isProhibitedLineStart(unsigned int cp) {
    switch (cp) {
        // 全角句读：、。，！？：；
        case 0x3001u: case 0x3002u: case 0xFF0Cu: case 0xFF01u:
        case 0xFF1Fu: case 0xFF1Au: case 0xFF1Bu:
        // 全角结束括号/引号：）〉》」』】
        case 0xFF09u: case 0x3009u: case 0x300Bu: case 0x300Du:
        case 0x300Fu: case 0x3011u:
        // 省略号 …
        case 0x2026u:
            return true;
        default:
            return false;
    }
}

/// 从 text 的 byte 偏移解码一个 UTF-8 码点（供只有字符串 + 字节偏移的调用方，
/// 如 measure 路径的 TextMetrics::byteIndices）。不校验非法序列：首字节非法、
/// 错位或越界返回 0（判不在禁则集，安全方向）。
inline unsigned int decodeUtf8At(const std::string& text, std::size_t byte) {
    if (byte >= text.size()) {
        return 0u;
    }
    const unsigned char first = static_cast<unsigned char>(text[byte]);
    if (first < 0x80u) {
        return first;
    }
    unsigned int cp = 0u;
    std::size_t extra = 0u;
    if ((first & 0xE0u) == 0xC0u) {
        cp = first & 0x1Fu;
        extra = 1u;
    } else if ((first & 0xF0u) == 0xE0u) {
        cp = first & 0x0Fu;
        extra = 2u;
    } else if ((first & 0xF8u) == 0xF0u) {
        cp = first & 0x07u;
        extra = 3u;
    } else {
        return 0u;
    }
    for (std::size_t k = 1u; k <= extra && byte + k < text.size(); ++k) {
        const unsigned char cont = static_cast<unsigned char>(text[byte + k]);
        if ((cont & 0xC0u) != 0x80u) {
            return 0u;
        }
        cp = (cp << 6u) | (cont & 0x3Fu);
    }
    return cp;
}

} // namespace text_detail
} // namespace core
