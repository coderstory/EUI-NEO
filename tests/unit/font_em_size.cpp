#include "core/render/text.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H

#include <cmath>
#include <iostream>
#include <string>

// Verifies CSS font-size semantics: fontSize is the em size. The measured
// advance width of a text at size F must be F * (sum of unhinted design
// advances / units_per_EM). Before v0.7.0 every scalable font was scaled by
// upem/(ascender-descender) (Segoe UI ~0.75), which this test catches.

namespace {

const char* kSample = "Hamburgefonstiv 0123";

// Sum of glyph advances in em units, read straight from the font file with
// FreeType - independent of EUI-NEO's sizing logic.
bool expectedEmWidth(const std::string& fontPath, const std::string& sample, double& outEm) {
    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0) {
        return false;
    }
    FT_Face face = nullptr;
    if (FT_New_Face(library, fontPath.c_str(), 0, &face) != 0 || !face) {
        FT_Done_FreeType(library);
        return false;
    }
    bool ok = FT_IS_SCALABLE(face) && face->units_per_EM > 0;
    double em = 0.0;
    for (char c : sample) {
        const FT_UInt glyph = FT_Get_Char_Index(face, static_cast<unsigned char>(c));
        FT_Fixed advance = 0;
        if (glyph == 0 || FT_Get_Advance(face, glyph, FT_LOAD_NO_SCALE, &advance) != 0) {
            // A character EUI would have to serve from a fallback font would
            // make the comparison meaningless.
            ok = false;
            break;
        }
        // FT_Get_Advance with FT_LOAD_NO_SCALE returns plain font units.
        em += static_cast<double>(advance) / face->units_per_EM;
    }
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    if (ok) {
        outEm = em;
    }
    return ok;
}

} // namespace

int main() {
    using core::TextPrimitive;

    int failures = 0;

    const std::string fontPath = TextPrimitive::resolveSystemUiFontPathForWeight(400);
    double expectedEm = 0.0;
    if (fontPath.empty() || !expectedEmWidth(fontPath, kSample, expectedEm)) {
        // No scalable system UI font with full ASCII coverage - nothing to
        // compare against on this machine.
        std::cout << "font_em_size: skipped (no scalable system UI font at '"
                  << fontPath << "')\n";
        return 0;
    }

    // 1. fontSize is the em size: measured width at 100px == 100 * em-width.
    {
        const float width = TextPrimitive::measureTextWidth(kSample, fontPath, 100.0f);
        const double ratio = width / 100.0;
        const double tolerance = expectedEm * 0.02;
        if (std::fabs(ratio - expectedEm) > tolerance) {
            std::cerr << "[1] em semantics violated: width/100px=" << ratio
                      << " expected em-width=" << expectedEm
                      << " (ratio " << ratio / expectedEm << ")\n";
            ++failures;
        }
    }

    // 2. Linear in fontSize at a regular UI size (16px), not in hhea metrics.
    {
        const float width = TextPrimitive::measureTextWidth(kSample, fontPath, 16.0f);
        const double expected = expectedEm * 16.0;
        if (std::fabs(width - expected) > expected * 0.02) {
            std::cerr << "[2] em semantics violated at 16px: width=" << width
                      << " expected=" << expected << "\n";
            ++failures;
        }
    }

    if (failures == 0) {
        std::cout << "font_em_size: all checks passed (em-width " << expectedEm
                  << " from " << fontPath << ")\n";
        return 0;
    }
    std::cerr << "font_em_size: " << failures << " check(s) failed\n";
    return 1;
}
