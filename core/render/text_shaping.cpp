#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

// text_shaping —— 字体发现/解析/加载/fallback 栈 + 逐码点 shaping。
// TU 分工见 core/render/text_internal.h 头注。
// 拆分前的匿名命名空间基础设施整体平移进 core::text_detail，逐字未改。

#include "core/render/text_internal.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <utility>

namespace core {

namespace text_detail {

namespace {

constexpr const char* kDefaultUiFontFile = "JingNanJunJunTi-JinNanJunJunTi-Bold-2.ttf";
constexpr const char* kDefaultIconFontFile = "Font Awesome 7 Free-Solid-900.otf";

FT_Library sharedFreeTypeLibrary() {
    static FT_Library library = [] {
        FT_Library created = nullptr;
        return FT_Init_FreeType(&created) == 0 ? created : nullptr;
    }();
    return library;
}

std::string existingPath(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        return path.string();
    }
    return {};
}

std::string firstExistingPath(std::initializer_list<const char*> paths) {
    for (const char* path : paths) {
        if (path == nullptr || path[0] == '\0') {
            continue;
        }
        if (const std::string existing = existingPath(path); !existing.empty()) {
            return existing;
        }
    }
    return {};
}

std::string resolveFontFilePath(const std::string& path);

std::string resolveSystemUiFontPath() {
    return resolveSystemUiFontPathForWeightImpl(400);
}

// Pick a platform-specific UI font variant that matches the requested weight.
// Falls back to the default UI font path when no weight-specific file exists
// (e.g. older systems with a single bundled font). This is what makes
// `ui.text(...).fontWeight(N)` actually switch glyph faces at runtime —
// without it, weight was accepted but the same default face was always used.
std::string resolveSystemUiFontPathForWeightPlatform(int fontWeight) {
#ifdef _WIN32
    // Windows ships explicit per-weight Segoe UI files since Win 7.
    // Order: heavy → light, fall back to regular.
    if (fontWeight >= 800) {
        if (const std::string path = firstExistingPath({"C:/Windows/Fonts/segoeuiz.ttf"}); !path.empty()) {
            return path;
        }
    }
    if (fontWeight >= 600) {
        if (const std::string path = firstExistingPath({"C:/Windows/Fonts/segoeuib.ttf"}); !path.empty()) {
            return path;
        }
    }
    if (fontWeight < 400) {
        if (const std::string path = firstExistingPath({"C:/Windows/Fonts/segoeuil.ttf"}); !path.empty()) {
            return path;
        }
    }
    return firstExistingPath({
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/arial.ttf"
    });
#elif defined(__APPLE__)
    // macOS ships SF Pro (SFNS*) as explicit weight files plus a variable font.
    if (fontWeight >= 800) {
        if (const std::string path = firstExistingPath({"/System/Library/Fonts/SFNS-Heavy.ttf",
                                                       "/System/Library/Fonts/SFNSText-Heavy.ttf",
                                                       "/System/Library/Fonts/SFNS-Black.ttf"}); !path.empty()) {
            return path;
        }
    }
    if (fontWeight >= 600) {
        if (const std::string path = firstExistingPath({"/System/Library/Fonts/SFNS-Bold.ttf",
                                                       "/System/Library/Fonts/SFNSText-Bold.ttf",
                                                       "/System/Library/Fonts/SFNS-Semibold.ttf"}); !path.empty()) {
            return path;
        }
    }
    if (fontWeight < 400) {
        if (const std::string path = firstExistingPath({"/System/Library/Fonts/SFNS-Light.ttf",
                                                       "/System/Library/Fonts/SFNSText-Light.ttf",
                                                       "/System/Library/Fonts/SFNS-Ultralight.ttf"}); !path.empty()) {
            return path;
        }
    }
    return firstExistingPath({
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf"
    });
#else
    // Linux: explicit Noto Sans weight files when bundled, otherwise the
    // variable font (Fedora 38+) or single Regular file (Debian/Ubuntu).
    if (fontWeight >= 800) {
        if (const std::string path = firstExistingPath({
                "/usr/share/fonts/truetype/noto/NotoSans-ExtraBold.ttf",
                "/usr/share/fonts/noto/NotoSans-ExtraBold.ttf",
                "/usr/share/fonts/google-noto/NotoSans-ExtraBold.ttf",
                "/usr/share/fonts/google-noto-sans/NotoSans-ExtraBold.ttf",
                "/usr/share/fonts/google-noto-sans-fonts/NotoSans-ExtraBold.ttf"}); !path.empty()) {
            return path;
        }
    }
    if (fontWeight >= 600) {
        if (const std::string path = firstExistingPath({
                "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
                "/usr/share/fonts/noto/NotoSans-Bold.ttf",
                "/usr/share/fonts/google-noto/NotoSans-Bold.ttf",
                "/usr/share/fonts/google-noto-sans/NotoSans-Bold.ttf",
                "/usr/share/fonts/google-noto-sans-fonts/NotoSans-Bold.ttf",
                "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Bold.ttf"}); !path.empty()) {
            return path;
        }
    }
    if (fontWeight < 400) {
        if (const std::string path = firstExistingPath({
                "/usr/share/fonts/truetype/noto/NotoSans-Light.ttf",
                "/usr/share/fonts/noto/NotoSans-Light.ttf",
                "/usr/share/fonts/google-noto/NotoSans-Light.ttf",
                "/usr/share/fonts/google-noto-sans/NotoSans-Light.ttf",
                "/usr/share/fonts/google-noto-sans-fonts/NotoSans-Light.ttf"}); !path.empty()) {
            return path;
        }
    }
    return firstExistingPath({
        // Debian / Ubuntu layout
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        // Fedora / RHEL family (static packages)
        "/usr/share/fonts/google-noto-sans/NotoSans-Regular.ttf",
        "/usr/share/fonts/google-noto-sans-fonts/NotoSans-Regular.ttf",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-sans-cjk/NotoSansCJK-Regular.ttc",
        // Fedora 38+ variable-font packages
        "/usr/share/fonts/google-noto-vf/NotoSans[wght].ttf",
        "/usr/share/fonts/google-noto-sans-cjk-vf-fonts/NotoSansCJK-VF.ttc",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf"
    });
#endif
}

std::string resolveSystemIconFontPath() {
#ifdef _WIN32
    return firstExistingPath({
        "C:/Windows/Fonts/seguisym.ttf",
        "C:/Windows/Fonts/SegMDL2.ttf",
        "C:/Windows/Fonts/seguiemj.ttf",
        "C:/Windows/Fonts/segoeui.ttf"
    });
#elif defined(__APPLE__)
    return firstExistingPath({
        "/System/Library/Fonts/Apple Symbols.ttf",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Helvetica.ttc"
    });
#else
    return firstExistingPath({
        // Debian / Ubuntu layout
        "/usr/share/fonts/fontawesome/fa-solid-900.ttf",
        "/usr/share/fonts/TTF/fa-solid-900.ttf",
        "/usr/share/fonts/truetype/font-awesome/fa-solid-900.ttf",
        "/usr/share/fonts/opentype/font-awesome/Font Awesome 6 Free-Solid-900.otf",
        "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf",
        "/usr/share/fonts/noto/NotoSansSymbols2-Regular.ttf",
        "/usr/share/fonts/google-noto/NotoSansSymbols2-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansSymbols-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        // Fedora / RHEL family (static packages)
        "/usr/share/fonts/google-noto-sans-symbols2/NotoSansSymbols2-Regular.ttf",
        "/usr/share/fonts/google-noto-sans-symbols-fonts/NotoSansSymbols-Regular.ttf",
        "/usr/share/fonts/google-noto-sans-symbols/NotoSansSymbols-Regular.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        // Fedora 38+ variable-font packages
        "/usr/share/fonts/google-noto-vf/NotoSansSymbols[wght].ttf"
    });
#endif
}

std::string resolveSystemEmojiFontPath() {
#ifdef _WIN32
    return firstExistingPath({
        "C:/Windows/Fonts/seguiemj.ttf",
        "C:/Windows/Fonts/seguisym.ttf"
    });
#elif defined(__APPLE__)
    return firstExistingPath({
        "/System/Library/Fonts/Apple Color Emoji.ttc"
    });
#else
    return firstExistingPath({
        // Debian / Ubuntu layout
        "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",
        "/usr/share/fonts/noto/NotoColorEmoji.ttf",
        "/usr/share/fonts/google-noto/NotoColorEmoji.ttf",
        // Fedora / RHEL family
        "/usr/share/fonts/google-noto-emoji/NotoColorEmoji.ttf",
        "/usr/share/fonts/google-noto-color-emoji-fonts/Noto-COLRv1.ttf"
    });
#endif
}

std::string resolveSystemMonospaceFontPath() {
#ifdef _WIN32
    return firstExistingPath({
        "C:/Windows/Fonts/CascadiaMono.ttf",
        "C:/Windows/Fonts/CascadiaCode.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/cour.ttf"
    });
#elif defined(__APPLE__)
    return firstExistingPath({
        "/System/Library/Fonts/SFNSMono.ttf",
        "/System/Library/Fonts/Supplemental/Menlo.ttc",
        "/System/Library/Fonts/Menlo.ttc",
        "/System/Library/Fonts/Monaco.ttf"
    });
#else
    return firstExistingPath({
        // Debian / Ubuntu layout
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
        "/usr/share/fonts/opentype/noto/NotoSansMono-Regular.ttf",
        "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
        "/usr/share/fonts/google-noto/NotoSansMono-Regular.ttf",
        "/usr/share/fonts/TTF/Hack-Regular.ttf",
        // Fedora / RHEL family (static packages)
        "/usr/share/fonts/google-noto-sans-mono/NotoSansMono-Regular.ttf",
        "/usr/share/fonts/google-noto-sans-mono-fonts/NotoSansMono-Regular.ttf",
        // Fedora 38+ variable-font packages
        "/usr/share/fonts/google-noto-vf/NotoSansMono[wght].ttf",
        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/liberation-mono-fonts/LiberationMono-Regular.ttf",
        "/usr/share/fonts/source-foundry-hack-fonts/Hack-Regular.ttf"
    });
#endif
}

std::filesystem::path executableDirectory() {
#ifdef _WIN32
    std::vector<char> buffer(MAX_PATH);
    DWORD length = 0;
    while (true) {
        length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size() - 1) {
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::path(buffer.data()).parent_path();
#elif defined(__APPLE__)
    std::vector<char> buffer(4096);
    uint32_t size = static_cast<uint32_t>(buffer.size());
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        buffer.resize(size);
        if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
            return {};
        }
    }
    std::error_code error;
    return std::filesystem::absolute(std::filesystem::path(buffer.data()), error).parent_path();
#elif defined(__linux__)
    std::vector<char> buffer(4096);
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length <= 0) {
        return {};
    }
    buffer[static_cast<size_t>(length)] = '\0';
    std::error_code error;
    return std::filesystem::absolute(std::filesystem::path(buffer.data()), error).parent_path();
#else
    return {};
#endif
}

std::string resolveProjectAssetPath(const std::string& filename) {
    const std::filesystem::path exeDir = executableDirectory();
    const std::filesystem::path candidates[] = {
        exeDir / "assets" / filename,
        std::filesystem::path("assets") / filename,
        std::filesystem::path("..") / "assets" / filename,
        std::filesystem::path("..") / ".." / "assets" / filename
    };

    for (const auto& candidate : candidates) {
        if (const std::string path = existingPath(candidate); !path.empty()) {
            return path;
        }
    }
    return {};
}

// 平台首选系统 CJK UI 字体。理由：打包字体是装饰性/单一字重（或变量字体），
// 既非用户熟悉的中文界面字体，也与「按同一份字体度量 + 渲染」的前提冲突；
// 系统 CJK 字体（微软雅黑 / 苹方 / Noto CJK）覆盖 CJK + Latin，且必然存在。
std::string resolvePreferredSystemUiFontPath() {
#ifdef _WIN32
    return firstExistingPath({
        "C:/Windows/Fonts/msyh.ttc",    // 微软雅黑（Win 7+ 中文界面默认）
        "C:/Windows/Fonts/msyhl.ttc",   // 微软雅黑 Light
        "C:/Windows/Fonts/simhei.ttf",  // 黑体（兼容旧系统）
    });
#elif defined(__APPLE__)
    return firstExistingPath({
        "/System/Library/Fonts/PingFang.ttc",          // 苹方（macOS 10.11+）
        "/System/Library/Fonts/Hiragino Sans GB.ttc",  // 冬青黑体简体中文
        "/System/Library/Fonts/STHeiti Light.ttc",
    });
#else
    return firstExistingPath({
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-sans-cjk/NotoSansCJK-Regular.ttc",
    });
#endif
}

std::string resolveDefaultUiFontPath() {
    // 优先级：显式 override（setDefaultFontFiles）→ 系统 CJK 字体 → 打包兜底
    // → 系统 UI 字体。原顺序把打包字体放最前，导致默认界面用装饰性字体且
    // 与系统字体混排（2026-09-27）
    const std::string& override = defaultUiFontFileOverride();
    if (!override.empty()) {
        if (const std::string existing = existingPath(resolveFontFilePath(override)); !existing.empty()) {
            return existing;
        }
    }
    if (const std::string systemCjk = resolvePreferredSystemUiFontPath(); !systemCjk.empty()) {
        return systemCjk;
    }
    const std::string bundled = resolveProjectAssetPath(kDefaultUiFontFile);
    if (const std::string existing = existingPath(bundled); !existing.empty()) {
        return existing;
    }
    return resolveSystemUiFontPath();
}

std::string resolveDefaultIconFontPath() {
    const std::string& override = defaultIconFontFileOverride();
    const std::string path = override.empty() ? resolveProjectAssetPath(kDefaultIconFontFile) : resolveFontFilePath(override);
    if (const std::string existing = existingPath(path); !existing.empty()) {
        return existing;
    }
    return resolveSystemIconFontPath();
}

std::string resolveFontFilePath(const std::string& path) {
    const std::filesystem::path raw(path);
    if (const std::string existing = existingPath(raw); !existing.empty()) {
        return existing;
    }

    const std::filesystem::path exeDir = executableDirectory();
    const std::filesystem::path candidates[] = {
        exeDir / "assets" / raw.filename(),
        exeDir / raw,
        std::filesystem::path("assets") / raw.filename(),
        std::filesystem::path("..") / "assets" / raw.filename(),
        std::filesystem::path("..") / ".." / "assets" / raw.filename()
    };

    for (const auto& candidate : candidates) {
        if (const std::string existing = existingPath(candidate); !existing.empty()) {
            return existing;
        }
    }
    return path;
}

bool isEmojiFontPath(const std::string& path) {
    const std::string filename = std::filesystem::path(path).filename().string();
    return filename.find("Emoji") != std::string::npos ||
           filename.find("emoji") != std::string::npos ||
           filename.find("seguiemj") != std::string::npos;
}

bool loadFontFace(const std::string& path, float fontSize, FontFace& face) {
    FT_Library library = sharedFreeTypeLibrary();
    if (!library) {
        return false;
    }

    FT_Face loadedFace = nullptr;
    if (FT_New_Face(library, path.c_str(), 0, &loadedFace) != 0 || !loadedFace) {
        return false;
    }

    const bool emojiFont = isEmojiFontPath(path);
    // CSS font-size semantics: fontSize is the em size. Scalable outline fonts
    // are sized directly (FT_Set_Char_Size at fontSize -> y_ppem == fontSize);
    // the upem/(ascender-descender) compensation is only kept for fixed-size
    // strikes, where it converts the requested em size into a comparable
    // strike ppem. Scaling every font by hhea metrics shrank Windows fonts
    // (Segoe UI factor ~0.75) and broke em-size expectations.
    float pixelHeightScale = 1.0f;
    if ((emojiFont || FT_HAS_COLOR(loadedFace)) && loadedFace->num_fixed_sizes > 0 &&
        loadedFace->units_per_EM > 0 && loadedFace->ascender != loadedFace->descender) {
        const float designHeight = static_cast<float>(loadedFace->ascender - loadedFace->descender);
        pixelHeightScale = static_cast<float>(loadedFace->units_per_EM) / designHeight;
    }
    const float scaledFontSize = std::max(1.0f, fontSize * pixelHeightScale);

    FT_Error sizeError = 1;
    float glyphScale = 1.0f;
    if ((emojiFont || FT_HAS_COLOR(loadedFace)) && loadedFace->num_fixed_sizes > 0) {
        const float targetPpem = scaledFontSize * 64.0f;
        int bestStrike = 0;
        float bestDistance = std::numeric_limits<float>::max();
        for (int i = 0; i < loadedFace->num_fixed_sizes; ++i) {
            const float strikePpem = static_cast<float>(loadedFace->available_sizes[i].y_ppem);
            const float distance = std::fabs(strikePpem - targetPpem);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestStrike = i;
            }
        }
        sizeError = FT_Select_Size(loadedFace, bestStrike);
        const float strikePpem = static_cast<float>(loadedFace->available_sizes[bestStrike].y_ppem) / 64.0f;
        if (strikePpem > 0.0f) {
            glyphScale = fontSize / strikePpem;
        }
    }
    if (sizeError != 0) {
        sizeError = FT_Set_Char_Size(
            loadedFace,
            0,
            static_cast<FT_F26Dot6>(std::lround(scaledFontSize * 64.0f)),
            72,
            72);
        if (sizeError != 0) {
            const FT_UInt pixelSize = static_cast<FT_UInt>(std::max(1.0f, std::round(scaledFontSize)));
            sizeError = FT_Set_Pixel_Sizes(loadedFace, 0, pixelSize);
        }
    }
    if (sizeError != 0) {
        FT_Done_Face(loadedFace);
        return false;
    }

    face.path = path;
    face.face = loadedFace;
    face.size = fontSize;
    face.glyphScale = glyphScale;
    face.colored = emojiFont || FT_HAS_COLOR(loadedFace);
    if (loadedFace->size && loadedFace->size->metrics.y_ppem > 0) {
        face.ascent = static_cast<float>(loadedFace->size->metrics.ascender) / 64.0f * glyphScale;
        face.descent = static_cast<float>(loadedFace->size->metrics.descender) / 64.0f * glyphScale;
        const float height = static_cast<float>(loadedFace->size->metrics.height) / 64.0f * glyphScale;
        face.lineGap = height - (face.ascent - face.descent);
    } else {
        face.ascent = fontSize * 0.8f;
        face.descent = -fontSize * 0.2f;
        face.lineGap = 0.0f;
    }
    return true;
}

std::string fontStackCacheKey(const std::string& fontPath, float fontSize, int fontWeight) {
    // fontWeight is included so that two text nodes pointing at the same path
    // (e.g. empty fontFamily resolved to a different weight file) still get
    // separate atlas / face caches when a future variant font selection kicks in.
    return fontPath + "#w" + std::to_string(fontWeight) + "#" + std::to_string(static_cast<int>(std::round(fontSize * 64.0f)));
}

// Impl::resolveFontPath 的本体（fontFamily 语义表驱动）。放在匿名命名空间里
// 直接复用上面的解析基础设施；对外经由 text_detail::resolveFontPathForFamily。
std::string resolveFontPathForFamilyImpl(const std::string& fontFamily, int fontWeight) {
    if (!fontFamily.empty() && fontFamily.find('.') != std::string::npos) {
        return resolveFontFilePath(fontFamily);
    }

    if (fontFamily == "YouSheBiaoTiHei" || fontFamily == "YouShe") {
        return resolveFontFilePath("YouSheBiaoTiHei-2.ttf");
    }

    if (fontFamily == "Title" || fontFamily == "PingFang" || fontFamily == "PingFang SC") {
        return resolveDefaultUiFontPath();
    }

    if (fontFamily == "FontAwesome" || fontFamily == "Font Awesome" ||
        fontFamily == "Font Awesome 7 Free" || fontFamily == "Icon") {
        return resolveDefaultIconFontPath();
    }

#ifdef _WIN32
    if (fontFamily == "monospace" || fontFamily == "Mono" ||
        fontFamily == "Cascadia Mono" || fontFamily == "Cascadia Code") {
        if (const std::string path = resolveSystemMonospaceFontPath(); !path.empty()) {
            return path;
        }
    }
    if (fontFamily == "Microsoft YaHei" || fontFamily == "YaHei") {
        return "C:/Windows/Fonts/msyh.ttc";
    }
    if (fontFamily == "Segoe UI Emoji" || fontFamily == "Emoji") {
        if (const std::string path = resolveSystemEmojiFontPath(); !path.empty()) {
            return path;
        }
        return resolveDefaultUiFontPath();
    }
    if (fontFamily == "SimHei") {
        return "C:/Windows/Fonts/simhei.ttf";
    }
    // Pick a weight-specific system UI font when available; fall back to the
    // bundled default font only when no matching weight file exists.
    if (const std::string path = resolveSystemUiFontPathForWeightImpl(fontWeight); !path.empty()) {
        return path;
    }
    return resolveDefaultUiFontPath();
#elif defined(__APPLE__)
    if (fontFamily == "monospace" || fontFamily == "Mono" ||
        fontFamily == "SF Mono" || fontFamily == "Menlo" || fontFamily == "Monaco") {
        if (const std::string path = resolveSystemMonospaceFontPath(); !path.empty()) {
            return path;
        }
    }
    if (fontFamily == "Noto Color Emoji" || fontFamily == "Apple Color Emoji" || fontFamily == "Emoji") {
        if (const std::string path = resolveSystemEmojiFontPath(); !path.empty()) {
            return path;
        }
        return resolveDefaultUiFontPath();
    }
    if (const std::string path = resolveSystemUiFontPathForWeightImpl(fontWeight); !path.empty()) {
        return path;
    }
    return resolveDefaultUiFontPath();
#else
    (void)fontWeight;
    if (fontFamily == "monospace" || fontFamily == "Mono") {
        if (const std::string path = resolveSystemMonospaceFontPath(); !path.empty()) {
            return path;
        }
        return resolveDefaultUiFontPath();
    }
    if (fontFamily == "Noto Color Emoji" || fontFamily == "Emoji") {
        if (const std::string path = resolveSystemEmojiFontPath(); !path.empty()) {
            return path;
        }
        return resolveDefaultUiFontPath();
    }
    return resolveDefaultUiFontPath();
#endif
}

bool isCombiningMark(unsigned int codepoint) {
    return (codepoint >= 0x0300 && codepoint <= 0x036F) ||
           (codepoint >= 0x1AB0 && codepoint <= 0x1AFF) ||
           (codepoint >= 0x1DC0 && codepoint <= 0x1DFF) ||
           (codepoint >= 0x20D0 && codepoint <= 0x20FF) ||
           (codepoint >= 0xFE20 && codepoint <= 0xFE2F);
}

bool isEmojiCodepoint(unsigned int codepoint) {
    return (codepoint >= 0x1F000 && codepoint <= 0x1FAFF) ||
           (codepoint >= 0x2600 && codepoint <= 0x27BF);
}

bool nextCodepointIsEmojiPresentation(const std::string& text, size_t index) {
    if (index >= text.size()) {
        return false;
    }
    const size_t saved = index;
    const unsigned int next = readUtf8Codepoint(text, index);
    (void)saved;
    return next == 0xFE0F;
}

size_t findFaceForCodepoint(FontInfoHolder& holder, unsigned int codepoint, float fontSize) {
    if (holder.faces.empty()) {
        return 0;
    }
    if (codepoint == ' ' || codepoint == '\t' || codepoint == 0) {
        return 0;
    }

    for (size_t i = 0; i < holder.faces.size(); ++i) {
        if (FT_Get_Char_Index(holder.faces[i].face, codepoint) != 0) {
            return i;
        }
    }

    for (const std::string& fallbackPath : holder.lazyFallbackPaths) {
        if (fallbackPath.empty()) {
            continue;
        }

        const bool alreadyLoaded = std::any_of(holder.faces.begin(), holder.faces.end(),
                                               [&](const FontFace& loadedFace) {
                                                   return loadedFace.path == fallbackPath;
                                               });
        if (alreadyLoaded) {
            continue;
        }

        FontFace fallback;
        if (!loadFontFace(fallbackPath, fontSize, fallback)) {
            continue;
        }

        const bool hasGlyph = FT_Get_Char_Index(fallback.face, codepoint) != 0;
        holder.faces.push_back(std::move(fallback));
        if (hasGlyph) {
            return holder.faces.size() - 1;
        }
    }

    return 0;
}

size_t findEmojiFaceForCodepoint(FontInfoHolder& holder, unsigned int codepoint, float fontSize) {
    for (size_t i = 0; i < holder.faces.size(); ++i) {
        if (holder.faces[i].colored && FT_Get_Char_Index(holder.faces[i].face, codepoint) != 0) {
            return i;
        }
    }

    for (const std::string& fallbackPath : holder.lazyFallbackPaths) {
        if (fallbackPath.empty() || !isEmojiFontPath(fallbackPath)) {
            continue;
        }

        const auto loaded = std::find_if(holder.faces.begin(), holder.faces.end(),
                                         [&](const FontFace& loadedFace) {
                                             return loadedFace.path == fallbackPath;
                                         });
        if (loaded != holder.faces.end()) {
            if (FT_Get_Char_Index(loaded->face, codepoint) != 0) {
                return static_cast<size_t>(std::distance(holder.faces.begin(), loaded));
            }
            continue;
        }

        FontFace fallback;
        if (!loadFontFace(fallbackPath, fontSize, fallback)) {
            continue;
        }

        const bool hasGlyph = FT_Get_Char_Index(fallback.face, codepoint) != 0;
        holder.faces.push_back(std::move(fallback));
        if (hasGlyph) {
            return holder.faces.size() - 1;
        }
    }

    return findFaceForCodepoint(holder, codepoint, fontSize);
}

float loadGlyphAdvance(const FontFace& face,
                       unsigned int glyphIndex,
                       unsigned int codepoint,
                       float fontSize,
                       float maxGlyphHeight) {
    if (codepoint == '\t') {
        return fontSize * 4.0f;
    }
    if (isCombiningMark(codepoint)) {
        return 0.0f;
    }
    if (FT_Load_Glyph(face.face, glyphIndex, kGlyphLoadFlags) != 0) {
        return fontSize * 0.5f;
    }

    float glyphScale = face.glyphScale;
    const float glyphHeight = static_cast<float>(face.face->glyph->metrics.height) / 64.0f;
    if (face.colored && glyphHeight > 0.0f) {
        glyphScale = std::min(glyphScale, maxGlyphHeight / glyphHeight);
    }
    if (FT_IS_SCALABLE(face.face)) {
        return static_cast<float>(face.face->glyph->linearHoriAdvance) / 65536.0f * glyphScale;
    }
    return static_cast<float>(face.face->glyph->advance.x) / 64.0f * glyphScale;
}

std::uint64_t makeGlyphKey(size_t faceIndex, unsigned int glyphIndex) {
    return ((static_cast<std::uint64_t>(faceIndex) + 1ULL) << 32) | static_cast<std::uint64_t>(glyphIndex);
}

std::vector<TextPrimitive::ShapedGlyph> shapeWithFallback(FontInfoHolder& holder,
                                                          const std::string& text,
                                                          float fontSize) {
    std::vector<TextPrimitive::ShapedGlyph> shaped;
    const float maxGlyphHeight = holder.faces.front().ascent - holder.faces.front().descent;
    size_t index = 0;
    while (index < text.size()) {
        const size_t start = index;
        const unsigned int codepoint = readUtf8Codepoint(text, index);
        const bool preferEmoji = isEmojiCodepoint(codepoint) || nextCodepointIsEmojiPresentation(text, index);
        const size_t faceIndex = preferEmoji
            ? findEmojiFaceForCodepoint(holder, codepoint, fontSize)
            : findFaceForCodepoint(holder, codepoint, fontSize);
        const FontFace& face = holder.faces[faceIndex];
        const unsigned int glyphIndex = codepoint == '\t' ? 0 : FT_Get_Char_Index(face.face, codepoint);
        const float advance = loadGlyphAdvance(face, glyphIndex, codepoint, fontSize, maxGlyphHeight);
        shaped.push_back({glyphIndex == 0 && codepoint == '\t' ? 0 : makeGlyphKey(faceIndex, glyphIndex),
                          codepoint,
                          static_cast<int>(start),
                          static_cast<int>(index),
                          advance,
                          0.0f,
                          0.0f});
    }
    return shaped;
}

} // namespace

FontStackCache& sharedFontStackCache() {
    static FontStackCache cache;
    return cache;
}

unsigned int readUtf8Codepoint(const std::string& text, size_t& index) {
    const unsigned char first = static_cast<unsigned char>(text[index++]);
    if (first < 0x80) {
        return first;
    }
    if ((first >> 5) == 0x6 && index < text.size()) {
        return ((first & 0x1F) << 6) | (static_cast<unsigned char>(text[index++]) & 0x3F);
    }
    if ((first >> 4) == 0xE && index + 1 < text.size()) {
        unsigned int cp = (first & 0x0F) << 12;
        cp |= (static_cast<unsigned char>(text[index++]) & 0x3F) << 6;
        cp |= static_cast<unsigned char>(text[index++]) & 0x3F;
        return cp;
    }
    if ((first >> 3) == 0x1E && index + 2 < text.size()) {
        unsigned int cp = (first & 0x07) << 18;
        cp |= (static_cast<unsigned char>(text[index++]) & 0x3F) << 12;
        cp |= (static_cast<unsigned char>(text[index++]) & 0x3F) << 6;
        cp |= static_cast<unsigned char>(text[index++]) & 0x3F;
        return cp;
    }
    return '?';
}

std::string resolveSystemUiFontPathForWeightImpl(int fontWeight) {
    return resolveSystemUiFontPathForWeightPlatform(fontWeight);
}

std::string& defaultUiFontFileOverride() {
    static std::string value;
    return value;
}

std::string& defaultIconFontFileOverride() {
    static std::string value;
    return value;
}

std::shared_ptr<FontInfoHolder> loadSharedFontStack(const std::string& fontPath, float fontSize, int fontWeight) {
    const std::string cacheKey = fontStackCacheKey(fontPath, fontSize, fontWeight);
    FontStackCache& cache = sharedFontStackCache();
    const auto existing = cache.entries.find(cacheKey);
    if (existing != cache.entries.end()) {
        existing->second.lastUsed = ++cache.accessTick;
        return existing->second.holder;
    }

    auto holder = std::make_shared<FontInfoHolder>();

    auto addLoadedFace = [&](const std::string& path) {
        if (path.empty()) {
            return false;
        }
        const bool alreadyLoaded = std::any_of(holder->faces.begin(), holder->faces.end(),
                                               [&](const FontFace& loadedFace) {
                                                   return loadedFace.path == path;
                                               });
        if (alreadyLoaded) {
            return true;
        }
        FontFace face;
        if (!loadFontFace(path, fontSize, face)) {
            return false;
        }
        holder->faces.push_back(std::move(face));
        return true;
    };

    if (!addLoadedFace(fontPath) &&
        !addLoadedFace(resolveDefaultUiFontPath()) &&
        !addLoadedFace(resolveSystemUiFontPath())) {
        return {};
    }

    auto addLazyFallback = [&](const std::string& fallbackPath) {
        if (fallbackPath.empty() || fallbackPath == fontPath) {
            return;
        }
        if (std::find(holder->lazyFallbackPaths.begin(), holder->lazyFallbackPaths.end(), fallbackPath) == holder->lazyFallbackPaths.end()) {
            holder->lazyFallbackPaths.push_back(fallbackPath);
        }
    };

    addLazyFallback(resolveDefaultUiFontPath());
    addLazyFallback(resolveDefaultIconFontPath());
    addLazyFallback(resolveSystemUiFontPath());
    addLazyFallback(resolveSystemIconFontPath());
    addLazyFallback(resolveSystemEmojiFontPath());
    // Also register weight-specific fallbacks so glyphs that aren't on the
    // primary weight face (e.g. a CJK glyph missing from SFNS-Bold) still
    // resolve through the regular or light face of the same family.
    addLazyFallback(resolveSystemUiFontPathForWeightImpl(fontWeight == 700 ? 400 : 700));

#ifdef _WIN32
    addLazyFallback("C:/Windows/Fonts/seguiemj.ttf");
    addLazyFallback("C:/Windows/Fonts/seguisym.ttf");
    addLazyFallback("C:/Windows/Fonts/msyh.ttc");
    addLazyFallback("C:/Windows/Fonts/simhei.ttf");
#elif defined(__APPLE__)
    addLazyFallback("/System/Library/Fonts/Apple Color Emoji.ttc");
    addLazyFallback("/System/Library/Fonts/Supplemental/Arial Unicode.ttf");
    addLazyFallback("/System/Library/Fonts/Supplemental/Arial.ttf");
#else
    // Debian / Ubuntu layout
    addLazyFallback("/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf");
    addLazyFallback("/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc");
    addLazyFallback("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    // Fedora / RHEL family
    addLazyFallback("/usr/share/fonts/google-noto-emoji/NotoColorEmoji.ttf");
    addLazyFallback("/usr/share/fonts/google-noto-color-emoji-fonts/Noto-COLRv1.ttf");
    addLazyFallback("/usr/share/fonts/google-noto-sans-cjk/NotoSansCJK-Regular.ttc");
    addLazyFallback("/usr/share/fonts/google-noto-sans-cjk-vf-fonts/NotoSansCJK-VF.ttc");
    addLazyFallback("/usr/share/fonts/dejavu/DejaVuSans.ttf");
#endif

    if (cache.entries.size() >= kFontStackCacheCapacity) {
        const auto oldest = std::min_element(cache.entries.begin(), cache.entries.end(),
                                             [](const auto& left, const auto& right) {
                                                 return left.second.lastUsed < right.second.lastUsed;
                                             });
        if (oldest != cache.entries.end()) {
            cache.entries.erase(oldest);
        }
    }

    const auto inserted = cache.entries.emplace(cacheKey, FontStackCacheEntry{holder, ++cache.accessTick});
    return inserted.first->second.holder;
}

std::vector<TextPrimitive::ShapedGlyph> shapeTextWithFontStack(FontInfoHolder& holder,
                                                               const std::string& text,
                                                               float fontSize) {
    // shapeWithFallback already advances UTF-8 one codepoint at a time, so a
    // glyph's byteEnd is the next glyph's byteStart.  Avoid rescanning every
    // glyph to rediscover that boundary; long editable text is measured often.
    return shapeWithFallback(holder, text, fontSize);
}

std::string resolveFontPathForFamily(const std::string& fontFamily, int fontWeight) {
    return resolveFontPathForFamilyImpl(fontFamily, fontWeight);
}

} // namespace text_detail

std::vector<TextPrimitive::ShapedGlyph> TextPrimitive::Impl::shapeText(const std::string& text) {
    if (fontDirty_ && !loadFont()) {
        return {};
    }
    auto holder = std::static_pointer_cast<text_detail::FontInfoHolder>(fontInfoStorage_);
    if (!holder || holder->faces.empty()) {
        return {};
    }
    return text_detail::shapeTextWithFontStack(*holder, text, std::max(1.0f, style_.fontSize));
}

std::string TextPrimitive::Impl::resolveFontPath(const std::string& fontFamily, int fontWeight) {
    return text_detail::resolveFontPathForFamily(fontFamily, fontWeight);
}

} // namespace core
