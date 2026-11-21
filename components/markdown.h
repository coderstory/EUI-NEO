#pragma once

#include "components/theme.h"
#include "core/dsl.h"
#include "core/render/text.h"
#include "core/render/text_kinsoku.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(EUI_HAS_MD4C)
#include <md4c.h>
#endif

namespace components {

struct MarkdownStyle {
    explicit MarkdownStyle(const theme::ThemeColorTokens& tokens = theme::dark())
        : text(theme::withOpacity(tokens.text, 0.78f)),
          heading(theme::withOpacity(tokens.text, 0.96f)),
          muted(theme::withOpacity(tokens.text, 0.58f)),
          accent(tokens.primary),
          codeText(tokens.dark ? theme::color(0.88f, 0.94f, 1.0f) : theme::color(0.10f, 0.13f, 0.18f)),
          codeBackground(tokens.dark ? theme::color(0.08f, 0.09f, 0.11f, 1.0f) : theme::color(0.93f, 0.94f, 0.96f, 1.0f)),
          quoteBackground(theme::withOpacity(tokens.primary, tokens.dark ? 0.12f : 0.08f)),
          divider(theme::withOpacity(tokens.border, 0.82f)),
          bodySize(tokens.metrics.typography.body),
          bodyLineHeight(tokens.metrics.typography.body + tokens.metrics.typography.lineGapWide),
          h1Size(tokens.metrics.typography.displayCompact),
          h2Size(tokens.metrics.typography.heading),
          h3Size(tokens.metrics.typography.subtitle),
          codeSize(tokens.metrics.typography.label),
          blockGap(tokens.metrics.spacing.content),
          listIndent(tokens.metrics.control.indicator),
          codePadding(tokens.metrics.spacing.content),
          quotePadding(tokens.metrics.spacing.content),
          tableCellPadding(tokens.metrics.spacing.control),
          radius(tokens.metrics.radius.small) {}

    core::Color text;
    core::Color heading;
    core::Color muted;
    core::Color accent;
    core::Color codeText;
    core::Color codeBackground;
    core::Color quoteBackground;
    core::Color divider;
    std::string fontFamily;
    std::string codeFontFamily = "monospace";
    float bodySize = 16.0f;
    float bodyLineHeight = 24.0f;
    float h1Size = 30.0f;
    float h2Size = 24.0f;
    float h3Size = 20.0f;
    float codeSize = 14.0f;
    float blockGap = 12.0f;
    float listIndent = 22.0f;
    float codePadding = 12.0f;
    float quotePadding = 12.0f;
    float tableCellPadding = 10.0f;
    float radius = 6.0f;
};

namespace detail {

enum class MarkdownBlockKind {
    Paragraph,
    Heading,
    ListItem,
    Code,
    Html,
    TableRow,
    Divider
};

enum class MarkdownRunKind {
    Text,
    InlineCode,
    Html,
    Entity,
    Link,
    Image,
    Math,
    WikiLink,
    Underline
};

enum class MarkdownAlign {
    Default,
    Left,
    Center,
    Right
};

struct MarkdownRunStyle {
    bool emphasis = false;
    bool strong = false;
    bool deleted = false;
    bool underline = false;
    bool link = false;
    bool image = false;
    bool math = false;
    bool wiki = false;
    bool html = false;
};

struct MarkdownRun {
    MarkdownRunKind kind = MarkdownRunKind::Text;
    MarkdownRunStyle style;
    std::string text;
    std::string href;
    std::string title;
};

struct MarkdownTableCell {
    std::vector<MarkdownRun> runs;
    MarkdownAlign align = MarkdownAlign::Default;
};

struct MarkdownBlock {
    MarkdownBlockKind kind = MarkdownBlockKind::Paragraph;
    std::vector<MarkdownRun> runs;
    std::vector<MarkdownTableCell> cells;
    int headingLevel = 0;
    int quoteDepth = 0;
    int listDepth = 0;
    bool ordered = false;
    unsigned number = 0;
    bool task = false;
    bool taskChecked = false;
    bool tableHeader = false;
    std::string codeInfo;
    std::string codeLang;
};

inline std::string trimMarkdownText(std::string value) {
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

inline std::string decodeMarkdownEntity(const std::string& text) {
    if (text == "&amp;") {
        return "&";
    }
    if (text == "&lt;") {
        return "<";
    }
    if (text == "&gt;") {
        return ">";
    }
    if (text == "&quot;") {
        return "\"";
    }
    if (text == "&apos;") {
        return "'";
    }
    return text;
}

inline std::string displayText(const MarkdownRun& run) {
    std::string text = run.text;
    if (run.kind == MarkdownRunKind::Entity) {
        text = decodeMarkdownEntity(run.text);
    } else if (run.kind == MarkdownRunKind::Image) {
        text = "[image: " + (run.text.empty() ? run.href : run.text) + "]";
    } else if (run.kind == MarkdownRunKind::WikiLink) {
        text = "[[" + (run.text.empty() ? run.href : run.text) + "]]";
    } else if (run.kind == MarkdownRunKind::Math || run.style.math) {
        text = "$" + run.text + "$";
    } else if (run.kind == MarkdownRunKind::Html || run.style.html) {
        text = run.text;
    } else if ((run.kind == MarkdownRunKind::Link || run.style.link) && run.text.empty()) {
        text = run.href;
    }
    return text;
}

inline std::string plainText(const std::vector<MarkdownRun>& runs) {
    std::string value;
    for (const MarkdownRun& run : runs) {
        value += displayText(run);
    }
    return value;
}

inline std::string plainText(const std::vector<MarkdownTableCell>& cells, std::size_t index) {
    return index < cells.size() ? plainText(cells[index].runs) : "";
}

inline float estimateMarkdownTextHeight(const std::string& text, float width, float fontSize, float lineHeight) {
    const float safeLineHeight = lineHeight > 0.0f ? lineHeight : fontSize + 6.0f;
    const float averageCharWidth = std::max(1.0f, fontSize * 0.56f);
    const int charsPerLine = std::max(1, static_cast<int>(std::floor(std::max(1.0f, width) / averageCharWidth)));
    int lines = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::size_t count = end == std::string::npos ? text.size() - start : end - start;
        lines += std::max(1, static_cast<int>((count + static_cast<std::size_t>(charsPerLine) - 1) / static_cast<std::size_t>(charsPerLine)));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return static_cast<float>(std::max(1, lines)) * safeLineHeight;
}

inline std::vector<std::string> splitMarkdownTextForLayout(const std::string& text) {
    std::vector<std::string> parts;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const unsigned char first = static_cast<unsigned char>(text[begin]);
        if (std::isspace(first) != 0) {
            if (text[begin] == '\n') {
                parts.push_back("\n");
            }
            ++begin;
            continue;
        }

        if ((first & 0x80U) != 0U) {
            std::size_t end = begin + 1;
            while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) {
                ++end;
            }
            parts.push_back(text.substr(begin, end - begin));
            begin = end;
            continue;
        }

        std::size_t end = begin;
        while (end < text.size()) {
            const unsigned char ch = static_cast<unsigned char>(text[end]);
            if (std::isspace(ch) != 0 || (ch & 0x80U) != 0U) {
                break;
            }
            ++end;
        }
        parts.push_back(text.substr(begin, end - begin));
        begin = end;
    }
    return parts;
}

inline std::vector<std::string> splitMarkdownTextIntoCodepoints(const std::string& text) {
    std::vector<std::string> parts;
    std::size_t begin = 0;
    while (begin < text.size()) {
        std::size_t end = begin + 1;
        const unsigned char first = static_cast<unsigned char>(text[begin]);
        if ((first & 0x80U) != 0U) {
            while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) {
                ++end;
            }
        }
        parts.push_back(text.substr(begin, end - begin));
        begin = end;
    }
    return parts;
}

inline float measureMarkdownTextWidth(const std::string& text,
                                      const std::string& fontFamily,
                                      float fontSize,
                                      int fontWeight) {
    const float measured = core::TextPrimitive::measureTextWidth(text, fontFamily, fontSize, fontWeight);
    if (measured > 0.0f) {
        return measured;
    }
    return static_cast<float>(text.size()) * std::max(1.0f, fontSize * 0.56f);
}

inline std::string codeBlockDisplayText(const MarkdownBlock& block) {
    std::string text = plainText(block.runs);
    if (!block.codeLang.empty()) {
        text = block.codeLang + "\n" + text;
    } else if (!block.codeInfo.empty()) {
        text = block.codeInfo + "\n" + text;
    }
    return text;
}

inline float markdownHeadingSize(const MarkdownStyle& style, int level) {
    if (level <= 1) {
        return style.h1Size;
    }
    if (level == 2) {
        return style.h2Size;
    }
    return style.h3Size;
}

inline std::string markdownListPrefix(const MarkdownBlock& block) {
    if (block.task) {
        return block.taskChecked ? "[x]" : "[ ]";
    }
    if (block.ordered) {
        return std::to_string(block.number) + ".";
    }
    return "-";
}

inline core::HorizontalAlign horizontalAlign(MarkdownAlign align) {
    if (align == MarkdownAlign::Center) {
        return core::HorizontalAlign::Center;
    }
    if (align == MarkdownAlign::Right) {
        return core::HorizontalAlign::Right;
    }
    return core::HorizontalAlign::Left;
}

#if defined(EUI_HAS_MD4C)
struct MarkdownListContext {
    bool ordered = false;
    unsigned nextNumber = 1;
};

struct MarkdownListItemContext {
    bool task = false;
    bool checked = false;
};

struct MarkdownSpanContext {
    MarkdownRunKind kind = MarkdownRunKind::Text;
    MarkdownRunStyle style;
    std::string href;
    std::string title;
};

struct MarkdownParseState {
    std::vector<MarkdownBlock> blocks;
    std::vector<MarkdownListContext> lists;
    std::vector<MarkdownListItemContext> listItems;
    std::vector<MarkdownSpanContext> spans;
    MarkdownBlock current;
    bool hasCurrent = false;
    int quoteDepth = 0;
    bool inTableHead = false;
    bool inTableCell = false;
    std::vector<MarkdownTableCell> tableRow;
    MarkdownTableCell tableCell;
};

inline MarkdownParseState& markdownState(void* userdata) {
    return *static_cast<MarkdownParseState*>(userdata);
}

inline std::string markdownAttributeText(const MD_ATTRIBUTE& attr) {
    if (attr.text == nullptr || attr.size == 0) {
        return "";
    }
    return std::string(attr.text, attr.size);
}

inline MarkdownAlign markdownAlignFromMd(MD_ALIGN align) {
    switch (align) {
    case MD_ALIGN_LEFT:
        return MarkdownAlign::Left;
    case MD_ALIGN_CENTER:
        return MarkdownAlign::Center;
    case MD_ALIGN_RIGHT:
        return MarkdownAlign::Right;
    default:
        return MarkdownAlign::Default;
    }
}

inline MarkdownRunStyle currentRunStyle(const MarkdownParseState& state) {
    MarkdownRunStyle style;
    for (const MarkdownSpanContext& span : state.spans) {
        style.emphasis = style.emphasis || span.style.emphasis;
        style.strong = style.strong || span.style.strong;
        style.deleted = style.deleted || span.style.deleted;
        style.underline = style.underline || span.style.underline;
        style.link = style.link || span.style.link;
        style.image = style.image || span.style.image;
        style.math = style.math || span.style.math;
        style.wiki = style.wiki || span.style.wiki;
        style.html = style.html || span.style.html;
    }
    return style;
}

inline MarkdownRunKind currentRunKind(const MarkdownParseState& state, MD_TEXTTYPE type) {
    if (!state.spans.empty()) {
        return state.spans.back().kind;
    }
    if (type == MD_TEXT_CODE) {
        return MarkdownRunKind::InlineCode;
    }
    if (type == MD_TEXT_HTML) {
        return MarkdownRunKind::Html;
    }
    if (type == MD_TEXT_ENTITY) {
        return MarkdownRunKind::Entity;
    }
    if (type == MD_TEXT_LATEXMATH) {
        return MarkdownRunKind::Math;
    }
    return MarkdownRunKind::Text;
}

inline void pushMarkdownRun(MarkdownParseState& state, MD_TEXTTYPE type, std::string text) {
    if (text.empty()) {
        return;
    }

    MarkdownRun run;
    run.kind = currentRunKind(state, type);
    run.style = currentRunStyle(state);
    run.text = std::move(text);
    if (!state.spans.empty()) {
        run.href = state.spans.back().href;
        run.title = state.spans.back().title;
    }

    if (state.inTableCell) {
        state.tableCell.runs.push_back(std::move(run));
    } else if (state.hasCurrent) {
        state.current.runs.push_back(std::move(run));
    }
}

inline void finishMarkdownBlock(MarkdownParseState& state) {
    if (!state.hasCurrent) {
        return;
    }

    if (!trimMarkdownText(plainText(state.current.runs)).empty() || state.current.kind == MarkdownBlockKind::Divider) {
        state.blocks.push_back(std::move(state.current));
    }
    state.current = {};
    state.hasCurrent = false;
}

inline void beginMarkdownTextBlock(MarkdownParseState& state, MarkdownBlockKind kind) {
    finishMarkdownBlock(state);
    state.current = {};
    state.current.kind = kind;
    state.current.quoteDepth = state.quoteDepth;
    state.current.listDepth = static_cast<int>(state.lists.size());
    if (!state.lists.empty()) {
        MarkdownListContext& list = state.lists.back();
        state.current.ordered = list.ordered;
        if (list.ordered) {
            state.current.number = list.nextNumber++;
        }
    }
    if (!state.listItems.empty()) {
        state.current.task = state.listItems.back().task;
        state.current.taskChecked = state.listItems.back().checked;
    }
    state.hasCurrent = true;
}

inline int enterMarkdownBlock(MD_BLOCKTYPE type, void* detail, void* userdata) {
    MarkdownParseState& state = markdownState(userdata);
    switch (type) {
    case MD_BLOCK_QUOTE:
        ++state.quoteDepth;
        break;
    case MD_BLOCK_UL:
        state.lists.push_back({false, 1});
        break;
    case MD_BLOCK_OL: {
        unsigned start = 1;
        if (detail != nullptr) {
            start = static_cast<const MD_BLOCK_OL_DETAIL*>(detail)->start;
        }
        state.lists.push_back({true, start});
        break;
    }
    case MD_BLOCK_LI: {
        MarkdownListItemContext item;
        if (detail != nullptr) {
            const auto* li = static_cast<const MD_BLOCK_LI_DETAIL*>(detail);
            item.task = li->is_task != 0;
            item.checked = li->task_mark == 'x' || li->task_mark == 'X';
        }
        state.listItems.push_back(item);
        beginMarkdownTextBlock(state, MarkdownBlockKind::ListItem);
        break;
    }
    case MD_BLOCK_H:
        beginMarkdownTextBlock(state, MarkdownBlockKind::Heading);
        if (detail != nullptr) {
            state.current.headingLevel = std::clamp(static_cast<int>(static_cast<const MD_BLOCK_H_DETAIL*>(detail)->level), 1, 6);
        }
        break;
    case MD_BLOCK_P:
        if (state.lists.empty()) {
            beginMarkdownTextBlock(state, MarkdownBlockKind::Paragraph);
        } else if (!state.hasCurrent || state.current.kind != MarkdownBlockKind::ListItem || !trimMarkdownText(plainText(state.current.runs)).empty()) {
            beginMarkdownTextBlock(state, MarkdownBlockKind::ListItem);
        }
        break;
    case MD_BLOCK_CODE:
        beginMarkdownTextBlock(state, MarkdownBlockKind::Code);
        if (detail != nullptr) {
            const auto* code = static_cast<const MD_BLOCK_CODE_DETAIL*>(detail);
            state.current.codeInfo = markdownAttributeText(code->info);
            state.current.codeLang = markdownAttributeText(code->lang);
        }
        break;
    case MD_BLOCK_HTML:
        beginMarkdownTextBlock(state, MarkdownBlockKind::Html);
        break;
    case MD_BLOCK_HR:
        finishMarkdownBlock(state);
        state.blocks.push_back({MarkdownBlockKind::Divider});
        break;
    case MD_BLOCK_THEAD:
        state.inTableHead = true;
        break;
    case MD_BLOCK_TR:
        state.tableRow.clear();
        break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        state.inTableCell = true;
        state.tableCell = {};
        if (detail != nullptr) {
            state.tableCell.align = markdownAlignFromMd(static_cast<const MD_BLOCK_TD_DETAIL*>(detail)->align);
        }
        break;
    default:
        break;
    }
    return 0;
}

inline int leaveMarkdownBlock(MD_BLOCKTYPE type, void*, void* userdata) {
    MarkdownParseState& state = markdownState(userdata);
    switch (type) {
    case MD_BLOCK_H:
    case MD_BLOCK_P:
    case MD_BLOCK_CODE:
    case MD_BLOCK_HTML:
        finishMarkdownBlock(state);
        break;
    case MD_BLOCK_LI:
        finishMarkdownBlock(state);
        if (!state.listItems.empty()) {
            state.listItems.pop_back();
        }
        break;
    case MD_BLOCK_QUOTE:
        state.quoteDepth = std::max(0, state.quoteDepth - 1);
        break;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        if (!state.lists.empty()) {
            state.lists.pop_back();
        }
        break;
    case MD_BLOCK_THEAD:
        state.inTableHead = false;
        break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        state.inTableCell = false;
        state.tableRow.push_back(std::move(state.tableCell));
        state.tableCell = {};
        break;
    case MD_BLOCK_TR:
        if (!state.tableRow.empty()) {
            MarkdownBlock row;
            row.kind = MarkdownBlockKind::TableRow;
            row.cells = std::move(state.tableRow);
            row.tableHeader = state.inTableHead;
            state.blocks.push_back(std::move(row));
        }
        state.tableRow.clear();
        break;
    default:
        break;
    }
    return 0;
}

inline int enterMarkdownSpan(MD_SPANTYPE type, void* detail, void* userdata) {
    MarkdownParseState& state = markdownState(userdata);
    MarkdownSpanContext span;
    span.style = currentRunStyle(state);
    switch (type) {
    case MD_SPAN_EM:
        span.style.emphasis = true;
        break;
    case MD_SPAN_STRONG:
        span.style.strong = true;
        break;
    case MD_SPAN_A:
        span.kind = MarkdownRunKind::Link;
        span.style.link = true;
        if (detail != nullptr) {
            const auto* link = static_cast<const MD_SPAN_A_DETAIL*>(detail);
            span.href = markdownAttributeText(link->href);
            span.title = markdownAttributeText(link->title);
        }
        break;
    case MD_SPAN_IMG:
        span.kind = MarkdownRunKind::Image;
        span.style.image = true;
        if (detail != nullptr) {
            const auto* image = static_cast<const MD_SPAN_IMG_DETAIL*>(detail);
            span.href = markdownAttributeText(image->src);
            span.title = markdownAttributeText(image->title);
        }
        break;
    case MD_SPAN_CODE:
        span.kind = MarkdownRunKind::InlineCode;
        break;
    case MD_SPAN_DEL:
        span.style.deleted = true;
        break;
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
        span.kind = MarkdownRunKind::Math;
        span.style.math = true;
        break;
    case MD_SPAN_WIKILINK:
        span.kind = MarkdownRunKind::WikiLink;
        span.style.wiki = true;
        if (detail != nullptr) {
            span.href = markdownAttributeText(static_cast<const MD_SPAN_WIKILINK_DETAIL*>(detail)->target);
        }
        break;
    case MD_SPAN_U:
        span.kind = MarkdownRunKind::Underline;
        span.style.underline = true;
        break;
    default:
        break;
    }
    state.spans.push_back(std::move(span));
    return 0;
}

inline int leaveMarkdownSpan(MD_SPANTYPE, void*, void* userdata) {
    MarkdownParseState& state = markdownState(userdata);
    if (!state.spans.empty()) {
        state.spans.pop_back();
    }
    return 0;
}

inline int markdownText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
    MarkdownParseState& state = markdownState(userdata);
    switch (type) {
    case MD_TEXT_BR:
        pushMarkdownRun(state, type, "\n");
        break;
    case MD_TEXT_SOFTBR:
        pushMarkdownRun(state, type, " ");
        break;
    case MD_TEXT_NULLCHAR:
        pushMarkdownRun(state, type, "\xEF\xBF\xBD");
        break;
    default:
        pushMarkdownRun(state, type, text == nullptr ? "" : std::string(text, size));
        break;
    }
    return 0;
}

inline std::vector<MarkdownBlock> parseMarkdownBlocks(const std::string& source) {
    MarkdownParseState state;
    MD_PARSER parser = {};
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB |
                   MD_FLAG_LATEXMATHSPANS |
                   MD_FLAG_WIKILINKS |
                   MD_FLAG_UNDERLINE |
                   MD_FLAG_PERMISSIVEATXHEADERS;
    parser.enter_block = enterMarkdownBlock;
    parser.leave_block = leaveMarkdownBlock;
    parser.enter_span = enterMarkdownSpan;
    parser.leave_span = leaveMarkdownSpan;
    parser.text = markdownText;

    const int result = md_parse(source.c_str(), static_cast<MD_SIZE>(source.size()), &parser, &state);
    finishMarkdownBlock(state);
    if (result == 0) {
        return state.blocks;
    }

    MarkdownBlock block;
    block.kind = MarkdownBlockKind::Paragraph;
    block.runs.push_back({MarkdownRunKind::Text, {}, source, {}, {}});
    return {block};
}
#else
inline std::vector<MarkdownBlock> parseMarkdownBlocks(const std::string& source) {
    MarkdownBlock block;
    block.kind = MarkdownBlockKind::Paragraph;
    block.runs.push_back({MarkdownRunKind::Text, {}, source, {}, {}});
    return {block};
}
#endif

struct MarkdownDocument {
    std::vector<MarkdownBlock> blocks;
};

struct MarkdownDocumentCacheEntry {
    std::shared_ptr<const MarkdownDocument> document;
    std::size_t lastUsed = 0;
    std::size_t bytes = 0;
};

struct MarkdownDocumentCache {
    std::unordered_map<std::string, MarkdownDocumentCacheEntry> entries;
    std::size_t accessTick = 0;
    std::size_t bytes = 0;
};

inline MarkdownDocumentCache& sharedMarkdownDocumentCache() {
    static thread_local MarkdownDocumentCache cache;
    return cache;
}

inline void addMarkdownCacheBytes(std::size_t& total, std::size_t value) {
    const std::size_t available = std::numeric_limits<std::size_t>::max() - total;
    total += std::min(value, available);
}

inline std::size_t markdownCacheBytesForItems(std::size_t count, std::size_t bytesPerItem) {
    if (count == 0 || bytesPerItem == 0) {
        return 0;
    }
    const std::size_t maxCount = std::numeric_limits<std::size_t>::max() / bytesPerItem;
    return count > maxCount ? std::numeric_limits<std::size_t>::max() : count * bytesPerItem;
}

inline std::size_t markdownRunCacheBytes(const MarkdownRun& run) {
    std::size_t bytes = 0;
    addMarkdownCacheBytes(bytes, run.text.capacity());
    addMarkdownCacheBytes(bytes, run.href.capacity());
    addMarkdownCacheBytes(bytes, run.title.capacity());
    return bytes;
}

inline std::size_t markdownDocumentCacheBytes(const std::string& source, const MarkdownDocument& document) {
    std::size_t bytes = 0;
    addMarkdownCacheBytes(bytes, sizeof(MarkdownDocument));
    addMarkdownCacheBytes(bytes, source.size());
    addMarkdownCacheBytes(bytes, markdownCacheBytesForItems(document.blocks.capacity(), sizeof(MarkdownBlock)));
    for (const MarkdownBlock& block : document.blocks) {
        addMarkdownCacheBytes(bytes, block.codeInfo.capacity());
        addMarkdownCacheBytes(bytes, block.codeLang.capacity());
        addMarkdownCacheBytes(bytes, markdownCacheBytesForItems(block.runs.capacity(), sizeof(MarkdownRun)));
        for (const MarkdownRun& run : block.runs) {
            addMarkdownCacheBytes(bytes, markdownRunCacheBytes(run));
        }
        addMarkdownCacheBytes(bytes, markdownCacheBytesForItems(block.cells.capacity(), sizeof(MarkdownTableCell)));
        for (const MarkdownTableCell& cell : block.cells) {
            addMarkdownCacheBytes(bytes, markdownCacheBytesForItems(cell.runs.capacity(), sizeof(MarkdownRun)));
            for (const MarkdownRun& run : cell.runs) {
                addMarkdownCacheBytes(bytes, markdownRunCacheBytes(run));
            }
        }
    }
    return bytes;
}

// ── 性能仪表（bug399 round7 / debt398）── 一次加够，跨 TU 共享（inline 函数内 static）。
// 只读计数，零行为改动；探针读 delta 定位 compose 耗时构成。
struct MarkdownPerfStats {
    long long parseHits = 0;
    long long parseMisses = 0;
    double parseLookupMs = 0.0;
    double parseBuildMs = 0.0;
    long long inlineHits = 0;
    long long inlineMisses = 0;
    double inlineKeyMs = 0.0;
    double inlineLookupMs = 0.0;
    double inlineLayoutMs = 0.0;
    long long buildCalls = 0;
    double buildMs = 0.0;
    long long blocksVisited = 0;
    long long blocksCulled = 0;
    long long kindHeading = 0;
    long long kindParagraph = 0;
    long long kindListItem = 0;
    long long kindCode = 0;
    long long kindHtml = 0;
    long long kindDivider = 0;
    long long kindTableRow = 0;
    std::size_t inlineCacheEntries = 0;
    std::size_t inlineCacheBytes = 0;
    std::size_t docCacheEntries = 0;
    // bug399 round9 / debt398 P1-B：文档缓存条目真实展开体积（计费口径）与因
    // 超单条上限而被拒缓存（= 每帧 miss）的次数，供探针读实测复核容量。
    std::size_t lastDocBytes = 0;
    long long docTooLarge = 0;
    long long inlineLayoutBuilds = 0;
    long long inlineSegmentsBuilt = 0;
    long long inlineRunsBuilt = 0;
    void countBlockKind(MarkdownBlockKind kind) {
        switch (kind) {
        case MarkdownBlockKind::Heading: ++kindHeading; break;
        case MarkdownBlockKind::Paragraph: ++kindParagraph; break;
        case MarkdownBlockKind::ListItem: ++kindListItem; break;
        case MarkdownBlockKind::Code: ++kindCode; break;
        case MarkdownBlockKind::Html: ++kindHtml; break;
        case MarkdownBlockKind::Divider: ++kindDivider; break;
        case MarkdownBlockKind::TableRow: ++kindTableRow; break;
        default: break;
        }
    }
};

inline MarkdownPerfStats& markdownPerfStats() {
    static MarkdownPerfStats stats;
    return stats;
}

inline double markdownPerfMs(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

inline std::shared_ptr<const MarkdownDocument> cachedMarkdownDocument(const std::string& source) {
    // 64 = one screenful of typical blocks; with 8 a long document evicted
    // nearly every entry between frames and re-parsed on each compose pass.
    // Total memory stays bounded by kCacheByteCapacity below.
    constexpr std::size_t kCacheCapacity = 64;
    // bug399 round9 / debt398 P1-B：条目口径对齐真实工作集。旧 256KB 单条上限使
    // 53KB 夹具的展开体（blocks/runs/单元格逐项计费，实测数 MB）被判超限 → 不
    // 入缓存 → 每帧 miss 重 parse+重建（round7 log parse hit=0 miss=1/帧）。
    // 单条抬到 8MB 容典型长文档展开体；总量 32MB 封顶（实际占用=条目真实字节，
    // 上限只是准入闸，不预分配）。
    constexpr std::size_t kCacheByteCapacity = 8 * 1024 * 1024;
    constexpr std::size_t kEntryByteCapacity = 2 * 1024 * 1024;
    MarkdownDocumentCache& cache = sharedMarkdownDocumentCache();
    MarkdownPerfStats& perf = markdownPerfStats();
    const auto lookupT0 = std::chrono::steady_clock::now();
    const auto existing = cache.entries.find(source);
    perf.parseLookupMs += markdownPerfMs(lookupT0);
    perf.docCacheEntries = cache.entries.size();
    if (existing != cache.entries.end()) {
        ++perf.parseHits;
        existing->second.lastUsed = ++cache.accessTick;
        return existing->second.document;
    }
    ++perf.parseMisses;

    auto document = std::make_shared<MarkdownDocument>();
    const auto parseT0 = std::chrono::steady_clock::now();
    document->blocks = parseMarkdownBlocks(source);
    perf.parseBuildMs += markdownPerfMs(parseT0);
    const std::size_t documentBytes = markdownDocumentCacheBytes(source, *document);
    perf.lastDocBytes = documentBytes;
    if (documentBytes > kEntryByteCapacity) {
        ++perf.docTooLarge;
        return document;
    }

    while (!cache.entries.empty() &&
           (cache.entries.size() >= kCacheCapacity || documentBytes > kCacheByteCapacity - cache.bytes)) {
        const auto oldest = std::min_element(cache.entries.begin(), cache.entries.end(),
                                             [](const auto& left, const auto& right) {
                                                 return left.second.lastUsed < right.second.lastUsed;
                                             });
        if (oldest != cache.entries.end()) {
            cache.bytes -= oldest->second.bytes;
            cache.entries.erase(oldest);
        }
    }

    const auto inserted = cache.entries.emplace(source, MarkdownDocumentCacheEntry{document, ++cache.accessTick, documentBytes});
    cache.bytes += documentBytes;
    return inserted.first->second.document;
}

} // namespace detail

class MarkdownBuilder {
public:
    MarkdownBuilder(core::dsl::Ui& ui, std::string id)
        : ui_(ui), id_(std::move(id)) {}

    static float estimateHeight(const std::string& markdown,
                                float width,
                                const MarkdownStyle& style = MarkdownStyle()) {
        const std::shared_ptr<const detail::MarkdownDocument> document = detail::cachedMarkdownDocument(markdown);
        const std::vector<detail::MarkdownBlock>& blocks = document->blocks;
        float height = 0.0f;
        for (std::size_t index = 0; index < blocks.size(); ++index) {
            if (blocks[index].kind == detail::MarkdownBlockKind::TableRow) {
                std::size_t end = index + 1;
                while (end < blocks.size() && blocks[end].kind == detail::MarkdownBlockKind::TableRow) {
                    ++end;
                }
                height += estimateTableHeight(blocks, index, end, width, style);
                index = end - 1;
            } else {
                height += estimateBlockHeight(blocks[index], width, style);
            }
            if (index + 1 < blocks.size()) {
                height += style.blockGap;
            }
        }
        return height;
    }

    // ── 页内搜索缝（显示文本域）────────────────────────────────────────
    // 背景：搜索高亮若按「源码字节偏移 + charsPerLine 近似模型」反推矩形，对 CJK
    // （字宽 ≈ fontSize 而非 0.56×）与英文词边界折行完全失准——高亮块与文本整行
    // 错位。本缝把搜索域改到「渲染显示文本」：displayTextOf 给出与渲染同源的显示
    // 文本（调用方在其上做子串匹配），hitRectsForDisplayRange 把匹配的显示码点
    // 区间经真实 inline 布局（cachedInlineLayout，与 renderInlineRuns 同缓存同分
    // 割）反解为逐行矩形（块局部坐标）。两侧枚举共用 appendBlockDisplayText，
    // 零漂移。
    //
    // 显示文本构成（逐 md4c 块）：
    //   Paragraph/Heading/Quote = runs 的 displayText 串接；
    //   ListItem = 列表前缀（markdownListPrefix）+ runs 串接；
    //   Code = codeBlockDisplayText（含语言行）；Html = plainText(runs)；
    //   Table = 各 cell runs 串接；Divider 无文本；块与块之间 '\n' 分隔。
    // 已知近似：Code/Html 内文由 ui.text .wrap() 真排（非 inline 布局），矩形按
    // estimateMarkdownTextHeight 同式虚拟行近似（等宽 ASCII 下贴合）；Table 只
    // 给 cell 包围盒（不逐行）。
    struct MarkdownHitRect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    static std::string displayTextOf(const std::string& markdown) {
        const std::shared_ptr<const detail::MarkdownDocument> document =
            detail::cachedMarkdownDocument(markdown);
        std::string out;
        for (std::size_t i = 0; i < document->blocks.size(); ++i) {
            if (i > 0) {
                out += '\n';
            }
            appendBlockDisplayText(document->blocks[i], out);
        }
        return out;
    }

    // 显示码点区间 [cpBegin, cpEnd) → 渲染矩形（块局部坐标，逐行一矩形；区间
    // 尾自动钳到显示文本末）。width = 渲染宽（components::markdown 收到的同一
    // contentWidth，调用方必须与渲染同宽，否则折行不一致）。
    static std::vector<MarkdownHitRect> hitRectsForDisplayRange(
        const std::string& markdown, float width, const MarkdownStyle& style,
        std::size_t cpBegin, std::size_t cpEnd) {
        std::vector<MarkdownHitRect> rects;
        if (width <= 0.0f || cpBegin >= cpEnd) {
            return rects;
        }
        const std::shared_ptr<const detail::MarkdownDocument> document =
            detail::cachedMarkdownDocument(markdown);
        const std::vector<detail::MarkdownBlock>& blocks = document->blocks;
        std::size_t cp = 0;
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            if (i > 0) {
                ++cp;  // 块间 '\n'
            }
            const detail::MarkdownBlock& b = blocks[i];
            const float quoteInset = static_cast<float>(std::max(0, b.quoteDepth)) * 8.0f;
            const float listInset = static_cast<float>(std::max(0, b.listDepth - 1)) * style.listIndent;
            const float contentWidth = std::max(0.0f, width - quoteInset - listInset);
            if (b.kind == detail::MarkdownBlockKind::Divider) {
                continue;
            }
            if (b.kind == detail::MarkdownBlockKind::Code ||
                b.kind == detail::MarkdownBlockKind::Html) {
                rectsForPlainTextBlock(rects, b, contentWidth, style,
                                       quoteInset + listInset, cp, cpBegin, cpEnd);
            } else if (b.kind == detail::MarkdownBlockKind::TableRow) {
                std::size_t end = i + 1;
                while (end < blocks.size() && blocks[end].kind == detail::MarkdownBlockKind::TableRow) {
                    ++end;
                }
                cp = rectsForTable(rects, blocks, i, end, contentWidth, style,
                                   quoteInset + listInset, cp, cpBegin, cpEnd);
                i = end - 1;
            } else {
                rectsForInlineBlock(rects, b, contentWidth, style,
                                    quoteInset + listInset, cp, cpBegin, cpEnd);
            }
            if (cp > cpEnd) {
                break;
            }
        }
        mergeAdjacentRectsOnSameLine(rects);
        return rects;
    }

    // ── 整篇文档系选区矩形（need345 notepad 预览）───────────────────────
    // hitRectsForDisplayRange 的整篇叠层口径：块内枚举零漂移（同一组 per-block
    // 助手 + 同一条合并），y 自文档内容顶累计——displayCpForPoint 的 locals
    // 纵向排布同式（estimateBlockHeight/estimateTableHeight + blockGap，块间
    // gap 与 build() column gap 一致；尾块后多计一个 gap 只落在文档末空白区，
    // 不影响块内矩形定位）。整篇单元素渲染（notepad F-13 预览）的调用方无逐块
    // 几何表，选区/高亮按文档 y 直接叠层。
    static std::vector<MarkdownHitRect> hitRectsForDisplayRangeDoc(
        const std::string& markdown, float width, const MarkdownStyle& style,
        std::size_t cpBegin, std::size_t cpEnd) {
        std::vector<MarkdownHitRect> rects;
        if (width <= 0.0f || cpBegin >= cpEnd) {
            return rects;
        }
        const std::shared_ptr<const detail::MarkdownDocument> document =
            detail::cachedMarkdownDocument(markdown);
        const std::vector<detail::MarkdownBlock>& blocks = document->blocks;
        std::size_t cp = 0;
        float yAcc = 0.0f;  // 块顶文档 y（每块 = 块高 + blockGap）
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            if (i > 0) {
                ++cp;  // 块间 '\n'
            }
            const detail::MarkdownBlock& b = blocks[i];
            std::size_t groupEnd = i + 1;  // TableRow 行组尾（非行组 = i+1）
            if (b.kind == detail::MarkdownBlockKind::TableRow) {
                while (groupEnd < blocks.size() &&
                       blocks[groupEnd].kind == detail::MarkdownBlockKind::TableRow) {
                    ++groupEnd;
                }
            }
            const float blockH =
                b.kind == detail::MarkdownBlockKind::TableRow
                    ? estimateTableHeight(blocks, i, groupEnd, width, style)
                    : estimateBlockHeight(b, width, style);
            const float quoteInset = static_cast<float>(std::max(0, b.quoteDepth)) * 8.0f;
            const float listInset = static_cast<float>(std::max(0, b.listDepth - 1)) * style.listIndent;
            const float contentWidth = std::max(0.0f, width - quoteInset - listInset);
            const std::size_t base = rects.size();
            if (b.kind == detail::MarkdownBlockKind::Divider) {
                // 无显示文本，不发射矩形，但占排版位（displayCpForPoint 同式）
                yAcc += blockH + style.blockGap;
                continue;
            }
            if (b.kind == detail::MarkdownBlockKind::Code ||
                b.kind == detail::MarkdownBlockKind::Html) {
                rectsForPlainTextBlock(rects, b, contentWidth, style,
                                       quoteInset + listInset, cp, cpBegin, cpEnd);
            } else if (b.kind == detail::MarkdownBlockKind::TableRow) {
                cp = rectsForTable(rects, blocks, i, groupEnd, contentWidth, style,
                                   quoteInset + listInset, cp, cpBegin, cpEnd);
                i = groupEnd - 1;
            } else {
                // 内联块族（rectsForInlineBlock→rectsForInlineRuns 的 segment 域）
                // 的查询区间是块局部显示域（块首=0），非文档系：单块缝
                // hitRectsForDisplayRange 块基址=0 两域重合；文档缝此处 cp=块基址，
                // 必须平移后再入——不平移则区间整体右漂基址个码点（need345 往返
                // 零漂移红：cp=6 块间 '\n' 泄漏进基址 7 的段落局部域，矩形落在局部
                // 第 7 字、反查回 13）。Code/Html/Table 族内部以 cp 为锚自对齐，免。
                const std::size_t localBegin = cpBegin > cp ? cpBegin - cp : 0;
                const std::size_t localEnd = cpEnd > cp ? cpEnd - cp : 0;
                rectsForInlineBlock(rects, b, contentWidth, style,
                                    quoteInset + listInset, cp, localBegin, localEnd);
            }
            // 本块矩形 y 抬到文档系（块局部 y + 块顶 yAcc）
            for (std::size_t r = base; r < rects.size(); ++r) {
                rects[r].y += yAcc;
            }
            yAcc += blockH + style.blockGap;
            if (cp > cpEnd) {
                break;
            }
        }
        mergeAdjacentRectsOnSameLine(rects);
        return rects;
    }

    // ── 点→显示码点反查（need264 文本拖选）──────────────────────────────
    // hitRectsForDisplayRange 的逆问题：块局部坐标 (x, y)（与上方矩形输出同一
    // 块局部坐标系，调用方先经自己的几何表把内容系 y 定到块）反查最近字形边界
    // 的显示码点偏移，钳到 [0, 块显示文本码点数]。与渲染同源：逐子块走同一布局
    // 通路（cachedInlineLayout / Code|Html 虚拟行近似 / cell 包围盒），子块纵向
    // 排布与 estimateHeight 同式（estimateBlockHeight + blockGap），段内边界用
    // 逐码点增量 advance（与折行决策同款累加，同字体无 kerning 可加性成立，
    // 见 layoutInlineRunsWithStyle 折行注释）。
    struct MarkdownPointHit {
        std::size_t cp = 0;
        bool ok = false;  // false = 文档无显示文本（如 Divider-only），cp 无意义
    };

    static MarkdownPointHit displayCpForPoint(const std::string& markdown,
                                              float width,
                                              const MarkdownStyle& style,
                                              float x, float y) {
        MarkdownPointHit hit;
        if (width <= 0.0f) {
            return hit;
        }
        const std::shared_ptr<const detail::MarkdownDocument> document =
            detail::cachedMarkdownDocument(markdown);
        const std::vector<detail::MarkdownBlock>& blocks = document->blocks;
        // 子块几何录制（单遍）：每个渲染单元（md4c 子块 / 表格行组）的块局部
        // 纵向范围与显示码点基址——纵向排布与 estimateHeight 同式。
        struct LocalBlock {
            std::size_t index;
            std::size_t groupEnd;
            std::size_t cpBase;
            float yTop;
            float height;
        };
        std::vector<LocalBlock> locals;
        std::size_t cp = 0;
        float yAcc = 0.0f;
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            if (i > 0) {
                ++cp;  // 块间 '\n'
            }
            const detail::MarkdownBlock& b = blocks[i];
            std::size_t groupEnd = i + 1;
            float blockH = 0.0f;
            const bool isRowGroup =
                b.kind == detail::MarkdownBlockKind::TableRow;
            if (isRowGroup) {
                while (groupEnd < blocks.size() &&
                       blocks[groupEnd].kind == detail::MarkdownBlockKind::TableRow) {
                    ++groupEnd;
                }
                blockH = estimateTableHeight(blocks, i, groupEnd, width, style);
            } else {
                blockH = estimateBlockHeight(b, width, style);
            }
            locals.push_back({i, groupEnd, cp, yAcc, blockH});
            cp += blockDisplayCp(blocks, i, groupEnd);
            yAcc += blockH + style.blockGap;
            if (isRowGroup) {
                // 组内其余行并入本渲染单元（hitRectsForDisplayRange 同式
                // i = end - 1）——否则每行重复成 local 且码点重复计账。
                i = groupEnd - 1;
            }
        }
        if (locals.empty()) {
            return hit;
        }
        // 命中子块：纵向包含 y 优先；全部未含（浮点/近似缝隙）取中心最近兜底
        // ——保证恒有非越界返回。
        const LocalBlock* chosen = nullptr;
        const LocalBlock* nearest = &locals.front();
        float nearestDist = std::numeric_limits<float>::max();
        for (const LocalBlock& lb : locals) {
            if (blocks[lb.index].kind == detail::MarkdownBlockKind::Divider) {
                continue;  // 无显示文本，不做命中对象
            }
            if (y >= lb.yTop && y < lb.yTop + lb.height) {
                chosen = &lb;
                break;
            }
            const float dist = std::fabs(y - (lb.yTop + lb.height * 0.5f));
            if (dist < nearestDist) {
                nearestDist = dist;
                nearest = &lb;
            }
        }
        if (chosen == nullptr) {
            chosen = nearest;
        }
        resolveCpInBlock(blocks, chosen->index, chosen->groupEnd, width, style,
                         x, y - chosen->yTop, chosen->cpBase, hit);
        return hit;
    }

    MarkdownBuilder& markdown(std::string value) { markdown_ = std::move(value); return *this; }
    MarkdownBuilder& x(float value) { x_ = value; hasX_ = true; return *this; }
    MarkdownBuilder& y(float value) { y_ = value; hasY_ = true; return *this; }
    MarkdownBuilder& position(float xValue, float yValue) {
        x_ = xValue;
        y_ = yValue;
        hasX_ = true;
        hasY_ = true;
        return *this;
    }
    MarkdownBuilder& margin(float value) {
        margin_ = core::EdgeInsets::all(std::max(0.0f, value));
        return *this;
    }
    MarkdownBuilder& margin(float horizontal, float vertical) {
        margin_ = {std::max(0.0f, horizontal), std::max(0.0f, vertical), std::max(0.0f, horizontal), std::max(0.0f, vertical)};
        return *this;
    }
    MarkdownBuilder& margin(float left, float top, float right, float bottom) {
        margin_ = {std::max(0.0f, left), std::max(0.0f, top), std::max(0.0f, right), std::max(0.0f, bottom)};
        return *this;
    }
    MarkdownBuilder& size(float width, float height) {
        width_ = std::max(0.0f, width);
        height_ = core::SizeValue::fixed(std::max(0.0f, height));
        return *this;
    }
    MarkdownBuilder& width(float value) { width_ = std::max(0.0f, value); return *this; }
    MarkdownBuilder& height(float value) {
        height_ = core::SizeValue::fixed(std::max(0.0f, value));
        return *this;
    }
    MarkdownBuilder& wrapContentHeight() { height_ = core::SizeValue::wrapContent(); return *this; }
    MarkdownBuilder& style(const MarkdownStyle& value) { style_ = value; return *this; }
    MarkdownBuilder& theme(const theme::ThemeColorTokens& tokens) { style_ = MarkdownStyle(tokens); return *this; }
    MarkdownBuilder& zIndex(int value) { zIndex_ = value; return *this; }

    // ── 视口裁剪（DevDesk 池 bug382）──────────────────────────────────────
    // 预览滚动/调窗/全局帧泵会每帧重建整个文档元素树（数千行文本元素），
    // 长文档预览严重卡顿。opt-in 裁剪：设置后窗口外（上下各留 overscan 余量）
    // 的块只建包装元素（显式高 + margin 不变，几何与全量渲染逐像素一致），
    // 跳过其 content（文本/背景/子元素）生成。判定用渲染路径自身的显式高度
    // 推进列流游标，无独立几何表 → 不存在「裁剪后错位」漂移面。
    // 未设置（默认）时 culling()=false，全部 content 照常生成，行为不变。
    MarkdownBuilder& visibleRange(float top, float bottom) {
        cullTop_ = top;
        cullBottom_ = bottom;
        return *this;
    }

    void build() {
        const std::shared_ptr<const detail::MarkdownDocument> document = parsedDocument();
        const std::vector<detail::MarkdownBlock>& blocks = document->blocks;
        detail::MarkdownPerfStats& perf = detail::markdownPerfStats();
        ++perf.buildCalls;
        perf.blocksVisited += static_cast<long long>(blocks.size());
        const auto buildT0 = std::chrono::steady_clock::now();
        flowCursorY_ = 0.0f;  // bug382 裁剪游标：build() 帧内生命周期
        // bug399 round9 / debt398 P2①：快照键指针身份防御。runs 向量跨帧可能被
        // 重建到同一地址（悬空指针复用）——每帧清快照的 runs 指针，首查必走
        // cachedInlineLayout（内容哈希键），成本 0，杜绝错配陈旧布局。
        lastInlineLayout_.runs = nullptr;
        auto root = ui_.column(id_)
            .width(width_)
            .height(height_)
            .margin(margin_.left, margin_.top, margin_.right, margin_.bottom)
            .gap(style_.blockGap)
            .zIndex(zIndex_);
        if (hasX_) {
            root.x(x_);
        }
        if (hasY_) {
            root.y(y_);
        }
        root.content([&] {
                for (std::size_t index = 0; index < blocks.size(); ++index) {
                    if (index > 0) {
                        flowCursorY_ += style_.blockGap;  // 列 gap 计入游标
                    }
                    if (blocks[index].kind == detail::MarkdownBlockKind::TableRow) {
                        std::size_t end = index + 1;
                        while (end < blocks.size() && blocks[end].kind == detail::MarkdownBlockKind::TableRow) {
                            ++end;
                        }
                        renderTable(blocks, index, end);
                        index = end - 1;
                        continue;
                    }
                    renderBlock(blocks[index], index);
                }
            })
            .build();
        perf.buildMs += detail::markdownPerfMs(buildT0);
    }

private:
    // ── 视口裁剪内部件（bug382）── 判定与游标推进均用渲染路径已算出的
    // 显式高度；包装元素（显式高 + margin）不裁，裁的只有 content。
    bool culling() const { return cullBottom_ > cullTop_; }
    bool contentVisibleAt(float y, float h) const {
        if (!culling()) {
            return true;
        }
        // 上下各留 ~1.5 屏余量：滚动/调窗跨帧不闪白（模块侧同口径「上1下2屏」）。
        constexpr float kOverscanPx = 1200.0f;
        const bool visible = y + h > cullTop_ - kOverscanPx && y < cullBottom_ + kOverscanPx;
        if (!visible) {
            ++detail::markdownPerfStats().blocksCulled;
        }
        return visible;
    }
    void advanceFlow(float flowHeight) { flowCursorY_ += flowHeight; }

    std::shared_ptr<const detail::MarkdownDocument> parsedDocument() const {
        return detail::cachedMarkdownDocument(markdown_);
    }

    // ── 页内搜索缝内部件（与上方 public 静态 API 配对）─────────────────

    // 同行相邻矩形连续化（bug259-G 二轮复核）：CJK 经 splitMarkdownTextForLayout
    // 单码点成段、词间空白丢段，同一 span 的相邻段矩形之间会留 layout 间隙
    // （4px/空格）→ 高亮视觉碎裂。同行（y/h 一致）且水平相邻（缝隙 ≤ 8px，
    // 即 layout gap + 浮点余量；正常 intra-span 缝隙恒为 4px）的矩形合并：
    // 前段右缘扩展到后段左缘，只吞字间空白，不吞下一字墨迹（后段左缘即其
    // 首字墨迹左界）；跨行 / 跨块（y 不同）不合并。块局部（hitRectsForDisplay
    // Range）与文档系（hitRectsForDisplayRangeDoc）两口共用 = 合并行为零漂移
    //（need345 抽出，原内联逻辑原样平移）。
    static void mergeAdjacentRectsOnSameLine(std::vector<MarkdownHitRect>& rects) {
        constexpr float kMergeGapPx = 8.0f;
        std::vector<MarkdownHitRect> merged;
        merged.reserve(rects.size());
        for (const MarkdownHitRect& r : rects) {
            if (!merged.empty()) {
                MarkdownHitRect& prev = merged.back();
                if (std::fabs(prev.y - r.y) <= 0.5f &&
                    std::fabs(prev.h - r.h) <= 0.5f &&
                    r.x + r.w > prev.x &&
                    r.x - (prev.x + prev.w) <= kMergeGapPx) {
                    const float right = std::max(prev.x + prev.w, r.x + r.w);
                    prev.w = std::max(1.0f, right - prev.x);
                    continue;
                }
            }
            merged.push_back(r);
        }
        rects = std::move(merged);
    }

    static std::size_t countCodepoints(const std::string& text) {
        std::size_t n = 0;
        for (std::size_t i = 0; i < text.size();) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            const std::size_t step = c >= 0xF0U ? 4 : c >= 0xE0U ? 3 : c >= 0xC0U ? 2 : 1;
            i += step;
            ++n;
        }
        return n;
    }

    static std::string firstNCodepoints(const std::string& text, std::size_t n) {
        std::size_t i = 0;
        for (std::size_t k = 0; k < n && i < text.size(); ++k) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            const std::size_t step = c >= 0xF0U ? 4 : c >= 0xE0U ? 3 : c >= 0xC0U ? 2 : 1;
            i += std::min(step, text.size() - i);
        }
        return text.substr(0, i);
    }

    // 块显示文本的唯一枚举源：displayTextOf 与 hitRectsForDisplayRange 共用，
    // 两口径零漂移（见 displayTextOf 注释）。
    static void appendBlockDisplayText(const detail::MarkdownBlock& b, std::string& out) {
        if (b.kind == detail::MarkdownBlockKind::Divider) {
            return;
        }
        if (b.kind == detail::MarkdownBlockKind::Code) {
            out += detail::codeBlockDisplayText(b);
            return;
        }
        if (b.kind == detail::MarkdownBlockKind::Html) {
            out += detail::plainText(b.runs);
            return;
        }
        if (b.kind == detail::MarkdownBlockKind::TableRow) {
            for (const detail::MarkdownTableCell& cell : b.cells) {
                for (const detail::MarkdownRun& run : cell.runs) {
                    out += detail::displayText(run);
                }
            }
            return;
        }
        if (b.kind == detail::MarkdownBlockKind::ListItem) {
            out += detail::markdownListPrefix(b);
        }
        for (const detail::MarkdownRun& run : b.runs) {
            out += detail::displayText(run);
        }
    }

    // 一段 run 序列（真实 inline 布局）→ 命中矩形；cp 全局游标原地推进。
    static void rectsForInlineRuns(std::vector<MarkdownHitRect>& rects,
                                   const std::vector<detail::MarkdownRun>& runs,
                                   float textWidth,
                                   float fontSize,
                                   float lineHeight,
                                   bool heading,
                                   const MarkdownStyle& style,
                                   float xOff,
                                   float yOff,
                                   std::size_t& cp,
                                   std::size_t cpBegin,
                                   std::size_t cpEnd) {
        const std::shared_ptr<const InlineLayoutEntry> layout =
            cachedInlineLayout(runs, textWidth, fontSize, lineHeight, heading,
                               core::HorizontalAlign::Left, style);
        const std::vector<InlineSegment>& segments = layout->segments;
        // 码点域对齐（bug259-G 第二刀）：matches 的码点域 = displayText 串接流
        // （含空白）；而 layout 的 segment 经 splitMarkdownTextForLayout 生成，
        // 空白全部丢弃、'\n' 折行也不成段——旧实现按「segment 码点连减」映射，
        // 每遇一个被丢空白漂 +1 码点（逐词右移 / 溢出词尾 / 矩形断裂露缝）。
        // 现改为在显示流上按序消费 segment：跳过空白后逐段对齐，seg.x/前缀
        // measure 与渲染同源（渲染 x = seg.x + 段内 glyph advance，同一 measure
        // 函数），矩形边缘零漂移。
        std::string stream;
        for (const detail::MarkdownRun& run : runs) {
            stream += detail::displayText(run);
        }
        std::size_t byte = 0;  // 流内字节游标
        std::size_t pos = 0;   // 流内码点游标（= matches 域）
        auto cpStep = [](const std::string& s, std::size_t i) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            const std::size_t step =
                c >= 0xF0U ? 4 : c >= 0xE0U ? 3 : c >= 0xC0U ? 2 : 1;
            return std::min(step, s.size() - i);
        };
        for (const InlineSegment& seg : segments) {
            if (seg.text.empty() || seg.text == "\n") {
                continue;
            }
            // 跳过被 layout 丢弃的空白（含 '\n'：折行不成段）
            while (byte < stream.size()) {
                const unsigned char c = static_cast<unsigned char>(stream[byte]);
                if (std::isspace(c) == 0) {
                    break;
                }
                ++pos;
                byte += cpStep(stream, byte);
            }
            const std::size_t segBegin = pos;
            const std::size_t segCps = countCodepoints(seg.text);
            // 防御：segment 文本应与流严格对齐（同一 runs 派生）；失配（异常
            // 文档/未来语义漂移）时按段长消费游标但【不出矩形】——硬验收是
            // 「不得覆盖任何非命中字」，错位段宁漏勿滥。
            const bool aligned =
                stream.compare(byte, seg.text.size(), seg.text) == 0;
            byte = std::min(stream.size(), byte + seg.text.size());
            pos += segCps;
            const std::size_t localStart =
                cpBegin > segBegin ? std::min(cpBegin - segBegin, segCps) : 0;
            const std::size_t localEnd =
                cpEnd > segBegin ? std::min(cpEnd - segBegin, segCps) : 0;
            if (aligned && localEnd > localStart) {
                const bool code = seg.run.kind == detail::MarkdownRunKind::InlineCode;
                const bool chip = runIsChip(seg.run);
                const std::string family = code ? style.codeFontFamily : style.fontFamily;
                const float runFontSize = code ? std::max(1.0f, fontSize - 1.0f) : fontSize;
                const int weight = heading || seg.run.style.strong ? 700 : 400;
                const float chipInset = chip ? 6.0f : 0.0f;
                // 边界口径（need264 复核修正）：chip 文本真实绘制在 seg.x+6、宽
                // seg.width-12（renderInlineSegment textInset），段内前缀边界 =
                // chipInset + 前缀 measure——旧式在内层再 +6 是双重内衬，首字形
                // 盒虚胖 12px 且内部边界右偏 6px（高亮/选区盖到邻字）。
                const float x0 = seg.x + chipInset +
                    (localStart == 0
                         ? 0.0f
                         : std::min(seg.width,
                                    detail::measureMarkdownTextWidth(
                                        firstNCodepoints(seg.text, localStart),
                                        family, runFontSize, weight)));
                const float x1 = (localEnd >= segCps)
                    ? seg.x + seg.width  // 段尾 = segment 右缘（chip 含右内衬，不再外扩）
                    : seg.x + chipInset +
                          std::min(seg.width,
                                   detail::measureMarkdownTextWidth(
                                       firstNCodepoints(seg.text, localEnd),
                                       family, runFontSize, weight));
                MarkdownHitRect r;
                r.x = xOff + x0;
                r.y = yOff + seg.y;
                r.w = std::max(1.0f, x1 - x0);
                r.h = lineHeight;
                rects.push_back(r);
            }
        }
        cp += countCodepoints(stream);
    }

    // 单个 md4c 块（Paragraph/Heading/Quote/ListItem）：渲染同参矩形反解。
    static void rectsForInlineBlock(std::vector<MarkdownHitRect>& rects,
                                    const detail::MarkdownBlock& b,
                                    float contentWidth,
                                    const MarkdownStyle& style,
                                    float insetX,
                                    std::size_t& cp,
                                    std::size_t cpBegin,
                                    std::size_t cpEnd) {
        const bool heading = b.kind == detail::MarkdownBlockKind::Heading;
        const float fontSize = heading ? detail::markdownHeadingSize(style, b.headingLevel)
                                       : style.bodySize;
        const float lineHeight = heading ? fontSize + 6.0f : style.bodyLineHeight;
        float xOff = insetX;
        float yOff = 0.0f;
        float textWidth = contentWidth;
        if (b.quoteDepth > 0) {
            const float panelX = 3.0f + 8.0f;
            const float textX = panelX + style.quotePadding;
            textWidth = std::max(0.0f, contentWidth - panelX - style.quotePadding * 2.0f);
            xOff += textX;
            yOff = style.quotePadding;
        }
        std::size_t beginLocal = cpBegin;
        std::size_t endLocal = cpEnd;
        if (b.kind == detail::MarkdownBlockKind::ListItem) {
            const float markerWidth = b.task ? 34.0f : 24.0f;
            const std::string prefix = detail::markdownListPrefix(b);
            // marker 码点域对齐（need264 复核）：displayTextOf 把 prefix 计入显示
            // 文本，渲染也真实画出 .mark 文本（renderListItem，右对齐 markerWidth
            // 盒 + 内容起点 markerWidth+8）——矩形按该盒补齐，否则 marker 之后的
            // 命中/选区在 rect 域整体左漂 prefixCps 码点。
            const std::size_t prefixCps = countCodepoints(prefix);
            // marker 查询域 = 块局部显示域（cpBegin/cpEnd 自块首 0 起算）：单块缝
            // 块基址=0 时 std::max(cpBegin, cp) 退化等价；文档缝（need345）块基址
            // =cp>0，旧式把基址当域内偏移 → marker 矩形永不发射。矩形保持 marker
            // 整盒（右对齐字形盒；多码点 prefix 的逐码点往返由
            // resolveCpInInlineBlock 半宽分桶吸收，单码点 prefix 精确）。
            const std::size_t mBegin = cpBegin;
            const std::size_t mEnd = std::min(cpEnd, prefixCps);
            if (mEnd > mBegin) {
                rects.push_back(MarkdownHitRect{xOff, yOff, markerWidth,
                                                style.bodyLineHeight});
            }
            cp += prefixCps;
            // 内容段域换算：rectsForInlineRuns 的 segment 域自 0 起（纯内容），
            // 查询区间须自显示域平移 prefixCps——否则列表内容高亮/选区整体错位
            // prefixCps 码点（旧实现两处同时漂：marker 无矩形 + 内容不平移）。
            beginLocal = cpBegin > prefixCps ? cpBegin - prefixCps : 0;
            endLocal = cpEnd > prefixCps ? cpEnd - prefixCps : 0;
            textWidth = std::max(0.0f, contentWidth - markerWidth - 8.0f);
            xOff += markerWidth + 8.0f;
        }
        rectsForInlineRuns(rects, b.runs, textWidth, fontSize, lineHeight,
                           heading, style, xOff, yOff, cp, beginLocal, endLocal);
    }

    // Code/Html：ui.text .wrap() 真排无 inline 布局可查，矩形按 estimateMarkdownTextHeight
    // 同式虚拟行近似（声明处的已知近似；等宽代码下贴合）。计账口径 = 码点
    // （need264 P1 复核：旧 walk 按字节计（piece.size()/lineLen），CJK 注释代码块
    // 的 hit.cp 膨胀为字节偏移 → 与显示文本域/选区切片错位越尾；ASCII 块逐位不变）。
    static void rectsForPlainTextBlock(std::vector<MarkdownHitRect>& rects,
                                       const detail::MarkdownBlock& b,
                                       float contentWidth,
                                       const MarkdownStyle& style,
                                       float insetX,
                                       std::size_t& cp,
                                       std::size_t cpBegin,
                                       std::size_t cpEnd) {
        const std::string text = b.kind == detail::MarkdownBlockKind::Code
                                     ? detail::codeBlockDisplayText(b)
                                     : detail::plainText(b.runs);
        const std::size_t total = countCodepoints(text);
        const std::size_t blockBegin = cp;
        cp += total;
        if (blockBegin >= cpEnd || blockBegin + total <= cpBegin) {
            return;
        }
        const float textWidth =
            std::max(0.0f, contentWidth - style.codePadding * 2.0f);
        const float lh = style.codeSize + 6.0f;
        const float avgW = std::max(1.0f, style.codeSize * 0.56f);
        const std::size_t charsPerLine = std::max<std::size_t>(
            1, static_cast<std::size_t>(
                   std::floor(std::max(1.0f, textWidth) / avgW)));
        float y = static_cast<float>(style.codePadding);
        std::size_t walked = 0;  // 码点计（虚拟行块游标）
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t nl = text.find('\n', start);
            const std::size_t end = nl == std::string::npos ? text.size() : nl;
            const std::string piece = text.substr(start, end - start);
            const std::size_t pieceCps = countCodepoints(piece);
            std::size_t inPiece = 0;
            while (inPiece < pieceCps) {
                const std::size_t lineLen =
                    std::min(pieceCps - inPiece, charsPerLine);
                const std::size_t lineBegin = blockBegin + walked;
                const std::size_t localStart =
                    cpBegin > lineBegin ? std::min(cpBegin - lineBegin, lineLen) : 0;
                const std::size_t localEnd =
                    cpEnd > lineBegin ? std::min(cpEnd - lineBegin, lineLen) : 0;
                if (localEnd > localStart) {
                    MarkdownHitRect r;
                    r.x = insetX + style.codePadding +
                          static_cast<float>(localStart) * avgW;
                    r.y = y;
                    r.w = std::max(1.0f, static_cast<float>(localEnd - localStart) * avgW);
                    r.h = lh;
                    rects.push_back(r);
                }
                walked += lineLen;
                y += lh;
                inPiece += lineLen;
            }
            if (nl == std::string::npos) {
                break;
            }
            walked += 1;  // '\n' 本身（1 码点）
            start = nl + 1;
        }
    }

    // 表格：cell 级近似（包围盒高亮，见 displayTextOf 已知近似）；返回推进后的
    // 全局码点游标。码点域口径（need264 复核对齐）：行显示文本 = 行内 cell 直接
    // 串接（appendBlockDisplayText 同式，cell 间无分隔）、行间 '\n'——旧实现按
    // cell 逐个计账，与 displayTextOf（行串接）漂 (cells-1)×rows 级码点，表格内
    // 及其后命中/选区映射整体错位。
    static std::size_t rectsForTable(std::vector<MarkdownHitRect>& rects,
                                     const std::vector<detail::MarkdownBlock>& blocks,
                                     std::size_t begin,
                                     std::size_t end,
                                     float contentWidth,
                                     const MarkdownStyle& style,
                                     float insetX,
                                     std::size_t cp,
                                     std::size_t cpBegin,
                                     std::size_t cpEnd) {
        float y = 0.0f;
        for (std::size_t row = begin; row < end; ++row) {
            const detail::MarkdownBlock& b = blocks[row];
            const std::size_t cellCount = std::max<std::size_t>(1, b.cells.size());
            const float cellWidth = contentWidth / static_cast<float>(cellCount);
            float rowHeight = 0.0f;
            for (std::size_t cell = 0; cell < cellCount; ++cell) {
                const std::vector<detail::MarkdownRun>& runs =
                    cell < b.cells.size() ? b.cells[cell].runs
                                          : std::vector<detail::MarkdownRun>{};
                const float textWidth =
                    std::max(0.0f, cellWidth - style.tableCellPadding * 2.0f);
                rowHeight = std::max(
                    rowHeight,
                    cachedInlineLayout(runs, textWidth, style.bodySize,
                                       style.bodyLineHeight, b.tableHeader,
                                       core::HorizontalAlign::Left, style)
                        ->measuredHeight);
            }
            rowHeight += style.tableCellPadding * 2.0f;
            // 行显示码点与 cell 前缀表（cell 串接域内各 cell 的子区间）
            std::vector<std::size_t> cellPrefix(b.cells.size() + 1, 0);
            for (std::size_t cell = 0; cell < b.cells.size(); ++cell) {
                std::string cellText;
                for (const detail::MarkdownRun& run : b.cells[cell].runs) {
                    cellText += detail::displayText(run);
                }
                cellPrefix[cell + 1] =
                    cellPrefix[cell] + countCodepoints(cellText);
            }
            const std::size_t rowCps = cellPrefix.back();
            const std::size_t rowBegin = cp;
            cp += rowCps;
            if (row + 1 < end) {
                ++cp;  // 行间 '\n'（displayTextOf 块间分隔同式）
            }
            if (rowCps == 0 || rowBegin >= cpEnd || rowBegin + rowCps <= cpBegin) {
                continue;
            }
            for (std::size_t cell = 0; cell < b.cells.size(); ++cell) {
                const std::size_t cellBegin = rowBegin + cellPrefix[cell];
                const std::size_t cellEnd = rowBegin + cellPrefix[cell + 1];
                if (cellEnd <= cpBegin || cellBegin >= cpEnd) {
                    continue;
                }
                MarkdownHitRect r;
                r.x = insetX + static_cast<float>(cell) * cellWidth;
                r.y = y;
                r.w = cellWidth;
                r.h = rowHeight;
                rects.push_back(r);
            }
            y += rowHeight;
        }
        return cp;
    }

    // ── 点→码点反查内部件（与 displayCpForPoint 配对）────────────────────

    // 渲染单元（md4c 子块 / 表格行组）的显示文本码点数：直接借
    // appendBlockDisplayText 拼文本再计数（行组行间 '\n' 与 displayTextOf 同式）。
    static std::size_t blockDisplayCp(const std::vector<detail::MarkdownBlock>& blocks,
                                      std::size_t begin, std::size_t end) {
        std::string text;
        for (std::size_t row = begin; row < end; ++row) {
            if (row > begin) {
                text += '\n';
            }
            appendBlockDisplayText(blocks[row], text);
        }
        return countCodepoints(text);
    }

    // 命中子块内的点→局部码点解析：按块种类分派到与 rects* 同参数的解析件。
    static void resolveCpInBlock(const std::vector<detail::MarkdownBlock>& blocks,
                                 std::size_t index, std::size_t groupEnd,
                                 float width, const MarkdownStyle& style,
                                 float x, float yLocal, std::size_t cpBase,
                                 MarkdownPointHit& hit) {
        const detail::MarkdownBlock& b = blocks[index];
        const float quoteInset =
            static_cast<float>(std::max(0, b.quoteDepth)) * 8.0f;
        const float listInset =
            static_cast<float>(std::max(0, b.listDepth - 1)) * style.listIndent;
        const float contentWidth =
            std::max(0.0f, width - quoteInset - listInset);
        if (b.kind == detail::MarkdownBlockKind::Divider) {
            return;  // 无显示文本（ok 保持 false，cp 无意义）
        }
        hit.ok = true;
        if (b.kind == detail::MarkdownBlockKind::Code ||
            b.kind == detail::MarkdownBlockKind::Html) {
            hit.cp = cpBase + resolveCpInPlainTextBlock(
                b, contentWidth, style, quoteInset + listInset, x, yLocal);
        } else if (b.kind == detail::MarkdownBlockKind::TableRow) {
            hit.cp = resolveCpInTableRowGroup(blocks, index, groupEnd,
                                              contentWidth, style,
                                              quoteInset + listInset, x, yLocal,
                                              cpBase);
        } else {
            hit.cp = cpBase + resolveCpInInlineBlock(b, contentWidth, style,
                                                     quoteInset + listInset,
                                                     x, yLocal);
        }
    }

    // Paragraph/Heading/Quote/ListItem：insets 与 rectsForInlineBlock 同式镜像。
    static std::size_t resolveCpInInlineBlock(const detail::MarkdownBlock& b,
                                              float contentWidth,
                                              const MarkdownStyle& style,
                                              float insetX, float x, float y) {
        const bool heading = b.kind == detail::MarkdownBlockKind::Heading;
        const float fontSize =
            heading ? detail::markdownHeadingSize(style, b.headingLevel)
                    : style.bodySize;
        const float lineHeight =
            heading ? fontSize + 6.0f : style.bodyLineHeight;
        float xOff = insetX;
        float yOff = 0.0f;
        float textWidth = contentWidth;
        if (b.quoteDepth > 0) {
            const float panelX = 3.0f + 8.0f;
            const float textX = panelX + style.quotePadding;
            textWidth = std::max(0.0f, contentWidth - panelX - style.quotePadding * 2.0f);
            xOff += textX;
            yOff = style.quotePadding;
        }
        std::size_t localCp = 0;
        if (b.kind == detail::MarkdownBlockKind::ListItem) {
            const float markerWidth = b.task ? 34.0f : 24.0f;
            const std::string prefix = detail::markdownListPrefix(b);
            localCp = countCodepoints(prefix);
            // 点落在 marker 文本盒（首行带内、x < 内容起点）：marker 渲染为
            // markerWidth 盒内【右对齐】文本（renderListItem .mark），按字形宽
            // 反推桶位——字形右缘贴盒右、左侧留白 → x ≤ 字形左缘 = marker 前
            // （cp0）；字形内逐码点半字宽边界（与 inline 段内同语义）；
            // 字形右与内容起点之间的空隙 → marker 末。折叠行带以下照常走内容域。
            const std::size_t prefixCps = localCp;
            const float contentStart = markerWidth + 8.0f;
            if (prefixCps > 0 && y < yOff + style.bodyLineHeight &&
                x < xOff + contentStart) {
                const float glyphSpan = detail::measureMarkdownTextWidth(
                    prefix, style.fontFamily, style.bodySize, 400);
                const float glyphLeft =
                    xOff + std::max(0.0f, markerWidth - glyphSpan);
                if (x <= glyphLeft) {
                    return 0;
                }
                float acc = 0.0f;
                std::size_t k = 0;
                for (const std::string& part :
                     detail::splitMarkdownTextIntoCodepoints(prefix)) {
                    if (part.empty()) {
                        continue;
                    }
                    const float w = detail::measureMarkdownTextWidth(
                        part, style.fontFamily, style.bodySize, 400);
                    if (acc + w * 0.5f > x - glyphLeft) {
                        break;
                    }
                    acc += w;
                    ++k;
                }
                return std::min(prefixCps, k);
            }
            textWidth = std::max(0.0f, contentWidth - markerWidth - 8.0f);
            xOff += markerWidth + 8.0f;
        }
        return localCp + resolveCpInInlineRuns(b.runs, textWidth, fontSize,
                                               lineHeight, heading, style,
                                               xOff, yOff, x, y);
    }

    // run 序列 → 段定位 + 段内最近前缀边界。段矩形几何与 rectsForInlineRuns 完全
    // 同源（同一 cachedInlineLayout 与显示流走法）；段内用逐码点增量 advance 找
    // 「x 左侧完整字形的最大个数」（半字宽内归前界——最近边界语义）。
    static std::size_t resolveCpInInlineRuns(const std::vector<detail::MarkdownRun>& runs,
                                             float textWidth, float fontSize,
                                             float lineHeight, bool heading,
                                             const MarkdownStyle& style,
                                             float xOff, float yOff,
                                             float x, float y) {
        const std::shared_ptr<const InlineLayoutEntry> layout =
            cachedInlineLayout(runs, textWidth, fontSize, lineHeight, heading,
                               core::HorizontalAlign::Left, style);
        const std::vector<InlineSegment>& segments = layout->segments;
        std::string stream;
        for (const detail::MarkdownRun& run : runs) {
            stream += detail::displayText(run);
        }
        std::size_t byte = 0;  // 流内字节游标（走法与 rectsForInlineRuns 逐行同款）
        std::size_t pos = 0;   // 流内码点游标（= 显示文本域）
        auto cpStep = [](const std::string& s, std::size_t i) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            const std::size_t step =
                c >= 0xF0U ? 4 : c >= 0xE0U ? 3 : c >= 0xC0U ? 2 : 1;
            return std::min(step, s.size() - i);
        };
        const InlineSegment* best = nullptr;
        std::size_t bestBegin = 0;
        std::size_t lastEnd = 0;  // 全段最大 segBegin+segCps（下方越界 → 块末）
        float bestDist = std::numeric_limits<float>::max();
        for (const InlineSegment& seg : segments) {
            if (seg.text.empty() || seg.text == "\n") {
                continue;
            }
            // 跳过被 layout 丢弃的空白（含 '\n' 折行）
            while (byte < stream.size()) {
                const unsigned char c = static_cast<unsigned char>(stream[byte]);
                if (std::isspace(c) == 0) {
                    break;
                }
                ++pos;
                byte += cpStep(stream, byte);
            }
            const std::size_t segBegin = pos;
            const std::size_t segCps = countCodepoints(seg.text);
            const bool aligned =
                stream.compare(byte, seg.text.size(), seg.text) == 0;
            byte = std::min(stream.size(), byte + seg.text.size());
            pos += segCps;
            if (!aligned) {
                continue;  // 失配段不出候选（宁漏勿滥，同 rect 路径）
            }
            lastEnd = std::max(lastEnd, segBegin + segCps);
            const float rx = xOff + seg.x;
            const float ry = yOff + seg.y;
            const float cx = std::clamp(x, rx, rx + seg.width);
            const float cy = std::clamp(y, ry, ry + lineHeight);
            const float dx = cx - x;
            const float dy = cy - y;
            const float dist = dx * dx + dy * dy;
            if (dist < bestDist) {
                bestDist = dist;
                best = &seg;
                bestBegin = segBegin;
            }
        }
        if (best == nullptr) {
            return 0;
        }
        // 纵向越界钳制（need264）：点在全部段所在行之下时直接给块末——「最近行
        // 再取段内边界」会把下方越界钳到末行首段段尾（非块末）；上方越界语义上
        // 即块首 0（首段 segBegin 恒 0，无需记录）。
        if (y > yOff + best->y + lineHeight) {
            return lastEnd;
        }
        if (y < yOff + best->y) {
            return 0;
        }
        const bool code = best->run.kind == detail::MarkdownRunKind::InlineCode;
        const bool chip = runIsChip(best->run);
        const std::string family = code ? style.codeFontFamily : style.fontFamily;
        const float runFontSize = code ? std::max(1.0f, fontSize - 1.0f) : fontSize;
        const int weight = heading || best->run.style.strong ? 700 : 400;
        const float chipInset = chip ? 6.0f : 0.0f;
        const float xLocal = x - (xOff + best->x + chipInset);
        const std::size_t segCps = countCodepoints(best->text);
        if (xLocal <= 0.0f) {
            return bestBegin;
        }
        if (chip && xLocal >= best->width - chipInset * 2.0f) {
            return bestBegin + segCps;  // chip 右内衬之外 → 段尾
        }
        float acc = 0.0f;
        std::size_t k = 0;
        for (const std::string& part :
             detail::splitMarkdownTextIntoCodepoints(best->text)) {
            if (part.empty()) {
                continue;
            }
            const float w =
                detail::measureMarkdownTextWidth(part, family, runFontSize, weight);
            if (acc + w * 0.5f > xLocal) {
                break;  // 落点进字形前半 → 边界归该字之前（最近边界）
            }
            acc += w;
            ++k;
        }
        return bestBegin + std::min(k, segCps);
    }

    // Code/Html：虚拟行近似行定位（与 rectsForPlainTextBlock 的行切分逐行同构：
    // y = codePadding + lineIdx×lh，行内字符位 = (x - insetX - codePadding)/avgW）。
    // 计账口径 = 码点（need264 P1 复核，与 rect 路径同刀：旧按字节计对 CJK 注释
    // 代码块 hit.cp 膨胀为字节偏移）。
    static std::size_t resolveCpInPlainTextBlock(const detail::MarkdownBlock& b,
                                                 float contentWidth,
                                                 const MarkdownStyle& style,
                                                 float insetX, float x,
                                                 float y) {
        const std::string text = b.kind == detail::MarkdownBlockKind::Code
                                     ? detail::codeBlockDisplayText(b)
                                     : detail::plainText(b.runs);
        const float textWidth =
            std::max(0.0f, contentWidth - style.codePadding * 2.0f);
        const float lh = style.codeSize + 6.0f;
        const float avgW = std::max(1.0f, style.codeSize * 0.56f);
        const std::size_t charsPerLine = std::max<std::size_t>(
            1, static_cast<std::size_t>(
                   std::floor(std::max(1.0f, textWidth) / avgW)));
        const std::size_t lineIdx =
            y <= style.codePadding
                ? 0
                : static_cast<std::size_t>(
                      std::floor((y - style.codePadding) / lh));
        std::size_t walked = 0;  // 码点计
        std::size_t line = 0;    // 虚拟行序号（与 rect 路径 chunk 序一致）
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t nl = text.find('\n', start);
            const std::size_t end = nl == std::string::npos ? text.size() : nl;
            const std::string piece = text.substr(start, end - start);
            const std::size_t pieceCps = countCodepoints(piece);
            std::size_t inPiece = 0;
            while (inPiece < pieceCps) {
                const std::size_t lineLen =
                    std::min(pieceCps - inPiece, charsPerLine);
                if (line == lineIdx) {
                    const float charX = x - (insetX + style.codePadding);
                    const std::size_t off =
                        charX <= 0.0f
                            ? 0
                            : std::min(lineLen,
                                       static_cast<std::size_t>(
                                           std::floor(charX / avgW)));
                    return walked + off;
                }
                walked += lineLen;
                ++line;
                inPiece += lineLen;
            }
            if (nl == std::string::npos) {
                break;  // 末 piece 无 '\n'（与 rect 路径同式：钳制不计幻影换行）
            }
            walked += 1;  // '\n' 本身（与 rect 路径同式，1 码点；不推进虚拟行）
            start = nl + 1;
        }
        return walked;  // y 越过末虚拟行 → 文本末（钳制语义）
    }

    // 表格行组：行/单元格定位与 rectsForTable 几何同式（cellWidth 均分、行高
    // = cell 布局最大 measuredHeight + 2×padding）；cell 内走 inline 解析。
    static std::size_t resolveCpInTableRowGroup(
        const std::vector<detail::MarkdownBlock>& blocks, std::size_t begin,
        std::size_t end, float contentWidth, const MarkdownStyle& style,
        float insetX, float x, float y, std::size_t cpBase) {
        float yRow = 0.0f;
        std::size_t cp = 0;  // 组内局部码点游标（含行间 '\n'）
        for (std::size_t row = begin; row < end; ++row) {
            const detail::MarkdownBlock& b = blocks[row];
            const std::size_t cellCount = std::max<std::size_t>(1, b.cells.size());
            const float cellWidth = contentWidth / static_cast<float>(cellCount);
            float rowHeight = 0.0f;
            for (std::size_t cell = 0; cell < cellCount; ++cell) {
                const std::vector<detail::MarkdownRun>& runs =
                    cell < b.cells.size() ? b.cells[cell].runs
                                          : std::vector<detail::MarkdownRun>{};
                rowHeight = std::max(
                    rowHeight,
                    cachedInlineLayout(runs,
                                       std::max(0.0f, cellWidth - style.tableCellPadding * 2.0f),
                                       style.bodySize, style.bodyLineHeight,
                                       b.tableHeader, core::HorizontalAlign::Left,
                                       style)
                        ->measuredHeight);
            }
            rowHeight += style.tableCellPadding * 2.0f;
            if (y >= yRow && y < yRow + rowHeight && !b.cells.empty()) {
                const float xLocal = x - insetX;
                const std::size_t cellIdx =
                    xLocal <= 0.0f
                        ? 0
                        : std::min(cellCount - 1,
                                   static_cast<std::size_t>(std::floor(xLocal / cellWidth)));
                if (cellIdx < b.cells.size()) {
                    std::size_t cellBase = cp;  // 本行内目标 cell 之前的 cell 码点
                    for (std::size_t cell = 0; cell < cellIdx; ++cell) {
                        std::string cellText;
                        for (const detail::MarkdownRun& run : b.cells[cell].runs) {
                            cellText += detail::displayText(run);
                        }
                        cellBase += countCodepoints(cellText);
                    }
                    const std::size_t local =
                        resolveCpInInlineRuns(b.cells[cellIdx].runs,
                                              std::max(0.0f, cellWidth - style.tableCellPadding * 2.0f),
                                              style.bodySize, style.bodyLineHeight,
                                              b.tableHeader, style,
                                              insetX + static_cast<float>(cellIdx) * cellWidth +
                                                  style.tableCellPadding,
                                              yRow + style.tableCellPadding,
                                              x, y);
                    return cpBase + cellBase + local;
                }
            }
            yRow += rowHeight;
            std::string rowText;
            for (std::size_t cell = 0; cell < b.cells.size(); ++cell) {
                for (const detail::MarkdownRun& run : b.cells[cell].runs) {
                    rowText += detail::displayText(run);
                }
            }
            cp += countCodepoints(rowText);
            if (row + 1 < end) {
                ++cp;  // 行间 '\n'
            }
        }
        return cpBase + cp;  // y 越过末行 → 行组文本末（钳制语义）
    }

    static bool runIsChip(const detail::MarkdownRun& run) {
        return run.kind == detail::MarkdownRunKind::InlineCode ||
               run.kind == detail::MarkdownRunKind::Image ||
               run.kind == detail::MarkdownRunKind::Math ||
               run.kind == detail::MarkdownRunKind::WikiLink ||
               run.kind == detail::MarkdownRunKind::Html ||
               run.style.html;
    }

    static float estimateInlineSegmentWidth(const detail::MarkdownRun& run,
                                            const std::string& text,
                                            float maxWidth,
                                            float fontSize,
                                            bool heading,
                                            const MarkdownStyle& style) {
        if (text == "\n") {
            return maxWidth;
        }
        const bool code = run.kind == detail::MarkdownRunKind::InlineCode;
        const bool chip = runIsChip(run);
        const std::string fontFamily = code ? style.codeFontFamily : style.fontFamily;
        const float runFontSize = code ? std::max(1.0f, fontSize - 1.0f) : fontSize;
        const int weight = heading || run.style.strong ? 700 : 400;
        const float padding = chip ? 12.0f : 0.0f;
        return std::min(maxWidth, detail::measureMarkdownTextWidth(text, fontFamily, runFontSize, weight) + padding);
    }

    struct InlineSegment {
        detail::MarkdownRun run;
        std::string text;
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        bool chip = false;
    };

    struct InlineLayoutEntry {
        std::vector<InlineSegment> segments;
        float measuredHeight = 0.0f;
    };

    struct InlineLayoutCacheEntry {
        std::shared_ptr<const InlineLayoutEntry> layout;
        std::size_t lastUsed = 0;
        std::size_t bytes = 0;
    };

    struct InlineLayoutCache {
        std::unordered_map<std::string, InlineLayoutCacheEntry> entries;
        std::size_t accessTick = 0;
        std::size_t bytes = 0;
    };

    static InlineLayoutCache& sharedInlineLayoutCache() {
        static thread_local InlineLayoutCache cache;
        return cache;
    }

    static void appendInlineLayoutKey(std::string& key, std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            key.push_back(static_cast<char>((value >> shift) & 0xFFU));
        }
    }

    static void appendInlineLayoutKey(std::string& key, float value) {
        static_assert(sizeof(float) == sizeof(std::uint32_t));
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        appendInlineLayoutKey(key, bits);
    }

    static void appendInlineLayoutKey(std::string& key, const std::string& value) {
        appendInlineLayoutKey(key, static_cast<std::uint32_t>(value.size()));
        key.append(value);
    }

    // bug399 round9 / debt398 P1-A：段间水平 gap 常量。布局推进 cursorX 时，非行首
    // 段前会先 += gap（见 layoutInlineRunsWithStyle），因此「同段相邻」判据的右缘
    // 必须含 gap；旧判据漏 gap → 恒差 4px → 段合并从未触发（review 实证）。
    static constexpr float kInlineSegmentGap = 4.0f;

    static std::string inlineLayoutKey(const std::vector<detail::MarkdownRun>& runs,
                                       float width,
                                       float fontSize,
                                       float lineHeight,
                                       bool heading,
                                       core::HorizontalAlign align,
                                       const MarkdownStyle& style) {
        std::string key;
        key.reserve(32 + style.fontFamily.size() + style.codeFontFamily.size());
        appendInlineLayoutKey(key, width);
        appendInlineLayoutKey(key, fontSize);
        appendInlineLayoutKey(key, lineHeight);
        key.push_back(heading ? 1 : 0);
        appendInlineLayoutKey(key, static_cast<std::uint32_t>(align));
        appendInlineLayoutKey(key, style.fontFamily);
        appendInlineLayoutKey(key, style.codeFontFamily);
        appendInlineLayoutKey(key, static_cast<std::uint32_t>(runs.size()));
        for (const detail::MarkdownRun& run : runs) {
            appendInlineLayoutKey(key, static_cast<std::uint32_t>(run.kind));
            key.push_back(run.style.emphasis ? 1 : 0);
            key.push_back(run.style.strong ? 1 : 0);
            key.push_back(run.style.deleted ? 1 : 0);
            key.push_back(run.style.underline ? 1 : 0);
            key.push_back(run.style.link ? 1 : 0);
            key.push_back(run.style.image ? 1 : 0);
            key.push_back(run.style.math ? 1 : 0);
            key.push_back(run.style.wiki ? 1 : 0);
            key.push_back(run.style.html ? 1 : 0);
            appendInlineLayoutKey(key, run.text);
            appendInlineLayoutKey(key, run.href);
            appendInlineLayoutKey(key, run.title);
        }
        return key;
    }

    static std::size_t inlineLayoutCacheBytes(const InlineLayoutEntry& layout, std::size_t keyBytes) {
        std::size_t bytes = 0;
        detail::addMarkdownCacheBytes(bytes, sizeof(InlineLayoutEntry));
        detail::addMarkdownCacheBytes(bytes, keyBytes);
        detail::addMarkdownCacheBytes(bytes,
                                      detail::markdownCacheBytesForItems(layout.segments.capacity(), sizeof(InlineSegment)));
        for (const InlineSegment& segment : layout.segments) {
            detail::addMarkdownCacheBytes(bytes, segment.text.capacity());
            detail::addMarkdownCacheBytes(bytes, detail::markdownRunCacheBytes(segment.run));
        }
        return bytes;
    }

    static std::vector<InlineSegment> layoutInlineRunsWithStyle(const std::vector<detail::MarkdownRun>& runs,
                                                                float width,
                                                                float fontSize,
                                                                float lineHeight,
                                                                bool heading,
                                                                const MarkdownStyle& style,
                                                                float& outHeight) {
        std::vector<InlineSegment> segments;
        const float gap = kInlineSegmentGap;
        float cursorX = 0.0f;
        float cursorY = 0.0f;
        bool hasLineItem = false;
        // need324 CJK 行首禁则：本行已拉回的禁则 segment 数，钳制 ≤2 次防极端
        // 溢出；换行/换段（\n）即复位。口径同 text_layout 拉回法。
        int linePullCount = 0;

        auto rawSegmentWidth = [&](const detail::MarkdownRun& run, const std::string& text) {
            if (text == "\n") {
                return width;
            }
            const bool code = run.kind == detail::MarkdownRunKind::InlineCode;
            const bool chip = runIsChip(run);
            const std::string fontFamily = code ? style.codeFontFamily : style.fontFamily;
            const float runFontSize = code ? std::max(1.0f, fontSize - 1.0f) : fontSize;
            const int weight = heading || run.style.strong ? 700 : 400;
            const float padding = chip ? 12.0f : 0.0f;
            return detail::measureMarkdownTextWidth(text, fontFamily, runFontSize, weight) + padding;
        };

        auto pushFittingSegment = [&](detail::MarkdownRun run, std::string text) {
            if (text.empty()) {
                return;
            }
            if (text == "\n") {
                cursorX = 0.0f;
                cursorY += lineHeight;
                hasLineItem = false;
                linePullCount = 0;
                return;
            }
            const bool chip = runIsChip(run);
            const float segmentWidth = estimateInlineSegmentWidth(run, text, width, fontSize, heading, style);
            if (hasLineItem && width > 0.0f && cursorX + gap + segmentWidth > width) {
                // need324：即将起行 segment 首码点 ∈ 禁则集 → 拉回本行行尾
                // （允许超 width；同 text_layout 拉回法与 ≤2 钳制口径）。
                if (linePullCount < 2 &&
                    core::text_detail::isProhibitedLineStart(core::text_detail::decodeUtf8At(text, 0))) {
                    ++linePullCount;
                } else {
                    cursorX = 0.0f;
                    cursorY += lineHeight;
                    hasLineItem = false;
                    linePullCount = 0;
                }
            }
            if (hasLineItem) {
                cursorX += gap;
            }
            // bug399 round6 / debt398：段合并（布局级）——同一视觉 run + 同一行 +
            // 水平紧邻（上段右缘 == 本段左缘，gap=0）的相邻段合并为一个段。
            // CJK 单码点成段是排版语义，不必逐码点成段：合并后 segment 表体积降
            // 1-2 个量级（inline 缓存不再超字节上限反复逐出）、元素数与 shaping 次
            // 数同降；advance 累加使宽度/位置逐位不变（选区/命中走本表不受影响）。
            if (!chip && !segments.empty()) {
                InlineSegment& last = segments.back();
                if (!last.chip && std::fabs(last.y - cursorY) < 0.5f &&
                    std::fabs(last.x + last.width + gap - cursorX) < 0.6f &&
                    last.run.kind == run.kind &&
                    last.run.style.emphasis == run.style.emphasis &&
                    last.run.style.strong == run.style.strong &&
                    last.run.style.deleted == run.style.deleted &&
                    last.run.style.underline == run.style.underline &&
                    last.run.style.link == run.style.link &&
                    last.run.style.image == run.style.image &&
                    last.run.style.math == run.style.math &&
                    last.run.style.wiki == run.style.wiki &&
                    last.run.style.html == run.style.html) {
                    last.text += text;
                    last.width += segmentWidth;
                } else {
                    segments.push_back({std::move(run), std::move(text), cursorX, cursorY, segmentWidth, chip});
                }
            } else {
                segments.push_back({std::move(run), std::move(text), cursorX, cursorY, segmentWidth, chip});
            }
            cursorX += segmentWidth;
            hasLineItem = true;
        };

        auto pushSegment = [&](const detail::MarkdownRun& run, const std::string& text) {
            if (text.empty()) {
                return;
            }
            if (text == "\n" || width <= 0.0f || rawSegmentWidth(run, text) <= width) {
                pushFittingSegment(run, text);
                return;
            }

            // 折行决策用逐码点【增量累加】宽度（bug259-C）：旧实现每个前缀整串
            // 重measure —— 单个长 run O(n²) 次 measure × 每次全前缀 shaping，CJK
            // 长段落单块即百万级字形查表。advance 度量逐码点可加（同字体同字号
            // 纯 advance 求和，无 kerning），累加与前缀测量逐位一致。
            std::string chunk;
            float chunkWidth = 0.0f;
            for (const std::string& part : detail::splitMarkdownTextIntoCodepoints(text)) {
                if (part.empty()) {
                    continue;
                }
                const float partWidth = rawSegmentWidth(run, part);
                if (!chunk.empty() && chunkWidth + partWidth > width) {
                    pushFittingSegment(run, chunk);
                    chunk.clear();
                    chunkWidth = 0.0f;
                }
                chunk += part;
                chunkWidth += partWidth;
            }
            if (!chunk.empty()) {
                pushFittingSegment(run, chunk);
            }
        };

        for (const detail::MarkdownRun& run : runs) {
            const std::string text = detail::displayText(run);
            if (text.empty()) {
                continue;
            }
            if (runIsChip(run)) {
                pushSegment(run, text);
                continue;
            }
            for (const std::string& part : detail::splitMarkdownTextForLayout(text)) {
                pushSegment(run, part);
            }
        }

        outHeight = segments.empty() ? lineHeight : cursorY + lineHeight;
        // bug399 round9 / debt398 P1-A：段表体积回归计量——记录每条新布局的段数
        // 与 run 数（未合并时 CJK 单码点成段，段数远多于 run 数；合并后趋近 1）。
        {
            detail::MarkdownPerfStats& perf = detail::markdownPerfStats();
            ++perf.inlineLayoutBuilds;
            perf.inlineSegmentsBuilt += static_cast<long long>(segments.size());
            perf.inlineRunsBuilt += static_cast<long long>(runs.size());
        }
        return segments;
    }

    static std::shared_ptr<const InlineLayoutEntry> cachedInlineLayout(const std::vector<detail::MarkdownRun>& runs,
                                                                        float width,
                                                                        float fontSize,
                                                                        float lineHeight,
                                                                        bool heading,
                                                                        core::HorizontalAlign align,
                                                                        const MarkdownStyle& style) {
        // 容量 1024/2MB（bug259-C）：md 查看器 183 块 × 乒乓宽度集合（16px 列宽
        // 量化 + 滚动条占位双宽）≈ 400+ 活跃条目，256 容量会在宽度乒乓时 LRU 对半
        // 互踩 → resize 每帧全量重 shaping。条目本身轻（段落 segment 表），翻倍
        // 换回稳态零重测。
        // bug399 round7 实测：53KB/399 块文档的内联布局工作集约 950 条/8MB（每帧
        // estimate + render 双请求），2MB 容量 = 顺序扫描 LRU 纯 thrash——每帧
        // ~690 次未命中重 shaping（620-980ms/帧）。容量须 > 双宽（滚动条占位）
        // 工作集，故 4096 条 / 32MB；条目值轻（段表），命中后每帧仅剩键构造+查找。
        // bug399 round9 / debt398 尺寸族：4096 条 / 32MB 在「多尺寸档切换」下仍
        // thrash——每档 ~1400 条布局（estimate+render 双宽），两档即触顶 4096；从
        // 最大化返回原宽时旧档条目已被 LRU 逐出 → 整篇重 shaping（round9 实测
        // restore 冷帧 1098ms）。抬到 8192 条 / 64MB 容纳 3 档工作集（~4200 条 /
        // ~48MB），返回原尺寸走命中（实测降至 ~17ms）。实际占用 = 条目真实字节，
        // 上限只是准入闸，不预分配。
        constexpr std::size_t kCacheCapacity = 8192;
        constexpr std::size_t kCacheByteCapacity = 64 * 1024 * 1024;
        constexpr std::size_t kEntryByteCapacity = 256 * 1024;
        InlineLayoutCache& cache = sharedInlineLayoutCache();
        detail::MarkdownPerfStats& perf = detail::markdownPerfStats();
        const auto keyT0 = std::chrono::steady_clock::now();
        const std::string key = inlineLayoutKey(runs, width, fontSize, lineHeight, heading, align, style);
        perf.inlineKeyMs += detail::markdownPerfMs(keyT0);
        const auto lookupT0 = std::chrono::steady_clock::now();
        const auto existing = cache.entries.find(key);
        perf.inlineLookupMs += detail::markdownPerfMs(lookupT0);
        perf.inlineCacheEntries = cache.entries.size();
        perf.inlineCacheBytes = cache.bytes;
        if (existing != cache.entries.end()) {
            ++perf.inlineHits;
            existing->second.lastUsed = ++cache.accessTick;
            return existing->second.layout;
        }
        ++perf.inlineMisses;

        auto layout = std::make_shared<InlineLayoutEntry>();
        layout->measuredHeight = lineHeight;
        const auto layoutT0 = std::chrono::steady_clock::now();
        layout->segments = layoutInlineRunsWithStyle(runs, width, fontSize, lineHeight, heading, style, layout->measuredHeight);
        alignInlineSegments(layout->segments, width, align);
        perf.inlineLayoutMs += detail::markdownPerfMs(layoutT0);
        const std::size_t layoutBytes = inlineLayoutCacheBytes(*layout, key.size());
        if (layoutBytes > kEntryByteCapacity) {
            return layout;
        }

        while (!cache.entries.empty() &&
               (cache.entries.size() >= kCacheCapacity || layoutBytes > kCacheByteCapacity - cache.bytes)) {
            const auto oldest = std::min_element(cache.entries.begin(), cache.entries.end(),
                                                 [](const auto& left, const auto& right) {
                                                     return left.second.lastUsed < right.second.lastUsed;
                                                 });
            if (oldest != cache.entries.end()) {
                cache.bytes -= oldest->second.bytes;
                cache.entries.erase(oldest);
            }
        }

        const auto inserted = cache.entries.emplace(key, InlineLayoutCacheEntry{layout, ++cache.accessTick, layoutBytes});
        cache.bytes += layoutBytes;
        perf.inlineCacheEntries = cache.entries.size();
        perf.inlineCacheBytes = cache.bytes;
        return inserted.first->second.layout;
    }

    static float estimateInlineRunsHeight(const std::vector<detail::MarkdownRun>& runs,
                                          float width,
                                          float fontSize,
                                          float lineHeight,
                                          bool heading,
                                          const MarkdownStyle& style) {
        return cachedInlineLayout(runs, width, fontSize, lineHeight, heading,
                                  core::HorizontalAlign::Left, style)->measuredHeight;
    }

    static float estimateTableRowHeight(const detail::MarkdownBlock& block,
                                        float contentWidth,
                                        const MarkdownStyle& style) {
        const std::size_t cellCount = std::max<std::size_t>(1, block.cells.size());
        const float cellWidth = contentWidth / static_cast<float>(cellCount);
        const float textWidth = std::max(0.0f, cellWidth - style.tableCellPadding * 2.0f);
        float height = 0.0f;
        for (std::size_t cell = 0; cell < cellCount; ++cell) {
            height = std::max(height, estimateInlineRunsHeight(
                cell < block.cells.size() ? block.cells[cell].runs : std::vector<detail::MarkdownRun>{},
                textWidth,
                style.bodySize,
                style.bodyLineHeight,
                false,
                style));
        }
        return height + style.tableCellPadding * 2.0f;
    }

    static float estimateTableHeight(const std::vector<detail::MarkdownBlock>& blocks,
                                     std::size_t begin,
                                     std::size_t end,
                                     float width,
                                     const MarkdownStyle& style) {
        if (begin >= end) {
            return 0.0f;
        }
        const detail::MarkdownBlock& first = blocks[begin];
        const float quoteInset = static_cast<float>(std::max(0, first.quoteDepth)) * 8.0f;
        const float listInset = static_cast<float>(std::max(0, first.listDepth - 1)) * style.listIndent;
        const float contentWidth = std::max(0.0f, width - quoteInset - listInset);
        float height = 0.0f;
        for (std::size_t row = begin; row < end; ++row) {
            height += estimateTableRowHeight(blocks[row], contentWidth, style);
        }
        return height;
    }

    static float estimateBlockHeight(const detail::MarkdownBlock& block,
                                     float width,
                                     const MarkdownStyle& style) {
        const float quoteInset = static_cast<float>(std::max(0, block.quoteDepth)) * 8.0f;
        const float listInset = static_cast<float>(std::max(0, block.listDepth - 1)) * style.listIndent;
        const float contentWidth = std::max(0.0f, width - quoteInset - listInset);

        if (block.kind == detail::MarkdownBlockKind::Divider) {
            return 9.0f;
        }
        if (block.kind == detail::MarkdownBlockKind::Code || block.kind == detail::MarkdownBlockKind::Html) {
            const std::string text = block.kind == detail::MarkdownBlockKind::Code
                ? detail::codeBlockDisplayText(block)
                : detail::plainText(block.runs);
            const float textWidth = std::max(0.0f, contentWidth - style.codePadding * 2.0f);
            return detail::estimateMarkdownTextHeight(text, textWidth, style.codeSize, style.codeSize + 6.0f) + style.codePadding * 2.0f;
        }
        if (block.kind == detail::MarkdownBlockKind::ListItem) {
            const float markerWidth = block.task ? 34.0f : 24.0f;
            const float textWidth = std::max(0.0f, contentWidth - markerWidth - 8.0f);
            return std::max(style.bodyLineHeight, estimateInlineRunsHeight(block.runs, textWidth, style.bodySize, style.bodyLineHeight, false, style));
        }

        const bool heading = block.kind == detail::MarkdownBlockKind::Heading;
        const float fontSize = heading ? detail::markdownHeadingSize(style, block.headingLevel) : style.bodySize;
        const float lineHeight = heading ? fontSize + 6.0f : style.bodyLineHeight;
        if (block.quoteDepth > 0) {
            const float barWidth = 3.0f;
            const float barGap = 8.0f;
            const float textWidth = std::max(0.0f, contentWidth - barWidth - barGap - style.quotePadding * 2.0f);
            return estimateInlineRunsHeight(block.runs, textWidth, fontSize, lineHeight, heading, style) + style.quotePadding * 2.0f;
        }
        const float textHeight = estimateInlineRunsHeight(block.runs, contentWidth, fontSize, lineHeight, heading, style);
        return textHeight;
    }

    void renderBlock(const detail::MarkdownBlock& block, std::size_t index) {
        detail::markdownPerfStats().countBlockKind(block.kind);
        const std::string partId = id_ + ".block." + std::to_string(index);
        const float quoteInset = static_cast<float>(std::max(0, block.quoteDepth)) * 8.0f;
        const float listInset = static_cast<float>(std::max(0, block.listDepth - 1)) * style_.listIndent;
        const float contentWidth = std::max(0.0f, width_ - quoteInset - listInset);

        if (block.kind == detail::MarkdownBlockKind::Divider) {
            advanceFlow(9.0f);  // 1px 线 + 上下 margin 4+4（与 estimateBlockHeight 同口径）
            ui_.rect(partId)
                .width(contentWidth)
                .height(1.0f)
                .margin(quoteInset + listInset, 4.0f, 0.0f, 4.0f)
                .color(style_.divider)
                .build();
            return;
        }

        if (block.kind == detail::MarkdownBlockKind::Heading) {
            const float fontSize = detail::markdownHeadingSize(style_, block.headingLevel);
            renderInlineBlock(partId, block, quoteInset, listInset, contentWidth, fontSize, fontSize + 6.0f, true);
            return;
        }

        if (block.kind == detail::MarkdownBlockKind::Code) {
            renderCodeBlock(partId, block, quoteInset, listInset, contentWidth);
            return;
        }

        if (block.kind == detail::MarkdownBlockKind::Html) {
            renderMutedTextBlock(partId, block, quoteInset, listInset, contentWidth);
            return;
        }

        if (block.kind == detail::MarkdownBlockKind::ListItem) {
            renderListItem(partId, block, quoteInset, listInset, contentWidth);
            return;
        }

        renderInlineBlock(partId, block, quoteInset, listInset, contentWidth, style_.bodySize, style_.bodyLineHeight, false);
    }

    void renderListItem(const std::string& partId,
                        const detail::MarkdownBlock& block,
                        float quoteInset,
                        float listInset,
                        float contentWidth) {
        const float markerWidth = block.task ? 34.0f : 24.0f;
        const float textWidth = std::max(0.0f, contentWidth - markerWidth - 8.0f);
        const float height = std::max(style_.bodyLineHeight,
                                      inlineRunsHeight(block.runs,
                                                       textWidth,
                                                       style_.bodySize,
                                                       style_.bodyLineHeight,
                                                       false));
        const bool contentVisible = contentVisibleAt(flowCursorY_, height);
        advanceFlow(height);
        ui_.stack(partId)
            .width(contentWidth)
            .height(height)
            .margin(quoteInset + listInset, 0.0f, 0.0f, 0.0f)
            .content([&] {
                if (!contentVisible) return;  // bug382：窗口外块只留包装几何
                ui_.text(partId + ".mark")
                    .size(markerWidth, style_.bodyLineHeight)
                    .text(detail::markdownListPrefix(block))
                    .fontFamily(style_.fontFamily)
                    .fontSize(style_.bodySize)
                    .lineHeight(style_.bodyLineHeight)
                    .horizontalAlign(core::HorizontalAlign::Right)
                    .verticalAlign(core::VerticalAlign::Center)
                    .color(block.task ? style_.accent : style_.muted)
                    .build();
                renderInlineRuns(partId + ".text", block.runs, textWidth, height, style_.bodySize, style_.bodyLineHeight, false, markerWidth + 8.0f, 0.0f);
            })
            .build();
    }

    void renderInlineBlock(const std::string& partId,
                            const detail::MarkdownBlock& block,
                            float quoteInset,
                            float listInset,
                            float contentWidth,
                            float fontSize,
                            float lineHeight,
                            bool heading) {
        if (block.quoteDepth > 0) {
            const float barWidth = 3.0f;
            const float barGap = 8.0f;
            const float panelX = barWidth + barGap;
            const float panelWidth = std::max(0.0f, contentWidth - panelX);
            const float textX = panelX + style_.quotePadding;
            const float textWidth = std::max(0.0f, panelWidth - style_.quotePadding * 2.0f);
            const float textHeight = inlineRunsHeight(block.runs, textWidth, fontSize, lineHeight, heading);
            const float height = textHeight + style_.quotePadding * 2.0f;
            const bool contentVisible = contentVisibleAt(flowCursorY_, height);
            advanceFlow(height);
            ui_.stack(partId)
                .width(contentWidth)
                .height(height)
                .margin(quoteInset + listInset, 0.0f, 0.0f, 0.0f)
                .content([&] {
                    if (!contentVisible) return;  // bug382：窗口外块只留包装几何
                    ui_.rect(partId + ".bar")
                        .size(barWidth, height)
                        .color(style_.accent)
                        .radius(1.5f)
                        .build();
                    ui_.rect(partId + ".bg")
                        .x(panelX)
                        .width(panelWidth)
                        .height(height)
                        .color(style_.quoteBackground)
                        .radius(style_.radius)
                        .build();
                    renderInlineRuns(partId + ".text", block.runs, textWidth, textHeight, fontSize, lineHeight, heading, textX, style_.quotePadding);
                })
                .build();
            return;
        }

        const float blockHeight = inlineRunsHeight(block.runs, contentWidth, fontSize, lineHeight, heading);
        const bool contentVisible = contentVisibleAt(flowCursorY_, blockHeight);
        advanceFlow(blockHeight);
        ui_.stack(partId)
            .width(contentWidth)
            .height(blockHeight)
            .margin(quoteInset + listInset, 0.0f, 0.0f, 0.0f)
            .content([&] {
                if (!contentVisible) return;  // bug382：窗口外块只留包装几何
                renderInlineRuns(partId + ".text", block.runs, contentWidth, blockHeight, fontSize, lineHeight, heading);
            })
            .build();
    }

    core::Color inlineRunColor(const detail::MarkdownRun& run, bool heading) const {
        if (run.kind == detail::MarkdownRunKind::InlineCode) {
            return style_.codeText;
        }
        if (run.style.link || run.kind == detail::MarkdownRunKind::Link ||
            run.kind == detail::MarkdownRunKind::Image ||
            run.kind == detail::MarkdownRunKind::Math ||
            run.kind == detail::MarkdownRunKind::WikiLink) {
            return style_.accent;
        }
        if (run.kind == detail::MarkdownRunKind::Html || run.style.html) {
            return style_.muted;
        }
        return heading ? style_.heading : style_.text;
    }

    float inlineRunsHeight(const std::vector<detail::MarkdownRun>& runs,
                            float width,
                            float fontSize,
                            float lineHeight,
                            bool heading,
                            core::HorizontalAlign align = core::HorizontalAlign::Left) {
        return inlineLayoutFor(runs, width, fontSize, lineHeight, heading, align)->measuredHeight;
    }

    void renderInlineRuns(const std::string& partId,
                          const std::vector<detail::MarkdownRun>& runs,
                          float width,
                          float height,
                          float fontSize,
                          float lineHeight,
                          bool heading,
                           float x = 0.0f,
                           float y = 0.0f,
                           core::HorizontalAlign align = core::HorizontalAlign::Left) {
        const std::shared_ptr<const InlineLayoutEntry> layout =
            inlineLayoutFor(runs, width, fontSize, lineHeight, heading, align);
        ui_.stack(partId)
            .position(x, y)
            .size(width, height)
            .content([&] {
                // bug399 round6 / debt398：段合并降元素数——CJK 单码点成段是
                // 布局语义（splitMarkdownTextForLayout），但元素构建无需逐码点：
                // 同 run 样式 + 同行 + 水平紧邻（x == prev.x+prev.width）的段
                // 合并成一个 text 元素，元素数降 1-2 个量级；字形位置/宽度不变
                // （advance 累加），选区与命中走 layout 表不受影响。
                const auto sameVisualRun = [](const InlineSegment& a,
                                              const InlineSegment& b) {
                    return a.run.kind == b.run.kind &&
                           a.run.style.emphasis == b.run.style.emphasis &&
                           a.run.style.strong == b.run.style.strong &&
                           a.run.style.deleted == b.run.style.deleted &&
                           a.run.style.underline == b.run.style.underline &&
                           a.run.style.link == b.run.style.link &&
                           a.run.style.image == b.run.style.image &&
                           a.run.style.math == b.run.style.math &&
                           a.run.style.wiki == b.run.style.wiki &&
                           a.run.style.html == b.run.style.html;
                };
                const auto& segs = layout->segments;


                std::size_t outIndex = 0;
                std::size_t segIndex = 0;
                while (segIndex < segs.size()) {
                    const InlineSegment& first = segs[segIndex];
                    if (first.chip) {
                        renderInlineSegment(partId + ".seg." + std::to_string(outIndex++),
                                            first, fontSize, lineHeight, heading);
                        ++segIndex;
                        continue;
                    }
                    std::string mergedText = first.text;
                    float mergedWidth = first.width;
                    std::size_t nextIndex = segIndex + 1;
                    while (nextIndex < segs.size()) {
                        const InlineSegment& next = segs[nextIndex];
                        if (next.chip || std::fabs(next.y - first.y) > 0.5f ||
                            !sameVisualRun(first, next) ||
                            std::fabs(next.x - (first.x + mergedWidth) - kInlineSegmentGap) > 0.6f) {
                            break;
                        }
                        mergedText += next.text;
                        mergedWidth += next.width;
                        ++nextIndex;
                    }
                    if (nextIndex == segIndex + 1) {
                        renderInlineSegment(partId + ".seg." + std::to_string(outIndex++),
                                            first, fontSize, lineHeight, heading);
                    } else {
                        InlineSegment combined = first;
                        combined.text = std::move(mergedText);
                        combined.width = mergedWidth;
                        renderInlineSegment(partId + ".seg." + std::to_string(outIndex++),
                                            combined, fontSize, lineHeight, heading);
                    }
                    segIndex = nextIndex;



                }
            })
            .build();
    }

    void renderInlineSegment(const std::string& partId,
                             const InlineSegment& segment,
                             float fontSize,
                             float lineHeight,
                             bool heading) {
        const bool code = segment.run.kind == detail::MarkdownRunKind::InlineCode;
        const float runFontSize = code ? std::max(1.0f, fontSize - 1.0f) : fontSize;
        const int weight = heading || segment.run.style.strong ? 700 : 400;
        const float textInset = segment.chip ? 6.0f : 0.0f;
        const core::Color color = inlineRunColor(segment.run, heading);

        ui_.stack(partId)
            .position(segment.x, segment.y)
            .size(segment.width, lineHeight)
            .content([&] {
                if (segment.chip) {
                    const float bgHeight = std::max(16.0f, lineHeight - 4.0f);
                    ui_.rect(partId + ".bg")
                        .y((lineHeight - bgHeight) * 0.5f)
                        .size(segment.width, bgHeight)
                        .color(code ? style_.codeBackground : style_.quoteBackground)
                        .radius(style_.radius)
                        .build();
                }
                ui_.text(partId + ".text")
                    .x(textInset)
                    .width(std::max(0.0f, segment.width - textInset * 2.0f))
                    .height(lineHeight)
                    .text(segment.text)
                    .fontFamily(code ? style_.codeFontFamily : style_.fontFamily)
                    .fontSize(runFontSize)
                    .fontWeight(weight)
                    .lineHeight(lineHeight)
                    .verticalAlign(core::VerticalAlign::Center)
                    .color(color)
                    .build();
                if (segment.run.style.deleted || segment.run.style.underline || segment.run.style.link) {
                    const float lineY = segment.run.style.deleted ? lineHeight * 0.52f : lineHeight - 4.0f;
                    ui_.rect(partId + ".decor")
                        .x(textInset)
                        .y(lineY)
                        .size(std::max(0.0f, segment.width - textInset * 2.0f), 1.0f)
                        .color(segment.run.style.link ? style_.accent : color)
                        .build();
                }
            })
            .build();
    }

    static void alignInlineSegments(std::vector<InlineSegment>& segments,
                                    float width,
                                    core::HorizontalAlign align) {
        if (align == core::HorizontalAlign::Left || width <= 0.0f) {
            return;
        }
        std::size_t lineBegin = 0;
        while (lineBegin < segments.size()) {
            const float lineY = segments[lineBegin].y;
            std::size_t lineEnd = lineBegin;
            float lineRight = 0.0f;
            while (lineEnd < segments.size() && std::fabs(segments[lineEnd].y - lineY) <= 0.001f) {
                lineRight = std::max(lineRight, segments[lineEnd].x + segments[lineEnd].width);
                ++lineEnd;
            }
            const float extra = std::max(0.0f, width - lineRight);
            const float offset = align == core::HorizontalAlign::Center ? extra * 0.5f : extra;
            for (std::size_t index = lineBegin; index < lineEnd; ++index) {
                segments[index].x += offset;
            }
            lineBegin = lineEnd;
        }
    }

    void renderCodeBlock(const std::string& partId,
                         const detail::MarkdownBlock& block,
                         float quoteInset,
                         float listInset,
                         float contentWidth) {
        const std::string text = detail::codeBlockDisplayText(block);
        const float textWidth = std::max(0.0f, contentWidth - style_.codePadding * 2.0f);
        const float textHeight = detail::estimateMarkdownTextHeight(text, textWidth, style_.codeSize, style_.codeSize + 6.0f);
        const float blockHeight = textHeight + style_.codePadding * 2.0f;
        const bool contentVisible = contentVisibleAt(flowCursorY_, blockHeight);
        advanceFlow(blockHeight);
        ui_.stack(partId)
            .width(contentWidth)
            .height(blockHeight)
            .margin(quoteInset + listInset, 0.0f, 0.0f, 0.0f)
            .content([&] {
                if (!contentVisible) return;  // bug382：窗口外块只留包装几何
                ui_.rect(partId + ".bg")
                    .size(contentWidth, blockHeight)
                    .color(style_.codeBackground)
                    .radius(style_.radius)
                    .build();
                ui_.text(partId + ".text")
                    .x(style_.codePadding)
                    .y(style_.codePadding)
                    .width(textWidth)
                    .height(textHeight)
                    .text(text)
                    .fontFamily(style_.codeFontFamily)
                    .fontSize(style_.codeSize)
                    .lineHeight(style_.codeSize + 6.0f)
                    .wrap()
                    .color(style_.codeText)
                    .build();
            })
            .build();
    }

    void renderMutedTextBlock(const std::string& partId,
                              const detail::MarkdownBlock& block,
                              float quoteInset,
                              float listInset,
                              float contentWidth) {
        const std::string text = detail::plainText(block.runs);
        const float textWidth = std::max(0.0f, contentWidth - style_.codePadding * 2.0f);
        const float textHeight = detail::estimateMarkdownTextHeight(text, textWidth, style_.codeSize, style_.codeSize + 6.0f);
        const float blockHeight = textHeight + style_.codePadding * 2.0f;
        const bool contentVisible = contentVisibleAt(flowCursorY_, blockHeight);
        advanceFlow(blockHeight);
        ui_.stack(partId)
            .width(contentWidth)
            .height(blockHeight)
            .margin(quoteInset + listInset, 0.0f, 0.0f, 0.0f)
            .content([&] {
                if (!contentVisible) return;  // bug382：窗口外块只留包装几何
                ui_.rect(partId + ".bg")
                    .size(contentWidth, blockHeight)
                    .color(style_.quoteBackground)
                    .radius(style_.radius)
                    .build();
                ui_.text(partId + ".text")
                    .x(style_.codePadding)
                    .y(style_.codePadding)
                    .width(textWidth)
                    .height(textHeight)
                    .text(text)
                    .fontFamily(style_.codeFontFamily)
                    .fontSize(style_.codeSize)
                    .lineHeight(style_.codeSize + 6.0f)
                    .wrap()
                    .color(style_.muted)
                    .build();
            })
            .build();
    }

    void renderTable(const std::vector<detail::MarkdownBlock>& blocks, std::size_t begin, std::size_t end) {
        if (begin >= end) {
            return;
        }

        const detail::MarkdownBlock& first = blocks[begin];
        const std::string tableId = id_ + ".table." + std::to_string(begin);
        const float quoteInset = static_cast<float>(std::max(0, first.quoteDepth)) * 8.0f;
        const float listInset = static_cast<float>(std::max(0, first.listDepth - 1)) * style_.listIndent;
        const float contentWidth = std::max(0.0f, width_ - quoteInset - listInset);
        float tableHeight = 0.0f;
        std::vector<float> rowHeights;
        rowHeights.reserve(end - begin);
        for (std::size_t row = begin; row < end; ++row) {
            const float rh = tableRowHeight(tableId + ".row." + std::to_string(row - begin), blocks[row], contentWidth);
            rowHeights.push_back(rh);
            tableHeight += rh;
        }
        advanceFlow(tableHeight);
        // bug382：整表在窗口外时行级 content 全跳（表 column 高=Σ行高，显式不变）。
        const float tableTop = flowCursorY_ - tableHeight;

        ui_.column(tableId)
            .width(contentWidth)
            .height(tableHeight)
            .margin(quoteInset + listInset, 0.0f, 0.0f, 0.0f)
            .gap(0.0f)
            .content([&] {
                float rowY = tableTop;
                for (std::size_t row = begin; row < end; ++row) {
                    const std::size_t slot = row - begin;
                    renderTableRow(tableId + ".row." + std::to_string(slot), blocks[row], contentWidth,
                                   contentVisibleAt(rowY, rowHeights[slot]));
                    rowY += rowHeights[slot];
                }
            })
            .build();
    }

    float tableRowHeight(const std::string& partId,
                         const detail::MarkdownBlock& block,
                         float contentWidth) {
        const std::size_t cellCount = std::max<std::size_t>(1, block.cells.size());
        const float cellWidth = contentWidth / static_cast<float>(cellCount);
        const float textWidth = std::max(0.0f, cellWidth - style_.tableCellPadding * 2.0f);
        const std::vector<detail::MarkdownRun> emptyRuns;
        float height = 0.0f;
        for (std::size_t cell = 0; cell < cellCount; ++cell) {
            const bool hasCell = cell < block.cells.size();
            const std::vector<detail::MarkdownRun>& runs = hasCell ? block.cells[cell].runs : emptyRuns;
            const core::HorizontalAlign align = hasCell
                ? detail::horizontalAlign(block.cells[cell].align)
                : core::HorizontalAlign::Left;
            height = std::max(height, inlineRunsHeight(
                runs,
                textWidth,
                style_.bodySize,
                style_.bodyLineHeight,
                block.tableHeader,
                align));
        }
        return height + style_.tableCellPadding * 2.0f;
    }

    void renderTableRow(const std::string& partId,
                        const detail::MarkdownBlock& block,
                        float contentWidth,
                        bool contentVisible) {
        const std::size_t cellCount = std::max<std::size_t>(1, block.cells.size());
        const float cellWidth = contentWidth / static_cast<float>(cellCount);
        const float rowHeight = tableRowHeight(partId, block, contentWidth);
        const core::Color fill = block.tableHeader
            ? theme::withOpacity(style_.accent, 0.16f)
            : style_.quoteBackground;

        ui_.stack(partId)
            .width(contentWidth)
            .height(rowHeight)
            .content([&] {
                if (!contentVisible) return;  // bug382：窗口外行只留包装几何
                for (std::size_t cell = 0; cell < cellCount; ++cell) {
                    const std::string cellId = partId + ".cell." + std::to_string(cell);
                    ui_.stack(cellId)
                        .x(static_cast<float>(cell) * cellWidth)
                        .size(cellWidth, rowHeight)
                        .content([&] {
                            ui_.rect(cellId + ".bg")
                                .size(cellWidth, rowHeight)
                                .color(fill)
                                .border(1.0f, style_.divider)
                                .build();
                            if (cell < block.cells.size()) {
                                renderTableCell(cellId + ".text", block.cells[cell], block.tableHeader, cellWidth, rowHeight);
                            }
                        })
                        .build();
                }
            })
            .build();
    }

    void renderTableCell(const std::string& partId,
                         const detail::MarkdownTableCell& cell,
                         bool header,
                         float cellWidth,
                         float rowHeight) {
        const float textWidth = std::max(0.0f, cellWidth - style_.tableCellPadding * 2.0f);
        const core::HorizontalAlign align = detail::horizontalAlign(cell.align);
        const float textHeight = inlineRunsHeight(cell.runs, textWidth, style_.bodySize, style_.bodyLineHeight, header, align);
        float textX = style_.tableCellPadding;
        if (cell.align == detail::MarkdownAlign::Center) {
            textX = style_.tableCellPadding;
        } else if (cell.align == detail::MarkdownAlign::Right) {
            textX = style_.tableCellPadding;
        }
        const float textY = std::max(0.0f, (rowHeight - textHeight) * 0.5f);

        renderInlineRuns(partId,
                         cell.runs,
                         textWidth,
                         textHeight,
                         style_.bodySize,
                         style_.bodyLineHeight,
                          header,
                          textX,
                          textY,
                         align);
    }

    core::dsl::Ui& ui_;
    std::string id_;
    std::string markdown_;
    MarkdownStyle style_;
    float width_ = 420.0f;
    core::SizeValue height_ = core::SizeValue::wrapContent();
    core::EdgeInsets margin_;
    float x_ = 0.0f;
    float y_ = 0.0f;
    bool hasX_ = false;
    bool hasY_ = false;
    int zIndex_ = 0;
    // bug382 视口裁剪态：cullBottom_ <= cullTop_ = 关闭（默认）。
    float cullTop_ = 0.0f;
    float cullBottom_ = -1.0f;
    // bug399 round7：同块 estimate（inlineRunsHeight）与 render（renderInlineRuns）
    // 紧邻调用同一布局键——快照复用，免每帧第二次键构造 + 查找（53KB 实测 ~3.5ms/帧）。
    struct InlineLayoutSnapshot {
        const std::vector<detail::MarkdownRun>* runs = nullptr;
        float width = 0.0f;
        float fontSize = 0.0f;
        float lineHeight = 0.0f;
        bool heading = false;
        core::HorizontalAlign align = core::HorizontalAlign::Left;
        std::shared_ptr<const InlineLayoutEntry> layout;
    };
    InlineLayoutSnapshot lastInlineLayout_;

    std::shared_ptr<const InlineLayoutEntry> inlineLayoutFor(
        const std::vector<detail::MarkdownRun>& runs,
        float width,
        float fontSize,
        float lineHeight,
        bool heading,
        core::HorizontalAlign align) {
        InlineLayoutSnapshot& snap = lastInlineLayout_;
        if (snap.runs == &runs && snap.width == width && snap.fontSize == fontSize &&
            snap.lineHeight == lineHeight && snap.heading == heading && snap.align == align &&
            snap.layout) {
            return snap.layout;
        }
        snap.runs = &runs;
        snap.width = width;
        snap.fontSize = fontSize;
        snap.lineHeight = lineHeight;
        snap.heading = heading;
        snap.align = align;
        snap.layout = cachedInlineLayout(runs, width, fontSize, lineHeight, heading, align, style_);
        return snap.layout;
    }

    float flowCursorY_ = 0.0f;
};

inline MarkdownBuilder markdown(core::dsl::Ui& ui, const std::string& id) {
    return MarkdownBuilder(ui, id);
}

} // namespace components
