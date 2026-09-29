#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "cam/hct.h"
#include "quantize/celebi.h"
#include "score/score.h"

namespace material_color_utilities {

// Material 3 baseline seed color (#6750A4).
// Official fallback used in Android / MCU test suites (typescript/scheme/scheme_test.ts).
constexpr Argb kDefaultBaselineColor = 0xFF6750A4;

// Converts an ARGB integer to a CSS hex string (#rrggbb)
std::string ArgbToHex(Argb argb) {
    char buffer[10];
    std::snprintf(buffer, sizeof(buffer), "#%06x", argb & 0x00FFFFFF);
    return std::string(buffer);
}

/**
 * Extracts dominant colors from pixels and derives a Material 3 Dark Theme.
 * @param jsUint32Array Raw ARGB pixels from HTML5 Canvas.
 */
emscripten::val themeFromImage(const emscripten::val& jsUint32Array) {
    // 1. Copy TypedArray memory from JavaScript to native vector
    const unsigned int pixel_count = jsUint32Array["length"].as<unsigned int>();
    std::vector<Argb> pixels(pixel_count);
    
    emscripten::val memory_view{emscripten::typed_memory_view(pixel_count, pixels.data())};
    memory_view.call<void>("set", jsUint32Array);

    // 2. Quantize (Celebi) and rank colors with Material You scoring
    constexpr int kMaxColors = 32;
    QuantizerResult quant_result = QuantizeCelebi(pixels, kMaxColors);
    std::vector<Argb> ranked = RankedSuggestions(quant_result.color_to_count);
    const Argb source = ranked.empty() ? kDefaultBaselineColor : ranked[0];

    // 3. Convert dominant color to HCT perceptual space
    Hct hct(source);
    const double hue = hct.get_hue();
    const double chroma = hct.get_chroma();

    // 4. Derive Material Design 3 Dark Theme roles:
    // - Tones (L*) sourced from: cpp/dynamiccolor/material_dynamic_colors.cc
    // - Chromas sourced from:    cpp/scheme/scheme_tonal_spot.cc
    const Argb primary      = Hct(hue, std::max(chroma, 48.0), 80.0).ToInt(); // Primary: Tone 80, min chroma 48
    const Argb background   = Hct(hue, std::min(chroma / 6.0, 4.0), 10.0).ToInt(); // Background: Tone 10, neutral chroma
    const Argb surface      = Hct(hue, std::min(chroma / 6.0, 4.0), 14.0).ToInt(); // SurfaceContainer: Tone 14
    const Argb onBackground = Hct(hue, std::min(chroma / 6.0, 4.0), 90.0).ToInt(); // OnBackground: Tone 90
    const Argb outline      = Hct(hue, std::min(chroma / 6.0, 8.0), 30.0).ToInt(); // OutlineVariant: Tone 30, chroma 8

    // 5. Package results into a JavaScript object
    emscripten::val theme = emscripten::val::object();
    theme.set("source", ArgbToHex(source));
    theme.set("primary", ArgbToHex(primary));
    theme.set("background", ArgbToHex(background));
    theme.set("surface", ArgbToHex(surface));
    theme.set("onBackground", ArgbToHex(onBackground));
    theme.set("outline", ArgbToHex(outline));

    return theme;
}

EMSCRIPTEN_BINDINGS(material_color_module) {
    emscripten::function("themeFromImage", &themeFromImage);
}

}  // namespace material_color_utilities