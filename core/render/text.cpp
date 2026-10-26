#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// text —— 生命周期/状态位/图集栅格化/渲染命令/公共 API 转发。
// TU 分工见 core/render/text_internal.h 头注：
//   text_shaping.cpp  —— 字体发现/解析/加载/fallback 栈 + 逐码点 shaping
//   text_measure.cpp  —— 测量（TextMetrics/caret 停靠点/TextSizeCache/measure* 静态成员）
//   text_layout.cpp   —— 布局/断行/省略号装配/顶点装配

#include "core/render/text_internal.h"
#include "core/render/render_backend.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core {

namespace {

constexpr int kGrayAtlasSize = 2048;
constexpr int kColorAtlasSize = 1024;

struct AtlasPage {
    int width = 0;
    int height = 0;
    int channels = 1;
    int x = 1;
    int y = 1;
    int rowHeight = 0;
    std::uint64_t generation = 0;
    std::vector<unsigned char> pixels;
    std::unordered_map<std::string, TextPrimitive::Glyph> glyphs;
};

struct SharedTextAtlas {
    AtlasPage gray;
    AtlasPage color;
    int references = 0;
};

SharedTextAtlas& sharedTextAtlas() {
    static SharedTextAtlas atlas;
    return atlas;
}

std::uint64_t& textAtlasGenerationCounter() {
    static std::uint64_t generation = 0;
    return generation;
}

bool ensureAtlasPage(AtlasPage& page, int width, int height, int channels) {
    if (!page.pixels.empty()) {
        return true;
    }

    page.width = width;
    page.height = height;
    page.channels = channels;
    page.x = 1;
    page.y = 1;
    page.rowHeight = 0;
    page.generation = ++textAtlasGenerationCounter();
    page.pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(channels), 0);
    return !page.pixels.empty();
}

bool retainSharedTextAtlas() {
    SharedTextAtlas& atlas = sharedTextAtlas();
    ++atlas.references;
    if (ensureAtlasPage(atlas.gray, kGrayAtlasSize, kGrayAtlasSize, 1)) {
        return true;
    }
    atlas.references = std::max(0, atlas.references - 1);
    return false;
}

void releaseAtlasPage(AtlasPage& page) {
    page = {};
}

void releaseSharedTextAtlas() {
    SharedTextAtlas& atlas = sharedTextAtlas();
    atlas.references = std::max(0, atlas.references - 1);
    if (atlas.references > 0) {
        return;
    }

    releaseAtlasPage(atlas.gray);
    releaseAtlasPage(atlas.color);
}

size_t faceIndexFromGlyphKey(std::uint64_t key) {
    return static_cast<size_t>((key >> 32) - 1ULL);
}

unsigned int glyphIndexFromGlyphKey(std::uint64_t key) {
    return static_cast<unsigned int>(key & 0xffffffffULL);
}

std::string glyphCacheKey(const text_detail::FontFace& face, float fontSize, unsigned int glyphIndex, bool colored) {
    return face.path + "#" +
           std::to_string(static_cast<int>(std::round(fontSize * 64.0f))) + "#" +
           (colored ? "color#" : "gray#") +
           std::to_string(glyphIndex);
}

bool appendToAtlas(AtlasPage& page,
                   const unsigned char* pixels,
                   int width,
                   int height,
                   int channels,
                   TextPrimitive::Glyph& glyph) {
    if (width <= 0 || height <= 0 || pixels == nullptr || channels <= 0 || page.pixels.empty()) {
        return true;
    }

    if (page.x + width + 1 >= page.width) {
        page.x = 1;
        page.y += page.rowHeight + 1;
        page.rowHeight = 0;
    }
    if (page.y + height + 1 >= page.height) {
        return false;
    }

    const int copyChannels = std::min(channels, page.channels);
    for (int row = 0; row < height; ++row) {
        const auto dstOffset = (static_cast<std::size_t>(page.y + row) * static_cast<std::size_t>(page.width) +
                                static_cast<std::size_t>(page.x)) *
                               static_cast<std::size_t>(page.channels);
        const auto srcOffset = static_cast<std::size_t>(row) * static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
        unsigned char* dst = page.pixels.data() + dstOffset;
        const unsigned char* src = pixels + srcOffset;
        for (int x = 0; x < width; ++x) {
            std::copy(src + static_cast<std::size_t>(x) * static_cast<std::size_t>(channels),
                      src + static_cast<std::size_t>(x) * static_cast<std::size_t>(channels) + copyChannels,
                      dst + static_cast<std::size_t>(x) * static_cast<std::size_t>(page.channels));
        }
    }

    glyph.u0 = static_cast<float>(page.x) / static_cast<float>(page.width);
    glyph.v0 = static_cast<float>(page.y) / static_cast<float>(page.height);
    glyph.u1 = static_cast<float>(page.x + width) / static_cast<float>(page.width);
    glyph.v1 = static_cast<float>(page.y + height) / static_cast<float>(page.height);

    page.x += width + 1;
    page.rowHeight = std::max(page.rowHeight, height);
    page.generation = ++textAtlasGenerationCounter();
    return true;
}

std::vector<unsigned char> copyGrayBitmap(const FT_Bitmap& bitmap) {
    if (bitmap.width == 0 || bitmap.rows == 0 || !bitmap.buffer ||
        std::abs(bitmap.pitch) < static_cast<int>(bitmap.width)) {
        return {};
    }
    std::vector<unsigned char> compact(static_cast<size_t>(bitmap.width) * bitmap.rows);
    const int pitch = std::abs(bitmap.pitch);
    const unsigned char* base = bitmap.pitch >= 0
        ? bitmap.buffer
        : bitmap.buffer + static_cast<ptrdiff_t>(bitmap.rows - 1) * pitch;
    for (unsigned int row = 0; row < bitmap.rows; ++row) {
        const unsigned char* source = bitmap.pitch >= 0
            ? base + static_cast<ptrdiff_t>(row) * pitch
            : base - static_cast<ptrdiff_t>(row) * pitch;
        std::copy(source, source + bitmap.width, compact.begin() + static_cast<ptrdiff_t>(row) * bitmap.width);
    }
    return compact;
}

std::vector<unsigned char> copyBgraBitmapAsRgba(const FT_Bitmap& bitmap) {
    if (bitmap.width == 0 || bitmap.rows == 0 || !bitmap.buffer ||
        std::abs(bitmap.pitch) < static_cast<int>(bitmap.width * 4)) {
        return {};
    }
    std::vector<unsigned char> compact(static_cast<size_t>(bitmap.width) * bitmap.rows * 4);
    const int pitch = std::abs(bitmap.pitch);
    const unsigned char* base = bitmap.pitch >= 0
        ? bitmap.buffer
        : bitmap.buffer + static_cast<ptrdiff_t>(bitmap.rows - 1) * pitch;
    for (unsigned int row = 0; row < bitmap.rows; ++row) {
        const unsigned char* source = bitmap.pitch >= 0
            ? base + static_cast<ptrdiff_t>(row) * pitch
            : base - static_cast<ptrdiff_t>(row) * pitch;
        unsigned char* target = compact.data() + static_cast<ptrdiff_t>(row) * bitmap.width * 4;
        for (unsigned int x = 0; x < bitmap.width; ++x) {
            target[x * 4 + 0] = source[x * 4 + 2];
            target[x * 4 + 1] = source[x * 4 + 1];
            target[x * 4 + 2] = source[x * 4 + 0];
            target[x * 4 + 3] = source[x * 4 + 3];
        }
    }
    return compact;
}

} // namespace

std::vector<TextRun> normalizeTextRuns(const std::string& text, std::vector<TextRun> runs) {
    if (runs.empty()) {
        return {};
    }
    const int length = static_cast<int>(text.size());
    // ponytail: 逐字节 owner 数组，O(text + runs)；编辑器场景 runs 是行级的
    //（~100 字节），若将来要归一化整篇文档再换区间扫描。
    std::vector<int> owner(static_cast<std::size_t>(length), -1);
    bool any = false;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        int start = runs[i].byteStart;
        int end = runs[i].byteEnd;
        if (start < 0) {
            start = 0;
        }
        if (end > length) {
            end = length;
        }
        if (start > end) {
            continue;   // 反转 run 视为无效，丢弃
        }
        // 向下取整到 UTF-8 codepoint 起点：glyph 着色按 codepoint 粒度，
        // 不允许 run 边界劈开一个 codepoint。
        while (start > 0 && (static_cast<unsigned char>(text[static_cast<std::size_t>(start)]) & 0xC0) == 0x80) {
            --start;
        }
        while (end > 0 && end < length &&
               (static_cast<unsigned char>(text[static_cast<std::size_t>(end)]) & 0xC0) == 0x80) {
            --end;
        }
        if (start >= end) {
            continue;
        }
        any = true;
        for (int byte = start; byte < end; ++byte) {
            owner[static_cast<std::size_t>(byte)] = static_cast<int>(i);
        }
    }
    if (!any) {
        return {};
    }

    std::vector<TextRun> result;
    const auto sameColor = [](const Color& left, const Color& right) {
        return left.r == right.r && left.g == right.g &&
               left.b == right.b && left.a == right.a;
    };
    for (int byte = 0; byte < length; ++byte) {
        const int runIndex = owner[static_cast<std::size_t>(byte)];
        if (runIndex < 0) {
            continue;
        }
        const Color& color = runs[static_cast<std::size_t>(runIndex)].color;
        if (!result.empty() && result.back().byteEnd == byte && sameColor(result.back().color, color)) {
            result.back().byteEnd = byte + 1;   // 相邻同色合并
            continue;
        }
        result.push_back(TextRun{byte, byte + 1, color});
    }
    return result;
}

bool TextPrimitive::Impl::initialize() {
    if (!loadFont() || !retainSharedTextAtlas()) {
        return false;
    }

    return true;
}

void TextPrimitive::Impl::destroy() {
    releaseSharedTextAtlas();
    fontInfoStorage_.reset();
    glyphs_.clear();
    glyphs_.rehash(0);
    lines_.clear();
    vertices_.clear();
    measuredSize_ = {};
    layoutDirty_ = true;
    verticesDirty_ = true;
    fontDirty_ = true;
}

void TextPrimitive::Impl::setPosition(float x, float y) {
    if (position_.x == x && position_.y == y) {
        return;
    }
    position_ = {x, y};
    invalidateVertices();
}

void TextPrimitive::Impl::setText(const std::string& text) {
    if (style_.text == text) {
        return;
    }
    if (style_.text.capacity() / 4u > text.size()) {
        std::string compactText = text;
        style_.text.swap(compactText);
    } else {
        style_.text = text;
    }
    // 虚拟列表会复用 TextPrimitive。glyph cache 仅服务于当前内容；共享 atlas
    // 仍保留栅格化结果，因此清理实例缓存不会重复分配 GPU atlas。
    glyphs_.clear();
    glyphs_.rehash(0);
    if (lines_.capacity() > 256u) {
        std::vector<Line>().swap(lines_);
    }
    // 长文本产生的顶点容量不能跟随 slot 永久保留；普通文本更新仍复用小缓存。
    constexpr std::size_t kMinimumRetainedVertexFloats = 32u * 1024u;
    const std::size_t desiredCapacity = std::max(
        kMinimumRetainedVertexFloats,
        text.size() <= std::numeric_limits<std::size_t>::max() / 48u
            ? text.size() * 48u
            : std::numeric_limits<std::size_t>::max());
    if (vertices_.capacity() > desiredCapacity) {
        std::vector<float>().swap(vertices_);
    }
    invalidateLayout();
}

void TextPrimitive::Impl::setFontFamily(const std::string& fontFamily) {
    if (style_.fontFamily == fontFamily) {
        return;
    }
    style_.fontFamily = fontFamily;
    fontDirty_ = true;
    invalidateLayout();
}

void TextPrimitive::Impl::setFontSize(float fontSize) {
    if (style_.fontSize == fontSize) {
        return;
    }
    style_.fontSize = fontSize;
    fontDirty_ = true;
    invalidateLayout();
}

void TextPrimitive::Impl::setFontWeight(int fontWeight) {
    if (style_.fontWeight == fontWeight) {
        return;
    }
    style_.fontWeight = fontWeight;
    fontDirty_ = true;
    invalidateLayout();
}

void TextPrimitive::Impl::setColor(const Color& color) {
    // v0.8 起颜色烘焙进顶点，变化必须重建顶点。
    if (style_.color.r == color.r && style_.color.g == color.g &&
        style_.color.b == color.b && style_.color.a == color.a) {
        return;
    }
    style_.color = color;
    invalidateVertices();
}

void TextPrimitive::Impl::setMaxWidth(float maxWidth) {
    if (style_.maxWidth == maxWidth) {
        return;
    }
    style_.maxWidth = maxWidth;
    invalidateLayout();
}

void TextPrimitive::Impl::setWrap(bool wrap) {
    if (style_.wrap == wrap) {
        return;
    }
    style_.wrap = wrap;
    invalidateLayout();
}

void TextPrimitive::Impl::setOverflow(TextOverflow overflow) {
    if (style_.overflow == overflow) {
        return;
    }
    style_.overflow = overflow;
    invalidateLayout();
}

void TextPrimitive::Impl::setHorizontalAlign(HorizontalAlign align) {
    if (style_.horizontalAlign == align) {
        return;
    }
    style_.horizontalAlign = align;
    invalidateVertices();
}

void TextPrimitive::Impl::setVerticalAlign(VerticalAlign align) {
    if (style_.verticalAlign == align) {
        return;
    }
    style_.verticalAlign = align;
    invalidateVertices();
}

void TextPrimitive::Impl::setLineHeight(float lineHeight) {
    if (style_.lineHeight == lineHeight) {
        return;
    }
    style_.lineHeight = lineHeight;
    invalidateLayout();
}

void TextPrimitive::Impl::setVisualScale(float originX, float originY, float scale) {
    const float nextScale = std::max(0.01f, scale);
    if (visualScaleOrigin_.x == originX && visualScaleOrigin_.y == originY && visualScale_ == nextScale) {
        return;
    }
    visualScaleOrigin_ = {originX, originY};
    visualScale_ = nextScale;
    invalidateVertices();
}

void TextPrimitive::Impl::setTransform(const Transform& transform, const Rect& frame) {
    auto close = [](float left, float right) {
        return std::fabs(left - right) <= 0.0001f;
    };
    auto closeVec = [&](const Vec2& left, const Vec2& right) {
        return close(left.x, right.x) && close(left.y, right.y);
    };
    const bool sameTransform =
        closeVec(transform_.translate, transform.translate) &&
        close(transform_.translateZ, transform.translateZ) &&
        closeVec(transform_.scale, transform.scale) &&
        close(transform_.rotate, transform.rotate) &&
        close(transform_.rotateX, transform.rotateX) &&
        close(transform_.rotateY, transform.rotateY) &&
        closeVec(transform_.origin, transform.origin) &&
        close(transform_.perspective, transform.perspective);
    const bool sameFrame =
        close(transformFrame_.x, frame.x) &&
        close(transformFrame_.y, frame.y) &&
        close(transformFrame_.width, frame.width) &&
        close(transformFrame_.height, frame.height);
    if (sameTransform && sameFrame) {
        return;
    }
    transform_ = transform;
    transformFrame_ = frame;
    hasTransformMatrix_ = false;
    invalidateVertices();
}

void TextPrimitive::Impl::setTransformMatrix(const TransformMatrix& matrix) {
    auto close = [](float left, float right) {
        return std::fabs(left - right) <= 0.0001f;
    };
    const bool same =
        close(transformMatrix_.m00, matrix.m00) &&
        close(transformMatrix_.m01, matrix.m01) &&
        close(transformMatrix_.tx, matrix.tx) &&
        close(transformMatrix_.m10, matrix.m10) &&
        close(transformMatrix_.m11, matrix.m11) &&
        close(transformMatrix_.ty, matrix.ty) &&
        close(transformMatrix_.px, matrix.px) &&
        close(transformMatrix_.py, matrix.py) &&
        close(transformMatrix_.pw, matrix.pw) &&
        hasTransformMatrix_;
    if (same) {
        return;
    }
    transformMatrix_ = matrix;
    hasTransformMatrix_ = true;
    invalidateVertices();
}

void TextPrimitive::Impl::setStyle(const TextStyle& style) {
    const bool fontChanged = style.fontFamily != style_.fontFamily ||
                             style.fontSize != style_.fontSize ||
                             style.fontWeight != style_.fontWeight;
    style_ = style;
    fontDirty_ = fontDirty_ || fontChanged;
    invalidateLayout();
}

void TextPrimitive::Impl::setRuns(const std::vector<TextRun>& runs) {
    if (style_.runs == runs) {
        return;
    }
    style_.runs = runs;
    // 颜色 run 不影响 shaping/advance/换行——只重建顶点色。
    invalidateVertices();
}

const TextStyle& TextPrimitive::Impl::style() const {
    return style_;
}

Vec2 TextPrimitive::Impl::position() const {
    return position_;
}

Vec2 TextPrimitive::Impl::measuredSize() {
    if (layoutDirty_) {
        rebuildLayout();
    }
    return measuredSize_;
}

void TextPrimitive::Impl::setDefaultFontFiles(const std::string& textFontFile, const std::string& iconFontFile) {
    if (text_detail::defaultUiFontFileOverride() == textFontFile &&
        text_detail::defaultIconFontFileOverride() == iconFontFile) {
        return;
    }
    text_detail::defaultUiFontFileOverride() = textFontFile;
    text_detail::defaultIconFontFileOverride() = iconFontFile;
    text_detail::clearSharedFontStackCache();
}

void TextPrimitive::Impl::prepare() {
    if (layoutDirty_) {
        rebuildLayout();
    }
    if (verticesDirty_) {
        rebuildVertices();
    }
}

void TextPrimitive::Impl::render(int windowWidth, int windowHeight) {
    core::render::RenderBackend* backend = core::render::activeRenderBackend();
    if (backend == nullptr || windowWidth <= 0 || windowHeight <= 0) {
        return;
    }

    prepare();

    if (vertices_.empty()) {
        return;
    }

    const SharedTextAtlas& atlas = sharedTextAtlas();
    core::render::TextDrawCommand command{};
    command.vertices = vertices_.data();
    command.vertexFloatCount = vertices_.size();
    // v0.8：颜色烘焙进每顶点 9-float 格式；command.color 降级为批级 tint，恒白。
    // 元素透明度动画在 runtime 侧折进 style_.color（setColor）再进顶点。
    command.color = {1.0f, 1.0f, 1.0f, 1.0f};
    command.grayAtlas = {
        core::render::TextAtlasPageKind::Gray,
        atlas.gray.width,
        atlas.gray.height,
        atlas.gray.channels,
        atlas.gray.generation,
        atlas.gray.pixels.empty() ? nullptr : atlas.gray.pixels.data()
    };
    command.colorAtlas = {
        core::render::TextAtlasPageKind::Color,
        atlas.color.width,
        atlas.color.height,
        atlas.color.channels,
        atlas.color.generation,
        atlas.color.pixels.empty() ? nullptr : atlas.color.pixels.data()
    };
    backend->drawText(command, windowWidth, windowHeight);
}

bool TextPrimitive::Impl::loadFont() {
    const std::string fontPath = resolveFontPath(style_.fontFamily, style_.fontWeight);
    auto holder = text_detail::loadSharedFontStack(fontPath, style_.fontSize, style_.fontWeight);
    if (!holder || holder->faces.empty()) {
        return false;
    }

    fontInfoStorage_ = holder;
    scale_ = 1.0f;
    ascent_ = holder->faces.front().ascent;
    descent_ = holder->faces.front().descent;
    lineGap_ = holder->faces.front().lineGap;

    glyphs_.clear();

    fontDirty_ = false;
    return true;
}

bool TextPrimitive::Impl::ensureGlyph(const ShapedGlyph& shaped) {
    if (shaped.key == 0) {
        return true;
    }
    if (findGlyph(shaped.key)) {
        return true;
    }

    if (fontDirty_ && !loadFont()) {
        return false;
    }

    auto holder = std::static_pointer_cast<text_detail::FontInfoHolder>(fontInfoStorage_);
    if (!holder || holder->faces.empty()) {
        return false;
    }

    const size_t faceIndex = faceIndexFromGlyphKey(shaped.key);
    const unsigned int glyphIndex = glyphIndexFromGlyphKey(shaped.key);
    if (faceIndex >= holder->faces.size()) {
        return false;
    }

    text_detail::FontFace& face = holder->faces[faceIndex];
    Glyph glyph;
    glyph.advance = shaped.advance;
    glyph.colored = face.colored;

    if (glyphIndex == 0 || shaped.codepoint == ' ' || shaped.codepoint == '\t') {
        cacheGlyph(shaped.key, glyph);
        return true;
    }

    if (FT_Load_Glyph(face.face, glyphIndex, text_detail::kGlyphLoadFlags) != 0) {
        cacheGlyph(shaped.key, glyph);
        return true;
    }

    FT_GlyphSlot slot = face.face->glyph;
    glyph.advance = shaped.advance;

    if (slot->format != FT_GLYPH_FORMAT_BITMAP) {
        if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) {
            cacheGlyph(shaped.key, glyph);
            return true;
        }
    }

    const FT_Bitmap& bitmap = slot->bitmap;
    const bool colorBitmap = bitmap.pixel_mode == FT_PIXEL_MODE_BGRA;
    float glyphScale = face.glyphScale;
    if (colorBitmap && bitmap.rows > 0) {
        const float emHeight = ascent_ - descent_;
        glyphScale = std::min(glyphScale, emHeight / static_cast<float>(bitmap.rows));
    }
    glyph.colored = colorBitmap;
    glyph.xOffset = static_cast<float>(slot->bitmap_left) * glyphScale;
    glyph.yOffset = ascent_ - static_cast<float>(slot->bitmap_top) * glyphScale;
    glyph.width = static_cast<float>(bitmap.width) * glyphScale;
    glyph.height = static_cast<float>(bitmap.rows) * glyphScale;
    if (colorBitmap) {
        glyph.yOffset = ascent_ - descent_ - glyph.height;
    }

    if (bitmap.width == 0 || bitmap.rows == 0 || !bitmap.buffer) {
        cacheGlyph(shaped.key, glyph);
        return true;
    }

    const std::string cacheKey = glyphCacheKey(face, style_.fontSize, glyphIndex, colorBitmap);
    SharedTextAtlas& atlas = sharedTextAtlas();
    AtlasPage& page = colorBitmap ? atlas.color : atlas.gray;
    if (const auto cached = page.glyphs.find(cacheKey); cached != page.glyphs.end()) {
        glyph.u0 = cached->second.u0;
        glyph.v0 = cached->second.v0;
        glyph.u1 = cached->second.u1;
        glyph.v1 = cached->second.v1;
        cacheGlyph(shaped.key, glyph);
        return true;
    }

    if (colorBitmap) {
        if (!ensureAtlasPage(atlas.color, kColorAtlasSize, kColorAtlasSize, 4)) {
            cacheGlyph(shaped.key, glyph);
            return true;
        }
        std::vector<unsigned char> rgba = copyBgraBitmapAsRgba(bitmap);
        if (rgba.empty()) {
            cacheGlyph(shaped.key, glyph);
            return true;
        }
        if (!appendToAtlas(atlas.color, rgba.data(), static_cast<int>(bitmap.width), static_cast<int>(bitmap.rows), 4, glyph)) {
            cacheGlyph(shaped.key, glyph);
            return true;
        }
        atlas.color.glyphs[cacheKey] = glyph;
    } else if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
        std::vector<unsigned char> gray = copyGrayBitmap(bitmap);
        if (gray.empty()) {
            cacheGlyph(shaped.key, glyph);
            return true;
        }
        if (!appendToAtlas(atlas.gray, gray.data(), static_cast<int>(bitmap.width), static_cast<int>(bitmap.rows), 1, glyph)) {
            cacheGlyph(shaped.key, glyph);
            return true;
        }
        atlas.gray.glyphs[cacheKey] = glyph;
    } else {
        cacheGlyph(shaped.key, glyph);
        return true;
    }

    cacheGlyph(shaped.key, glyph);
    return true;
}

TextPrimitive::Glyph* TextPrimitive::Impl::findGlyph(std::uint64_t key) {
    const auto it = glyphs_.find(key);
    return it == glyphs_.end() ? nullptr : &it->second;
}

void TextPrimitive::Impl::cacheGlyph(std::uint64_t key, const Glyph& glyph) {
    if (key == 0) {
        return;
    }
    glyphs_[key] = glyph;
}

void TextPrimitive::Impl::invalidateLayout() {
    layoutDirty_ = true;
    invalidateVertices();
}

void TextPrimitive::Impl::invalidateVertices() {
    verticesDirty_ = true;
}

unsigned int TextPrimitive::Impl::readCodepoint(const std::string& text, size_t& index) {
    return text_detail::readUtf8Codepoint(text, index);
}

TextPrimitive::TextPrimitive()
    : impl_(std::make_unique<Impl>()) {}

TextPrimitive::TextPrimitive(float x, float y)
    : impl_(std::make_unique<Impl>(x, y)) {}

TextPrimitive::~TextPrimitive() = default;
TextPrimitive::TextPrimitive(TextPrimitive&&) noexcept = default;
TextPrimitive& TextPrimitive::operator=(TextPrimitive&&) noexcept = default;

bool TextPrimitive::initialize() { return impl_->initialize(); }
void TextPrimitive::destroy() { impl_->destroy(); }
void TextPrimitive::setPosition(float x, float y) { impl_->setPosition(x, y); }
void TextPrimitive::setText(const std::string& text) { impl_->setText(text); }
void TextPrimitive::setFontFamily(const std::string& fontFamily) { impl_->setFontFamily(fontFamily); }
void TextPrimitive::setFontSize(float fontSize) { impl_->setFontSize(fontSize); }
void TextPrimitive::setFontWeight(int fontWeight) { impl_->setFontWeight(fontWeight); }
void TextPrimitive::setColor(const Color& color) { impl_->setColor(color); }
void TextPrimitive::setMaxWidth(float maxWidth) { impl_->setMaxWidth(maxWidth); }
void TextPrimitive::setWrap(bool wrap) { impl_->setWrap(wrap); }

void TextPrimitive::setOverflow(TextOverflow overflow) { impl_->setOverflow(overflow); }
void TextPrimitive::setHorizontalAlign(HorizontalAlign align) { impl_->setHorizontalAlign(align); }
void TextPrimitive::setVerticalAlign(VerticalAlign align) { impl_->setVerticalAlign(align); }
void TextPrimitive::setLineHeight(float lineHeight) { impl_->setLineHeight(lineHeight); }
void TextPrimitive::setStyle(const TextStyle& style) { impl_->setStyle(style); }
void TextPrimitive::setRuns(const std::vector<TextRun>& runs) { impl_->setRuns(runs); }
void TextPrimitive::setVisualScale(float originX, float originY, float scale) { impl_->setVisualScale(originX, originY, scale); }
void TextPrimitive::setTransform(const Transform& transform, const Rect& frame) { impl_->setTransform(transform, frame); }
void TextPrimitive::setTransformMatrix(const TransformMatrix& matrix) { impl_->setTransformMatrix(matrix); }
const TextStyle& TextPrimitive::style() const { return impl_->style(); }
Vec2 TextPrimitive::position() const { return impl_->position(); }
Vec2 TextPrimitive::measuredSize() { return impl_->measuredSize(); }
void TextPrimitive::prepare() { impl_->prepare(); }
float TextPrimitive::measureTextWidth(const std::string& text,
                                      const std::string& fontFamily,
                                      float fontSize,
                                      int fontWeight) {
    return Impl::measureTextWidth(text, fontFamily, fontSize, fontWeight);
}
TextPrimitive::TextMetrics TextPrimitive::measureTextMetrics(const std::string& text,
                                                             const std::string& fontFamily,
                                                             float fontSize,
                                                             int fontWeight) {
    return Impl::measureTextMetrics(text, fontFamily, fontSize, fontWeight);
}

Vec2 TextPrimitive::measureTextSize(const TextStyle& style) {
    return Impl::measureTextSize(style);
}
void TextPrimitive::setDefaultFontFiles(const std::string& textFontFile, const std::string& iconFontFile) {
    Impl::setDefaultFontFiles(textFontFile, iconFontFile);
}
void TextPrimitive::render(int windowWidth, int windowHeight) { impl_->render(windowWidth, windowHeight); }

const std::vector<float>& TextPrimitive::debugVertices() const { return impl_->vertices_; }

std::string TextPrimitive::resolveFontPath(const std::string& fontFamily, int fontWeight) {
    return Impl::resolveFontPath(fontFamily, fontWeight);
}
std::string TextPrimitive::resolveSystemUiFontPathForWeight(int fontWeight) {
    return text_detail::resolveSystemUiFontPathForWeightImpl(fontWeight);
}

} // namespace core
