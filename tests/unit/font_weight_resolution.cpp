#include "core/render/text.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool hasFontFile(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    return std::ifstream(path).good();
}

const char* platformName() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Linux";
#endif
}

} // namespace

int main() {
    using core::TextPrimitive;

    int failures = 0;

    // 1. Explicit file paths (with a dot in the family name) are passed through
    //    untouched regardless of weight. Users should always be able to point at
    //    a specific TTF when they know exactly which face they want.
    {
        const std::string path400 = TextPrimitive::resolveFontPath("/path/to/MyFont.ttf", 400);
        const std::string path700 = TextPrimitive::resolveFontPath("/path/to/MyFont.ttf", 700);
        if (path400 != "/path/to/MyFont.ttf" || path700 != "/path/to/MyFont.ttf") {
            std::cerr << "[1] explicit file path passthrough failed: 400='" << path400
                      << "' 700='" << path700 << "'\n";
            ++failures;
        }
    }

    // 2. Display / bundled aliases must still resolve to the bundled default
    //    UI font. Otherwise apps that rely on the curated display look break.
    {
        const std::string title400 = TextPrimitive::resolveFontPath("Title", 400);
        const std::string title700 = TextPrimitive::resolveFontPath("Title", 700);
        const std::string yoshe400 = TextPrimitive::resolveFontPath("YouSheBiaoTiHei", 400);
        if (title400.empty()) {
            std::cerr << "[2] 'Title' alias returned empty path\n";
            ++failures;
        }
        if (title400 != title700) {
            std::cerr << "[2] 'Title' alias should be weight-independent, got '"
                      << title400 << "' vs '" << title700 << "'\n";
            ++failures;
        }
        if (yoshe400.empty()) {
            std::cerr << "[2] 'YouSheBiaoTiHei' alias returned empty path\n";
            ++failures;
        }
    }

    // 3. The key behavior change: weight-specific system UI font paths must
    //    differ from the regular one whenever the platform actually ships
    //    weight-specific files (macOS, Windows, modern Fedora). When the
    //    system only has a single Regular file, the resolver falls back to
    //    the regular path — that case is also valid (we just print a note).
    {
        const std::string regular = TextPrimitive::resolveSystemUiFontPathForWeight(400);
        const std::string bold = TextPrimitive::resolveSystemUiFontPathForWeight(700);
        const std::string heavy = TextPrimitive::resolveSystemUiFontPathForWeight(800);
        const std::string light = TextPrimitive::resolveSystemUiFontPathForWeight(300);

        if (regular.empty()) {
            std::cerr << "[3] resolveSystemUiFontPathForWeight(400) returned empty on "
                      << platformName() << "\n";
            ++failures;
        }
        if (regular == bold && regular == light && hasFontFile(regular)) {
            // All paths collapse to the same file but at least one font exists.
            // This is the expected fallback on minimal systems (e.g. CI Linux
            // containers without NotoSans-Bold). Don't fail — just record.
            std::cout << "[3] note: " << platformName()
                      << " has no weight-specific UI font files; all weights fall back to "
                      << regular << "\n";
        } else if (regular == bold) {
            std::cerr << "[3] bold and regular resolved to the same file on "
                      << platformName() << " (" << regular
                      << ") but the system apparently has weight variants\n";
            ++failures;
        }
        if (!bold.empty() && heavy == bold) {
            // Heavy is allowed to fall back to Bold when no Heavy file exists.
            std::cout << "[3] note: " << platformName()
                      << " has no explicit Heavy variant; falls back to " << bold << "\n";
        }
    }

    // 4. measureTextMetrics should be callable with arbitrary weights; it
    //    must not crash and must return a non-empty byteIndices for non-empty
    //    text. We don't assert a specific width because platform font
    //    availability varies.
    {
        const std::string sample = "The quick brown fox";
        for (int weight : {300, 400, 500, 700, 900}) {
            const auto metrics = TextPrimitive::measureTextMetrics(sample, "", 16.0f, weight);
            if (metrics.byteIndices.empty() || metrics.caretX.empty()) {
                std::cerr << "[4] measureTextMetrics returned empty data for weight "
                          << weight << "\n";
                ++failures;
            }
            if (metrics.width <= 0.0f) {
                std::cerr << "[4] measureTextMetrics returned non-positive width for weight "
                          << weight << "\n";
                ++failures;
            }
        }
    }

    if (failures == 0) {
        std::cout << "font_weight_resolution: all checks passed on " << platformName() << "\n";
        return 0;
    }
    std::cerr << "font_weight_resolution: " << failures << " check(s) failed on "
              << platformName() << "\n";
    return 1;
}
