// Reafilm Pro - look preset table and resolver. See Presets.h for the format and rules.
#include "Presets.h"
#include "Params.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <set>

namespace cel { namespace ofxp {

// ------------------------------------------------------------------------------------------
// The table. New presets go at the end, existing ones are never renamed or removed. The one exception
// (owner request, 1.3): Signature 250D / 2383 was inserted first, so a project saved before 1.3 shows the
// next preset's name in the menu; its saved values (and so its render) are unchanged.
// Designed by the look designer on real-world-like test scenes; each preset is documented in
// docs/LOOKS.md (intent, when to use, key settings) and shown in docs/validation/presets.png.
// Parameters not listed are reset to their defaults when the preset is applied.
// ------------------------------------------------------------------------------------------
namespace {
// 1.3: the film edge of the Signature preset (digital sharpening removed, no integration blur, a fine
// acutance edge, a soft Aura around the halation) is shared by every preset that does not set its own.
std::vector<LookPreset> withTexture(std::vector<LookPreset> table) {
    const std::pair<const char*, const char*> edge[] = {
        {"integrate", "0"}, {"sharpenRemoval", "1.5"}, {"acutanceRadius", "0.5"},
        {"auraEnable", "true"}, {"auraRadius", "50"}, {"auraStrength", "0.15"}};
    for (LookPreset& p : table)
        for (const auto& e : edge) {
            bool has = false;
            for (const PresetValue& v : p.values) has = has || v.param == e.first;
            if (!has) p.values.push_back({e.first, e.second});
        }
    return table;
}
} // namespace

const std::vector<LookPreset>& lookPresets() {
    static const std::vector<LookPreset> table = withTexture({
        {   // 1.3: the owner's own setup (X-H2S F-Log2 C), first in the menu
            "Signature 250D / 2383",
            "VISION3 250D on the Kodak 2383 release print with ALEXA colour and a gentle film roll-off: "
            "warm colour head, steel-blue shadow toning, print crossover. Fine, crisp 65 mm grain on a film edge with the "
            "digital sharpening removed, a tight halation with a soft Aura and a Glimmerglass bloom - texture and depth.",
            {
                {"grainFormat", "65 mm"},
                {"alexaColourEnable", "true"},
                {"alexaColour", "0.5"},
                {"filmContrast", "1.047"},
                {"rolloffStart", "1.5"},
                {"rolloffSoftness", "1"},
                {"dyeSatEnable", "false"},
                {"negStock", "Kodak Vision3 250D 5207"},
                {"process", "Print"},
                {"printStock", "Kodak Vision 2383"},
                {"printCrossover", "0.4"},
                {"printRender", "Release Print (IP / IN)"},
                {"headCyan", "2.5"},
                {"headYellow", "-3"},
                {"toneShadowAmount", "0.5"},
                {"halationAmount", "0.76"},
                {"halationRadius", "2"},
                {"auraEnable", "true"},
                {"auraRadius", "50"},
                {"auraStrength", "0.15"},
                {"bloomRadius", "20"},
                {"bloomFilter", "Glimmerglass"},
                {"bloomFilterGrade", "0.125"},
                {"grainEnable", "true"},
                {"grainSize", "0.5"},
                {"grainSoftness", "0"},
                {"integrate", "0"},
                {"sharpenRemoval", "1.5"},
                {"acutanceRadius", "0.5"},
                {"weaveAmount", "0.25"},
                {"vignette", "0.15"},
            },
        },
        {
            "Studio Classic",
            "The house look: VISION3 200T on the Kodak 2383 release print - natural skin, dense mid-tones, fine Super 35 grain.",
            {
                {"negStock", "Kodak Vision3 200T 5213"},
                {"process", "Print"},
                {"printStock", "Kodak Vision 2383"},
                {"halationAmount", "0.5"},
                {"bloomAmount", "0.15"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "0.9"},
                {"acutanceAmount", "0.3"},
            },
        },
        {
            "Tungsten Night Drama",
            "VISION2 500T pushed half a stop on the high-contrast Eterna-CP 3521XD print, timed cool: deep blacks, warm practicals, moody night interiors.",
            {
                {"negStock", "Kodak Vision2 500T 5218"},
                {"pushPull", "0.5"},
                {"process", "Print"},
                {"printStock", "Fujifilm Eterna-CP 3521XD"},
                {"printerR", "0.5"},
                {"printerB", "-1"},
                {"printExposure", "0.5"},
                {"halationAmount", "0.8"},
                {"halationRadius", "26"},
                {"bloomAmount", "0.25"},
                {"bloomRadius", "50"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "1"},
                {"grainSoftness", "0.55"},
                {"acutanceAmount", "0.3"},
                {"vignette", "0.2"},
            },
        },
        {
            "Soft Daylight Pastel",
            "Portra 400 on the gentle Eterna-CP 3513DI print, printed light and warm: airy pastel colour, open shadows, gentle bloom.",
            {
                {"negStock", "Kodak Portra 400"},   // real film only: Eterna 250D sat too close to Soft Cinema
                {"interlayer", "1.0"},
                {"process", "Print"},
                {"printStock", "Fujifilm Eterna-CP 3513DI"},
                {"printerR", "-0.5"},
                {"printerG", "0.5"},
                {"printerB", "1"},
                {"printExposure", "0.5"},
                {"halationAmount", "0.35"},
                {"bloomAmount", "0.25"},
                {"bloomRadius", "70"},
                {"grainEnable", "true"},
                {"grainFormat", "35 mm Stills"},
                {"grainAmount", "0.8"},
                {"grainSoftness", "0.7"},
                {"acutanceAmount", "0.15"},
            },
        },
        {
            "Archive 1978",
            "VISION2 500T on a faded 2383 print: golden warmth, lifted brown blacks, 16 mm grain, gate weave and lens fall-off.",
            {
                {"negStock", "Kodak Vision2 500T 5218"},
                {"process", "Print"},
                {"printStock", "Kodak Vision 2383"},
                {"printBlack", "-0.5"},   // real film only: the faded print is 2383 with lifted blacks,
                {"printWhite", "0.05"},   // greyed whites, warm-brown shadow toning and tired dyes
                {"toneShadowAmount", "0.4"},
                {"toneShadowHue", "30"},
                {"dyeSaturation", "0.8"},
                {"printerR", "-1"},
                {"printerB", "1.5"},
                {"printExposure", "-1"},
                {"halationAmount", "0.7"},
                {"halationRadius", "28"},
                {"halationHue", "0.2"},
                {"bloomAmount", "0.3"},
                {"bloomRadius", "60"},
                {"grainEnable", "true"},
                {"grainFormat", "16 mm"},
                {"grainAmount", "0.8"},
                {"grainSoftness", "0.75"},
                {"acutanceAmount", "0.1"},
                {"weaveAmount", "0.4"},
                {"weaveSpeed", "0.8"},
                {"vignette", "0.3"},
            },
        },
        {
            "Silver Retention Thriller",
            "VISION3 200T pushed, with partial bleach bypass on the negative and on the 2383 print: hard, metallic, desaturated contrast with crisp edges.",
            {
                {"negStock", "Kodak Vision3 200T 5213"},
                {"pushPull", "0.5"},
                {"interlayer", "0.85"},
                {"negBleach", "0.15"},
                {"process", "Print"},
                {"printStock", "Kodak Vision 2383"},
                {"printBleach", "0.6"},   // real film only: silver retention as a bleach-bypassed 2383
                {"printerR", "0.5"},
                {"printerB", "-0.5"},
                {"printExposure", "0.5"},
                {"halationAmount", "0.35"},
                {"bloomAmount", "0.1"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "1.15"},
                {"grainSoftness", "0.4"},
                {"acutanceAmount", "0.6"},
                {"acutanceRadius", "1.6"},
                {"vignette", "0.15"},
            },
        },
        {
            "Neon Nocturne",
            "VISION2 500T for city nights: red-orange halation around neon and practicals, coarse but soft grain, cool timing.",
            {
                {"negStock", "Kodak Vision2 500T 5218"},
                {"process", "Print"},
                {"printStock", "Kodak Vision 2383"},
                {"printerB", "-0.5"},
                {"printExposure", "-0.5"},
                {"halationAmount", "0.6"},
                {"halationRadius", "24"},
                {"halationHue", "-0.2"},
                {"bloomAmount", "0.3"},
                {"bloomRadius", "45"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "0.85"},
                {"grainSoftness", "0.55"},
                {"acutanceAmount", "0.25"},
            },
        },
        {
            "Golden Hour Portrait",
            "Portra 400 through the lab scanner, timed warm: smooth golden skin, soft contrast, fine stills grain.",
            {
                {"negStock", "Kodak Portra 400"},
                {"process", "Lab Scan"},
                {"printerR", "-0.5"},
                {"printerB", "1"},
                {"halationAmount", "0.45"},
                {"halationHue", "0.3"},
                {"bloomAmount", "0.3"},
                {"bloomRadius", "60"},
                {"grainEnable", "true"},
                {"grainFormat", "35 mm Stills"},
                {"grainAmount", "0.9"},
                {"grainSoftness", "0.6"},
                {"acutanceAmount", "0.2"},
            },
        },
        {
            "Crisp Commercial",
            "VISION3 50D with extra colour density on the 2383 print: clean, saturated, sharp - restrained halation and grain.",
            {
                {"negStock", "Kodak Vision3 50D 5203"},
                {"interlayer", "1.1"},
                {"process", "Print"},
                {"printStock", "Kodak Vision 2383"},
                {"halationAmount", "0.25"},
                {"bloomAmount", "0.1"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "0.6"},
                {"grainSoftness", "0.4"},
                {"acutanceAmount", "0.45"},
                {"acutanceRadius", "1.5"},
            },
        },
        {
            "Summer Snapshot",
            "Reala 500D scanned like a lab print, timed warm: punchy holiday colour, visible stills grain, light vignette.",
            {
                {"negStock", "Fujifilm Reala 500D 8592"},   // real film only (was the designed C41 Consumer 200)
                {"process", "Lab Scan"},
                {"printerR", "-0.5"},
                {"printerB", "0.5"},
                {"halationAmount", "0.45"},
                {"bloomAmount", "0.25"},
                {"grainEnable", "true"},
                {"grainFormat", "35 mm Stills"},
                {"grainAmount", "1.1"},
                {"grainSoftness", "0.5"},
                {"acutanceAmount", "0.3"},
                {"vignette", "0.25"},
            },
        },
        {
            "Slide Show Brilliance",
            "Ektachrome E100 reversal (slide) film projected directly: brilliant saturation, deep blacks, short latitude - expose carefully.",
            {
                {"negStock", "Kodak Ektachrome E100"},
                {"halationAmount", "0.3"},
                {"bloomAmount", "0.15"},
                {"grainEnable", "true"},
                {"grainFormat", "35 mm Stills"},
                {"grainAmount", "1.2"},
                {"grainSoftness", "0.45"},
                {"acutanceAmount", "0.4"},
                {"vignette", "0.3"},
            },
        },
        {
            "Monochrome Noir",
            "Double-X 5222 pushed one stop on the Premier 2393 print: inky blacks, glowing highlights, strong vignette.",
            {
                {"negStock", "Kodak Double-X 5222"},
                {"pushPull", "1"},
                {"process", "Print"},
                {"printStock", "Kodak Vision Premier 2393"},
                {"halationAmount", "0.45"},
                {"bloomAmount", "0.3"},
                {"bloomRadius", "55"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "1.1"},
                {"grainSoftness", "0.5"},
                {"acutanceAmount", "0.45"},
                {"vignette", "0.35"},
            },
        },
        {
            "Home Movie Super 8",
            "Reversal film in the Super 8 gauge: bold colour, big soft grain, warm halation, nervous gate weave and heavy vignetting.",
            {
                {"negStock", "Kodak Ektachrome 100D 5294"},
                {"pushPull", "1.0"},
                {"interlayer", "1.6"},   // real film only (E100): push 1.5 -> 1, 1.9 -> 1.6 keeps the curve monotonic
                {"halationAmount", "0.8"},
                {"halationRadius", "30"},
                {"halationHue", "0.3"},
                {"bloomAmount", "0.45"},
                {"bloomRadius", "70"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 8"},
                {"grainAmount", "1.6"},
                {"grainSoftness", "0.6"},
                {"acutanceAmount", "-0.15"},
                {"weaveAmount", "1.1"},
                {"weaveSpeed", "1.4"},
                {"vignette", "0.45"},
            },
        },
        // ---- v1.1 (appended): film damage looks ----
        {
            "Worn Release Print",
            "A well-run 35 mm release print, timed warm: Eterna Vivid 160 on the 2383 print with a few dark specks, the odd white "
            "negative speck, a persistent print scratch now and then, gentle gate weave and a rare moment of soft focus.",
            {
                {"negStock", "Fujifilm Eterna Vivid 160 8543"},
                {"process", "Print"},
                {"printStock", "Kodak Vision 2383"},
                {"printerR", "1"},
                {"printerG", "0.5"},
                {"printerB", "-0.5"},
                {"printExposure", "0.5"},
                {"halationAmount", "0.6"},
                {"bloomAmount", "0.2"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "1"},
                {"acutanceAmount", "0.25"},
                {"weaveAmount", "0.6"},
                {"vignette", "0.2"},
                {"damageEnable", "true"},
                {"dustAmount", "0.7"},
                {"dustNegative", "0.3"},
                {"scratchAmount", "0.5"},
                {"scratchPersistence", "0.75"},
                {"scratchEmulsion", "0.35"},
                {"scratchNegative", "0.2"},
                {"hairAmount", "0.35"},
                {"filmBlurEnable", "true"},
                {"filmBlurRadius", "2.5"},
                {"filmBlurFrequency", "0.12"},
                {"filmBlurDuration", "30"},
                {"filmBlurBreathing", "0.15"},
                {"filmBlurChromatic", "0.3"},
            },
        },
        {
            "Found Footage 16",
            "A forgotten 16 mm reel: Eterna 250D on a faded 2383 print, its cyan dye gone warm-magenta, coarse grain, nervous weave, lots of dust and lint, "
            "flickering scratches, hairs in the gate and focus that drifts in and out.",
            {
                {"negStock", "Fujifilm Eterna 250D 8563"},
                {"process", "Print"},
                {"printStock", "Kodak Vision 2383"},
                {"printBlack", "-0.6"},   // real film only: the faded print is 2383 with lifted blacks,
                {"printWhite", "0.05"},   // greyed whites, magenta-brown shadow toning and tired dyes
                {"toneShadowAmount", "0.5"},
                {"toneShadowHue", "330"},
                {"dyeSaturation", "0.75"},
                {"printerR", "-1.5"},
                {"printerG", "1"},
                {"printExposure", "-1.5"},
                {"halationAmount", "0.8"},
                {"halationRadius", "30"},
                {"halationHue", "0.25"},
                {"bloomAmount", "0.35"},
                {"bloomRadius", "60"},
                {"grainEnable", "true"},
                {"grainFormat", "16 mm"},
                {"grainAmount", "1.0"},
                {"grainSoftness", "0.7"},
                {"acutanceAmount", "0.0"},
                {"weaveAmount", "1.0"},
                {"weaveSpeed", "1.2"},
                {"vignette", "0.4"},
                {"damageEnable", "true"},
                {"dustAmount", "1.4"},
                {"dustSize", "1.2"},
                {"dustNegative", "0.4"},
                {"scratchAmount", "1.2"},
                {"scratchPersistence", "0.4"},
                {"scratchEmulsion", "0.5"},
                {"scratchNegative", "0.3"},
                {"hairAmount", "0.9"},
                {"filmBlurEnable", "true"},
                {"filmBlurRadius", "4"},
                {"filmBlurFrequency", "0.3"},
                {"filmBlurDuration", "36"},
                {"filmBlurBreathing", "0.35"},
                {"filmBlurChromatic", "0.5"},
            },
        },
        // ---- v1.2 (look-dev item C4): the owner's three liked families on the data-sheet stocks
        {
            "Studio Modern",
            "VISION3 250D on the high-D-max Fujifilm Eterna-CP 3521XD print - dense lower mids, true deep blacks, soft "
            "highlight roll-off, calm skies and warm, low-chroma skin. Fine 35 mm grain.",
            {
                {"negStock", "Kodak Vision3 250D 5207"},
                {"process", "Print"},
                {"printStock", "Fujifilm Eterna-CP 3521XD"},
                {"dyeSaturation", "0.75"},   // real film only: calm colour (P6 is gone; keeps it apart from Warm Print)
                {"halationAmount", "0.4"},
                {"bloomAmount", "0.12"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "0.7"},
                {"grainSoftness", "0.55"},
                {"acutanceAmount", "0.25"},
            },
        },
        {
            "Soft Cinema",
            "ETERNA-style softness: low-contrast daylight negative on the matching Eterna-CP print - gentle "
            "contrast, restrained colour, open shadows and a quiet, slightly cool palette.",
            {
                {"negStock", "Fujifilm Eterna 250D 8563"},
                {"process", "Print"},
                {"printStock", "Fujifilm Eterna-CP 3513DI"},
                {"halationAmount", "0.3"},
                {"bloomAmount", "0.2"},
                {"bloomRadius", "60"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "0.6"},
                {"grainSoftness", "0.65"},
                {"acutanceAmount", "0.15"},
            },
        },
        {
            "Warm Print",
            "Classic release-print warmth: VISION3 250D printed on 2383 with Print Crossover - golden highlights, "
            "cool teal-leaning shadows, rich contrast. The character of a photochemical 2383 chain.",
            {
                {"negStock", "Kodak Vision3 250D 5207"},
                {"process", "Print"},
                {"printStock", "Kodak Vision 2383"},
                {"printCrossover", "0.5"},
                {"halationAmount", "0.55"},
                {"bloomAmount", "0.18"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "0.9"},
                {"grainSoftness", "0.5"},
                {"acutanceAmount", "0.3"},
            },
        },
        {
            "Sunlit 35mm (soft scan)",
            "Sunlit 35 mm on a soft, low-contrast scan: a Soft-aim print with full Print Crossover, yellow-green "
            "mids, open lifted blacks from a light Black Pro-Mist veil plus halation, and a gentle film edge "
            "(digital sharpening removed, weak shadow acutance) that stays soft on dark scenes.",
            {
                {"printAim", "Soft"},
                {"printCrossover", "1"},
                {"printerR", "0.5"},
                {"printerG", "-1"},
                {"printerB", "2.5"},
                {"filmContrast", "0.85"},
                {"rolloffSoftness", "0.4"},
                {"dyeSaturation", "0.9"},
                {"bloomFilter", "Black Pro-Mist"},
                {"bloomFilterGrade", "0.125"},
                {"halationAmount", "0.7"},
                {"integrate", "0.8"},
                {"sharpenRemoval", "0.75"},
                {"acutanceAmount", "0.15"},
                {"grainAmount", "0.7"},
            },
        },
        // ---- Lab & Gate looks (appended): colour timer, film gate, weave styles, mechanical vignette
        {
            "Theatrical 2383 Warm",
            "A warm theatrical release: VISION3 250D through the IP / IN release chain onto 2383, the colour head "
            "dialled warm with exposure preserved, gentle golden highlight toning, projector weave and lens "
            "fall-off.",
            {
                {"negStock", "Kodak Vision3 250D 5207"},
                {"process", "Print"},
                {"printStock", "Kodak Vision 2383"},
                {"printRender", "Release Print (IP / IN)"},
                {"printCrossover", "0.4"},
                {"headCyan", "5"},
                {"headYellow", "-6"},
                {"toneHighlightHue", "50"},
                {"toneHighlightAmount", "0.35"},
                {"halationAmount", "0.55"},
                {"bloomAmount", "0.15"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "0.9"},
                {"grainSoftness", "0.5"},
                {"acutanceAmount", "0.3"},
                {"weaveAmount", "0.25"},
                {"vignette", "0.15"},
            },
        },
        {
            "Soft Telecine Scan",
            "A flat, gentle telecine transfer: VISION3 250D on the telecine render, lifted printed blacks and a "
            "held-back white, a light white Pro-Mist, softened film edge and the slow float of a capstan "
            "transport.",
            {
                {"negStock", "Kodak Vision3 250D 5207"},
                {"process", "Print"},
                {"printStock", "Kodak Vision 2383"},
                {"printRender", "Telecine / Scan"},
                {"printAim", "Soft"},
                {"printBlack", "-0.7"},
                {"printWhite", "0.06"},
                {"headMagenta", "-2"},
                {"headYellow", "5"},
                {"bloomFilter", "Pro-Mist (white)"},
                {"bloomFilterGrade", "0.125"},
                {"halationAmount", "0.4"},
                {"integrate", "0.7"},
                {"sharpenRemoval", "0.5"},
                {"acutanceAmount", "0.1"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "0.6"},
                {"grainSoftness", "0.7"},
                {"weaveStyle", "Telecine (Float)"},
                {"weaveAmount", "0.35"},
            },
        },
        {
            "500T Night Exterior",
            "VISION3 500T pushed half a stop for night exteriors on Premier 2393: deep printed blacks, teal-toned "
            "shadows under warm practicals, strong halation, Super 35 grain and pin-registered camera weave.",
            {
                {"negStock", "Kodak Vision3 500T 5219"},
                {"pushPull", "0.5"},
                {"process", "Print"},
                {"printStock", "Kodak Vision Premier 2393"},
                {"printBlack", "0.15"},
                {"headYellow", "4"},
                {"toneShadowHue", "200"},
                {"toneShadowAmount", "0.6"},
                {"toneHighlightHue", "40"},
                {"toneHighlightAmount", "0.3"},
                {"halationAmount", "0.9"},
                {"halationRadius", "26"},
                {"bloomAmount", "0.25"},
                {"bloomRadius", "50"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "1.1"},
                {"grainSoftness", "0.55"},
                {"acutanceAmount", "0.3"},
                {"weaveStyle", "Camera (Pin-Registered)"},
                {"weaveAmount", "0.5"},
                {"vignette", "0.25"},
            },
        },
        {
            "Scope Bleach Bypass",
            "A 2.39 scope frame through the camera gate with a partial bleach bypass on 2383: retained silver, "
            "steely desaturated contrast, cool-toned shadows and a hard, crisp edge.",
            {
                {"negStock", "Kodak Vision3 200T 5213"},
                {"process", "Print"},
                {"printStock", "Kodak Vision 2383"},
                {"printBleach", "0.9"},
                {"negBleach", "0.3"},
                {"dyeSaturation", "0.8"},
                {"printWhite", "0.05"},
                {"toneShadowHue", "220"},
                {"toneShadowAmount", "0.3"},
                {"halationAmount", "0.35"},
                {"bloomAmount", "0.1"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "1"},
                {"acutanceAmount", "0.5"},
                {"gateEnable", "true"},
                {"gateAspect", "2.39 Scope"},
                {"gateOverscan", "0.02"},
            },
        },
        {
            "16mm Documentary",
            "Handheld Super 16 for documentary work: Eterna 500 on the Kodak 2383 print through the Super 16 camera "
            "gate, big lively 16 mm grain, a touch of camera weave and a lens with real mechanical vignetting.",
            {
                {"negStock", "Fujifilm Eterna 500 8573"},   // real film only: Eterna 250D sat too close to Soft Cinema
                {"process", "Print"},
                {"printStock", "Kodak Vision 2383"},
                {"headCyan", "2"},
                {"printBlack", "-0.3"},
                {"halationAmount", "0.6"},
                {"bloomAmount", "0.2"},
                {"grainEnable", "true"},
                {"grainFormat", "16 mm"},
                {"grainAmount", "1.1"},
                {"grainSoftness", "0.6"},
                {"acutanceAmount", "0.4"},
                {"weaveStyle", "Camera (Pin-Registered)"},
                {"weaveAmount", "0.7"},
                {"vignette", "0.2"},
                {"vignetteMechanical", "0.5"},
                {"vignetteMechSize", "1.05"},
                {"gateEnable", "true"},
                {"gateAspect", "Super 16 (1.66)"},
                {"gateOverscan", "0.015"},
            },
        },
        {
            "Faded Magenta Print",
            "An old release print gone magenta: lifted printed blacks, a softened white, the cyan dye faded "
            "towards magenta-red in highlight and shadow, printer-gate clear border, projector weave and soft 35 "
            "mm grain.",
            {
                {"negStock", "Kodak Vision2 500T 5218"},
                {"process", "Print"},
                {"printStock", "Kodak Vision 2383"},
                {"printBlack", "-0.7"},
                {"printWhite", "0.08"},
                {"printDefog", "0"},
                {"headMagenta", "-5"},
                {"headCyan", "3"},
                {"toneHighlightHue", "340"},
                {"toneHighlightAmount", "0.6"},
                {"toneShadowHue", "320"},
                {"toneShadowAmount", "0.4"},
                {"dyeSaturation", "0.8"},
                {"halationAmount", "0.6"},
                {"bloomAmount", "0.25"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "1"},
                {"grainSoftness", "0.7"},
                {"weaveAmount", "0.5"},
                {"vignette", "0.3"},
            },
        },
        {
            "Sunlit 35mm Full Gate",
            "The Sunlit 35mm look printed full gate and timed warmer: a warm colour head, lifted printed blacks "
            "and golden highlights, framed by the camera aperture's rounded rebate with a little overscan, a slow telecine float and the faint cos^4 fall-off of the "
            "taking lens.",
            {
                {"printAim", "Soft"},
                {"printCrossover", "1"},
                {"printerR", "0.5"},
                {"printerG", "-1"},
                {"printerB", "2.5"},
                {"filmContrast", "0.85"},
                {"rolloffSoftness", "0.4"},
                {"dyeSaturation", "0.9"},
                {"bloomFilter", "Black Pro-Mist"},
                {"bloomFilterGrade", "0.125"},
                {"halationAmount", "0.7"},
                {"integrate", "0.8"},
                {"sharpenRemoval", "0.75"},
                {"acutanceAmount", "0.15"},
                {"grainAmount", "0.7"},
                {"headCyan", "3"},
                {"headYellow", "-5"},
                {"printBlack", "-0.4"},
                {"toneHighlightHue", "60"},
                {"toneHighlightAmount", "0.3"},
                {"weaveStyle", "Telecine (Float)"},
                {"weaveAmount", "0.3"},
                {"vignette", "0.15"},
                {"gateEnable", "true"},
                {"gateOverscan", "0.025"},
                {"gateCorner", "18"},
            },
        },
    });
    return table;
}

// ------------------------------------------------------------------------------------------

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool parseDouble(const std::string& s, double* out) {
    if (s.empty()) return false;
    const char* b = s.c_str();
    char* e = nullptr;
    errno = 0;
    double v = std::strtod(b, &e);
    if (e == b || errno == ERANGE || !std::isfinite(v)) return false;
    while (*e == ' ') ++e;
    if (*e != '\0') return false;
    *out = v;
    return true;
}

bool parseBool(const std::string& s, double* out) {
    const std::string l = lower(s);
    if (l == "true" || l == "on" || l == "yes" || l == "1") { *out = 1.0; return true; }
    if (l == "false" || l == "off" || l == "no" || l == "0") { *out = 0.0; return true; }
    return false;
}

// Parses one preset value for a parameter. Returns false + reason on failure; out-of-range
// numbers are clamped (and reported).
bool parseValue(const ParamDef& d, const std::string& text, double* out, std::string* why) {
    switch (d.type) {
    case PType::Double:
    case PType::Int: {
        double v;
        if (!parseDouble(text, &v)) { *why = "not a number: \"" + text + "\""; return false; }
        if (d.type == PType::Int && std::floor(v) != v) { *why = "not an integer: \"" + text + "\""; return false; }
        if (v < d.min || v > d.max) {
            *why = "value " + text + " outside range, clamped";
            v = std::min(std::max(v, d.min), d.max);
            *out = v;
            return true;   // usable, but reported
        }
        *out = v;
        return true;
    }
    case PType::Bool:
        if (!parseBool(text, out)) { *why = "not a boolean: \"" + text + "\""; return false; }
        return true;
    case PType::Choice: {
        int idx = choiceIndex(d, text);
        if (idx < 0) { *why = "unknown option \"" + text + "\""; return false; }
        *out = double(idx);
        return true;
    }
    default:
        *why = "parameter type cannot be part of a preset";
        return false;
    }
}

} // namespace

std::vector<ResolvedValue> resolvePreset(size_t index, std::vector<std::string>* problems) {
    std::vector<ResolvedValue> out;
    const auto& presets = lookPresets();
    if (index >= presets.size()) return out;
    const LookPreset& p = presets[index];

    for (const ParamDef& d : paramDefs()) {
        if (d.look) out.push_back({d.id, numericDefault(d)});
    }
    for (const PresetValue& pv : p.values) {
        const ParamDef* d = findParam(pv.param);
        if (!d) {
            if (problems) problems->push_back(p.name + ": unknown parameter \"" + pv.param + "\"");
            continue;
        }
        if (!d->look) {
            if (problems) problems->push_back(p.name + ": parameter \"" + pv.param + "\" is not a look parameter");
            continue;
        }
        double v = 0.0;
        std::string why;
        bool ok = parseValue(*d, pv.value, &v, &why);
        if (!why.empty() && problems) problems->push_back(p.name + ": " + pv.param + ": " + why);
        if (!ok) continue;
        for (ResolvedValue& rv : out) {
            if (rv.param == d->id) rv.value = v;
        }
    }
    return out;
}

bool validatePresets(std::vector<std::string>* problems) {
    std::vector<std::string> local;
    std::vector<std::string>& pr = problems ? *problems : local;
    const size_t before = pr.size();
    std::set<std::string> names;
    const auto& presets = lookPresets();
    for (size_t i = 0; i < presets.size(); ++i) {
        const LookPreset& p = presets[i];
        if (p.name.empty()) pr.push_back("preset #" + std::to_string(i) + " has no name");
        if (lower(p.name) == "custom") pr.push_back("preset name \"Custom\" is reserved");
        if (!names.insert(lower(p.name)).second) pr.push_back("duplicate preset name \"" + p.name + "\"");
        std::set<std::string> seen;
        for (const PresetValue& pv : p.values) {
            if (!seen.insert(pv.param).second) pr.push_back(p.name + ": parameter \"" + pv.param + "\" listed twice");
        }
        resolvePreset(i, &pr);
    }
    return pr.size() == before;
}

}} // namespace cel::ofxp
