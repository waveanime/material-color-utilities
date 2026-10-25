/**
 * @file bindings.cc
 * @brief Emscripten WebAssembly bindings for Material Color Utilities (MCU).
 *
 * Exposes core algorithms including Celebi quantization, color scoring,
 * dynamic scheme generation (Material Design 3), tonal palettes,
 * and CAM16-UCS color harmonization to JavaScript and WebAssembly runtimes.
 */

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cam/hct.h"
#include "quantize/celebi.h"
#include "score/score.h"
#include "blend/blend.h"
#include "dynamiccolor/dynamic_scheme.h"
#include "dynamiccolor/material_dynamic_colors.h"
#include "dynamiccolor/variant.h"
#include "palettes/tones.h"
#include "scheme/scheme_content.h"
#include "scheme/scheme_expressive.h"
#include "scheme/scheme_fidelity.h"
#include "scheme/scheme_fruit_salad.h"
#include "scheme/scheme_monochrome.h"
#include "scheme/scheme_neutral.h"
#include "scheme/scheme_rainbow.h"
#include "scheme/scheme_tonal_spot.h"
#include "scheme/scheme_vibrant.h"
#include "utils/utils.h"

namespace material_color_utilities {

// Material Design 3 baseline primary seed color (#6750A4).
constexpr Argb kDefaultBaselineColor = 0xFF6750A4;

/**
 * Converts a 32-bit ARGB integer to a lowercase CSS hex string (#rrggbb).
 */
std::string ArgbToHex(Argb argb) {
    char buffer[10];
    std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", (argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
    return std::string(buffer);
}

/**
 * Parses a CSS hex string (#rgb, #rrggbb, #aarrggbb) into a 32-bit ARGB integer.
 * Falls back to kDefaultBaselineColor if parsing fails.
 */
Argb HexToArgb(const std::string& hexInput) {
    std::string hex = hexInput;
    if (!hex.empty() && hex[0] == '#') {
        hex = hex.substr(1);
    }
    if (hex.length() == 3) {
        std::string expanded;
        for (char c : hex) {
            expanded += c;
            expanded += c;
        }
        hex = expanded;
    }
    if (hex.length() == 6) {
        unsigned int val = 0;
        std::stringstream ss;
        ss << std::hex << hex;
        ss >> val;
        return 0xFF000000 | val;
    }
    if (hex.length() == 8) {
        unsigned long long val = 0;
        std::stringstream ss;
        ss << std::hex << hex;
        ss >> val;
        return static_cast<Argb>(val);
    }
    return kDefaultBaselineColor;
}

/**
 * Maps a string identifier to its corresponding Material Design 3 Variant enum.
 * Defaults to Variant::kTonalSpot if the string is unrecognized.
 */
Variant StringToVariant(const std::string& str) {
    if (str == "vibrant") return Variant::kVibrant;
    if (str == "expressive") return Variant::kExpressive;
    if (str == "content") return Variant::kContent;
    if (str == "fidelity") return Variant::kFidelity;
    if (str == "monochrome") return Variant::kMonochrome;
    if (str == "neutral") return Variant::kNeutral;
    if (str == "rainbow") return Variant::kRainbow;
    if (str == "fruitSalad" || str == "fruit_salad") return Variant::kFruitSalad;
    return Variant::kTonalSpot;
}

/**
 * Ingests a JavaScript pixel buffer and converts it into a native ARGB vector.
 *
 * Handles both:
 * - Uint8ClampedArray / Uint8Array: 4-byte RGBA stride (HTML5 Canvas ImageData.data).
 *   Filters out non-opaque pixels (alpha < 255) per Material Design specifications.
 * - Uint32Array: Direct 32-bit packed ARGB integers.
 *
 * @param jsPixels JavaScript typed array containing pixel data.
 * @return Vector of opaque 32-bit ARGB pixels.
 */
std::vector<Argb> ExtractPixelsFromJs(const emscripten::val& jsPixels) {
    const unsigned int length = jsPixels["length"].as<unsigned int>();
    std::vector<Argb> pixels;
    if (length == 0) {
        return pixels;
    }

    bool is_byte_array = false;
    emscripten::val bytes_per_element = jsPixels["BYTES_PER_ELEMENT"];
    if (!bytes_per_element.isUndefined()) {
        is_byte_array = (bytes_per_element.as<int>() == 1);
    }

    if (is_byte_array) {
        unsigned int pixel_count = length / 4;
        pixels.reserve(pixel_count);
        std::vector<uint8_t> raw_bytes(length);
        emscripten::val memory_view{emscripten::typed_memory_view(length, raw_bytes.data())};
        memory_view.call<void>("set", jsPixels);

        for (unsigned int i = 0; i < length; i += 4) {
            uint8_t r = raw_bytes[i];
            uint8_t g = raw_bytes[i + 1];
            uint8_t b = raw_bytes[i + 2];
            uint8_t a = raw_bytes[i + 3];
            if (a < 255) continue;
            pixels.push_back(0xFF000000 | (r << 16) | (g << 8) | b);
        }
    } else {
        pixels.resize(length);
        emscripten::val memory_view{emscripten::typed_memory_view(length, pixels.data())};
        memory_view.call<void>("set", jsPixels);
    }

    return pixels;
}

/**
 * Instantiates the appropriate DynamicScheme subclass based on the requested variant.
 */
std::unique_ptr<DynamicScheme> CreateScheme(Hct sourceHct, Variant variant, bool isDark, double contrastLevel) {
    switch (variant) {
        case Variant::kTonalSpot:
            return std::make_unique<SchemeTonalSpot>(sourceHct, isDark, contrastLevel);
        case Variant::kVibrant:
            return std::make_unique<SchemeVibrant>(sourceHct, isDark, contrastLevel);
        case Variant::kExpressive:
            return std::make_unique<SchemeExpressive>(sourceHct, isDark, contrastLevel);
        case Variant::kContent:
            return std::make_unique<SchemeContent>(sourceHct, isDark, contrastLevel);
        case Variant::kFidelity:
            return std::make_unique<SchemeFidelity>(sourceHct, isDark, contrastLevel);
        case Variant::kMonochrome:
            return std::make_unique<SchemeMonochrome>(sourceHct, isDark, contrastLevel);
        case Variant::kNeutral:
            return std::make_unique<SchemeNeutral>(sourceHct, isDark, contrastLevel);
        case Variant::kRainbow:
            return std::make_unique<SchemeRainbow>(sourceHct, isDark, contrastLevel);
        case Variant::kFruitSalad:
            return std::make_unique<SchemeFruitSalad>(sourceHct, isDark, contrastLevel);
    }
    return std::make_unique<SchemeTonalSpot>(sourceHct, isDark, contrastLevel);
}

/**
 * Serializes all standard Material Design 3 color roles from a DynamicScheme
 * into a JavaScript object containing camelCase tokens mapped to hex strings.
 */
emscripten::val SchemeToJsObject(const DynamicScheme& scheme) {
    emscripten::val c = emscripten::val::object();

    // Primary
    c.set("primary", ArgbToHex(scheme.GetPrimary()));
    c.set("onPrimary", ArgbToHex(scheme.GetOnPrimary()));
    c.set("primaryContainer", ArgbToHex(scheme.GetPrimaryContainer()));
    c.set("onPrimaryContainer", ArgbToHex(scheme.GetOnPrimaryContainer()));
    c.set("inversePrimary", ArgbToHex(scheme.GetInversePrimary()));

    // Secondary
    c.set("secondary", ArgbToHex(scheme.GetSecondary()));
    c.set("onSecondary", ArgbToHex(scheme.GetOnSecondary()));
    c.set("secondaryContainer", ArgbToHex(scheme.GetSecondaryContainer()));
    c.set("onSecondaryContainer", ArgbToHex(scheme.GetOnSecondaryContainer()));

    // Tertiary
    c.set("tertiary", ArgbToHex(scheme.GetTertiary()));
    c.set("onTertiary", ArgbToHex(scheme.GetOnTertiary()));
    c.set("tertiaryContainer", ArgbToHex(scheme.GetTertiaryContainer()));
    c.set("onTertiaryContainer", ArgbToHex(scheme.GetOnTertiaryContainer()));

    // Error
    c.set("error", ArgbToHex(scheme.GetError()));
    c.set("onError", ArgbToHex(scheme.GetOnError()));
    c.set("errorContainer", ArgbToHex(scheme.GetErrorContainer()));
    c.set("onErrorContainer", ArgbToHex(scheme.GetOnErrorContainer()));

    // Backgrounds & Surfaces
    c.set("background", ArgbToHex(scheme.GetBackground()));
    c.set("onBackground", ArgbToHex(scheme.GetOnBackground()));
    c.set("surface", ArgbToHex(scheme.GetSurface()));
    c.set("onSurface", ArgbToHex(scheme.GetOnSurface()));
    c.set("surfaceVariant", ArgbToHex(scheme.GetSurfaceVariant()));
    c.set("onSurfaceVariant", ArgbToHex(scheme.GetOnSurfaceVariant()));
    c.set("surfaceDim", ArgbToHex(scheme.GetSurfaceDim()));
    c.set("surfaceBright", ArgbToHex(scheme.GetSurfaceBright()));
    c.set("surfaceContainerLowest", ArgbToHex(scheme.GetSurfaceContainerLowest()));
    c.set("surfaceContainerLow", ArgbToHex(scheme.GetSurfaceContainerLow()));
    c.set("surfaceContainer", ArgbToHex(scheme.GetSurfaceContainer()));
    c.set("surfaceContainerHigh", ArgbToHex(scheme.GetSurfaceContainerHigh()));
    c.set("surfaceContainerHighest", ArgbToHex(scheme.GetSurfaceContainerHighest()));
    c.set("inverseSurface", ArgbToHex(scheme.GetInverseSurface()));
    c.set("inverseOnSurface", ArgbToHex(scheme.GetInverseOnSurface()));

    // Outlines & Utilities
    c.set("outline", ArgbToHex(scheme.GetOutline()));
    c.set("outlineVariant", ArgbToHex(scheme.GetOutlineVariant()));
    c.set("shadow", ArgbToHex(scheme.GetShadow()));
    c.set("scrim", ArgbToHex(scheme.GetScrim()));
    c.set("surfaceTint", ArgbToHex(scheme.GetSurfaceTint()));

    // Fixed Colors
    c.set("primaryFixed", ArgbToHex(scheme.GetPrimaryFixed()));
    c.set("primaryFixedDim", ArgbToHex(scheme.GetPrimaryFixedDim()));
    c.set("onPrimaryFixed", ArgbToHex(scheme.GetOnPrimaryFixed()));
    c.set("onPrimaryFixedVariant", ArgbToHex(scheme.GetOnPrimaryFixedVariant()));
    c.set("secondaryFixed", ArgbToHex(scheme.GetSecondaryFixed()));
    c.set("secondaryFixedDim", ArgbToHex(scheme.GetSecondaryFixedDim()));
    c.set("onSecondaryFixed", ArgbToHex(scheme.GetOnSecondaryFixed()));
    c.set("onSecondaryFixedVariant", ArgbToHex(scheme.GetOnSecondaryFixedVariant()));
    c.set("tertiaryFixed", ArgbToHex(scheme.GetTertiaryFixed()));
    c.set("tertiaryFixedDim", ArgbToHex(scheme.GetTertiaryFixedDim()));
    c.set("onTertiaryFixed", ArgbToHex(scheme.GetOnTertiaryFixed()));
    c.set("onTertiaryFixedVariant", ArgbToHex(scheme.GetOnTertiaryFixedVariant()));

    // Palette Key Colors
    c.set("primaryPaletteKeyColor", ArgbToHex(scheme.GetPrimaryPaletteKeyColor()));
    c.set("secondaryPaletteKeyColor", ArgbToHex(scheme.GetSecondaryPaletteKeyColor()));
    c.set("tertiaryPaletteKeyColor", ArgbToHex(scheme.GetTertiaryPaletteKeyColor()));
    c.set("neutralPaletteKeyColor", ArgbToHex(scheme.GetNeutralPaletteKeyColor()));
    c.set("neutralVariantPaletteKeyColor", ArgbToHex(scheme.GetNeutralVariantPaletteKeyColor()));

    return c;
}

/**
 * Serializes standard palette tones (0, 10, 20, ..., 95, 99, 100) into a key-value JS object.
 */
emscripten::val PaletteToJsObject(const TonalPalette& palette) {
    emscripten::val tonesObj = emscripten::val::object();
    const int commonTones[] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 95, 99, 100};
    for (int t : commonTones) {
        tonesObj.set(t, ArgbToHex(palette.get(static_cast<double>(t))));
    }
    return tonesObj;
}

/**
 * Serializes all foundational tonal palettes (primary, secondary, tertiary,
 * neutral, neutralVariant, error) of a DynamicScheme.
 */
emscripten::val BuildPalettesObject(const DynamicScheme& scheme) {
    emscripten::val palettes = emscripten::val::object();
    palettes.set("primary", PaletteToJsObject(scheme.primary_palette));
    palettes.set("secondary", PaletteToJsObject(scheme.secondary_palette));
    palettes.set("tertiary", PaletteToJsObject(scheme.tertiary_palette));
    palettes.set("neutral", PaletteToJsObject(scheme.neutral_palette));
    palettes.set("neutralVariant", PaletteToJsObject(scheme.neutral_variant_palette));
    palettes.set("error", PaletteToJsObject(scheme.error_palette));
    return palettes;
}

/**
 * Constructs the complete theme payload containing the source color, metadata,
 * dual light/dark schemes, raw tonal palettes, and candidate colors.
 */
emscripten::val BuildThemePayload(Hct sourceHct, const std::string& variantStr, double contrastLevel, const std::vector<Argb>& candidateColors = {}) {
    Variant variant = StringToVariant(variantStr);

    auto lightScheme = CreateScheme(sourceHct, variant, false, contrastLevel);
    auto darkScheme = CreateScheme(sourceHct, variant, true, contrastLevel);

    emscripten::val theme = emscripten::val::object();
    theme.set("source", ArgbToHex(sourceHct.ToInt()));
    theme.set("variant", variantStr);
    theme.set("contrastLevel", contrastLevel);

    emscripten::val schemes = emscripten::val::object();
    schemes.set("light", SchemeToJsObject(*lightScheme));
    schemes.set("dark", SchemeToJsObject(*darkScheme));
    theme.set("schemes", schemes);

    theme.set("palettes", BuildPalettesObject(*lightScheme));

    if (!candidateColors.empty()) {
        emscripten::val extracted = emscripten::val::array();
        for (size_t i = 0; i < candidateColors.size(); ++i) {
            extracted.set(i, ArgbToHex(candidateColors[i]));
        }
        theme.set("extractedColors", extracted);
    }

    return theme;
}

/**
 * Generates a complete Material Design 3 theme (Light and Dark schemes with
 * all ~45 semantic tokens and tonal palettes) from a seed hex color.
 *
 * @param hexOrArgb Seed color in hex format (e.g. "#6750A4") or ARGB integer string.
 * @param variant Style variant ("tonalSpot", "vibrant", "expressive", etc.).
 * @param contrastLevel Contrast adjustment level from -1.0 to 1.0 (default: 0.0).
 * @return Complete theme JavaScript object.
 */
emscripten::val themeFromSourceColor(const std::string& hexOrArgb, const std::string& variant = "tonalSpot", double contrastLevel = 0.0) {
    Hct source(HexToArgb(hexOrArgb));
    return BuildThemePayload(source, variant, contrastLevel);
}

/**
 * Quantizes and scores an image pixel buffer, extracts the dominant theme color,
 * and generates the complete dynamic theme.
 *
 * @param jsPixels Image pixel array (Uint8ClampedArray from Canvas or Uint32Array).
 * @param variant Theme style variant.
 * @param contrastLevel Contrast adjustment level from -1.0 to 1.0.
 * @param maxColors Maximum number of clusters for Celebi quantization (default: 128).
 * @return Complete theme payload including ranked candidate colors.
 */
emscripten::val themeFromImage(const emscripten::val& jsPixels, const std::string& variant = "tonalSpot", double contrastLevel = 0.0, int maxColors = 128) {
    std::vector<Argb> pixels = ExtractPixelsFromJs(jsPixels);
    if (pixels.empty()) {
        return BuildThemePayload(Hct(kDefaultBaselineColor), variant, contrastLevel);
    }

    QuantizerResult quant_result = QuantizeCelebi(pixels, static_cast<uint16_t>(maxColors));
    std::vector<Argb> ranked = RankedSuggestions(quant_result.color_to_count);
    const Argb source = ranked.empty() ? kDefaultBaselineColor : ranked[0];

    return BuildThemePayload(Hct(source), variant, contrastLevel, ranked);
}

/**
 * Extracts and ranks candidate source colors from an image pixel buffer
 * using Celebi quantization and Material You scoring.
 *
 * @param jsPixels Image pixel array.
 * @param maxColors Maximum cluster count.
 * @return Array of hex color strings ordered by theme suitability.
 */
emscripten::val extractColors(const emscripten::val& jsPixels, int maxColors = 128) {
    std::vector<Argb> pixels = ExtractPixelsFromJs(jsPixels);
    emscripten::val result = emscripten::val::array();
    if (pixels.empty()) return result;

    QuantizerResult quant_result = QuantizeCelebi(pixels, static_cast<uint16_t>(maxColors));
    std::vector<Argb> ranked = RankedSuggestions(quant_result.color_to_count);

    for (size_t i = 0; i < ranked.size(); ++i) {
        result.set(i, ArgbToHex(ranked[i]));
    }
    return result;
}

/**
 * Generates the 13 standard tonal stops (0 to 100) for a given color.
 *
 * @param hexColor Source hex color string.
 * @return Object mapping tone values to hex color strings.
 */
emscripten::val getTonalPalette(const std::string& hexColor) {
    Hct hct(HexToArgb(hexColor));
    TonalPalette palette = TonalPalette(hct.get_hue(), hct.get_chroma());
    return PaletteToJsObject(palette);
}

/**
 * Harmonizes an arbitrary UI or status color with the source brand color
 * by shifting its hue in CAM16-UCS space while maintaining original chroma and tone.
 *
 * @param designColorHex Target color to harmonize (e.g. error, warning, success).
 * @param sourceColorHex Brand/theme seed color.
 * @return Harmonized hex color string.
 */
std::string harmonize(const std::string& designColorHex, const std::string& sourceColorHex) {
    Argb design = HexToArgb(designColorHex);
    Argb source = HexToArgb(sourceColorHex);
    Argb result = BlendHarmonize(design, source);
    return ArgbToHex(result);
}

EMSCRIPTEN_BINDINGS(material_color_utilities_wasm) {
    emscripten::function("themeFromSourceColor", &themeFromSourceColor);
    emscripten::function("themeFromImage", &themeFromImage);
    emscripten::function("extractColors", &extractColors);
    emscripten::function("getTonalPalette", &getTonalPalette);
    emscripten::function("harmonize", &harmonize);
}

}  // namespace material_color_utilities