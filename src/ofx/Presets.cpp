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
// The table. APPEND ONLY (the choice index is stored in projects): this order is final as of
// v1.0; new presets go at the end, existing ones are never reordered, renamed or removed.
// Designed by the look designer on real-world-like test scenes; each preset is documented in
// docs/LOOKS.md (intent, when to use, key settings) and shown in docs/validation/presets.png.
// Parameters not listed are reset to their defaults when the preset is applied.
// ------------------------------------------------------------------------------------------
const std::vector<LookPreset>& lookPresets() {
    static const std::vector<LookPreset> table = {
        {
            "Studio Classic",
            "The house look: balanced tungsten negative on the standard projection print - natural skin, dense mid-tones, fine Super 35 grain.",
            {
                {"negStock", "CN 200T Studio Tungsten"},
                {"process", "Print"},
                {"printStock", "P1 Standard Projection"},
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
            "High-speed tungsten negative pushed half a stop on the rich-contrast print, timed cool: deep blacks, warm practicals, moody night interiors.",
            {
                {"negStock", "CN 500T High Speed"},
                {"pushPull", "0.5"},
                {"process", "Print"},
                {"printStock", "P3 Rich Contrast"},
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
            "Soft daylight negative on the low-contrast print, printed light and warm: airy pastel colour, open shadows, gentle bloom.",
            {
                {"negStock", "CN 250D Soft Daylight"},
                {"interlayer", "1.0"},
                {"process", "Print"},
                {"printStock", "P2 Low Contrast"},
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
            "Vintage negative on a faded archive print: golden warmth, lifted brown blacks, 16 mm grain, gate weave and lens fall-off.",
            {
                {"negStock", "CN Vintage 85"},
                {"process", "Print"},
                {"printStock", "P4 Faded Archive"},
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
            "Pushed tungsten negative with partial bleach bypass on the silver-retention print: hard, metallic, desaturated contrast with crisp edges.",
            {
                {"negStock", "CN 200T Studio Tungsten"},
                {"pushPull", "0.5"},
                {"interlayer", "0.85"},
                {"negBleach", "0.15"},
                {"process", "Print"},
                {"printStock", "P5 Silver Retention"},
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
            "Fast no-backing tungsten negative for city nights: red-orange halation around neon and practicals, coarse but soft grain, cool timing.",
            {
                {"negStock", "CN 800T No-Backing"},
                {"process", "Print"},
                {"printStock", "P1 Standard Projection"},
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
            "Portrait colour negative through the lab scanner, timed warm: smooth golden skin, soft contrast, fine stills grain.",
            {
                {"negStock", "C41 Portrait 400"},
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
            "Fine-grain daylight negative with extra colour density on the standard print: clean, saturated, sharp - restrained halation and grain.",
            {
                {"negStock", "CN 50D Fine Daylight"},
                {"interlayer", "1.1"},
                {"process", "Print"},
                {"printStock", "P1 Standard Projection"},
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
            "Consumer colour negative scanned like a lab print, timed warm: punchy holiday colour, visible stills grain, light vignette.",
            {
                {"negStock", "C41 Consumer 200"},
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
            "Daylight reversal (slide) film projected directly: brilliant saturation, deep blacks, short latitude - expose carefully.",
            {
                {"negStock", "RV 100 Slide"},
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
            "Panchromatic black & white negative pushed one stop on the rich-contrast print: inky blacks, glowing highlights, strong vignette.",
            {
                {"negStock", "BW 400 Panchromatic"},
                {"pushPull", "1"},
                {"process", "Print"},
                {"printStock", "P3 Rich Contrast"},
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
                {"negStock", "RV 100 Slide"},
                {"pushPull", "1.5"},
                {"interlayer", "1.9"},   // v1.2: 1.6 -> 1.9 (keeps it >= 2 dE2000 from Slide Show Brilliance under the v1.2 hue hold)
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
            "A well-run 35 mm release print, timed warm: high-speed negative on the standard print with a few dark specks, the odd white "
            "negative speck, a persistent print scratch now and then, gentle gate weave and a rare moment of soft focus.",
            {
                {"negStock", "CN 500T High Speed"},
                {"process", "Print"},
                {"printStock", "P1 Standard Projection"},
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
            "A forgotten 16 mm reel: daylight negative on the faded print, its cyan dye gone warm-magenta, coarse grain, nervous weave, lots of dust and lint, "
            "flickering scratches, hairs in the gate and focus that drifts in and out.",
            {
                {"negStock", "CN 250D Soft Daylight"},
                {"process", "Print"},
                {"printStock", "P4 Faded Archive"},
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
            "The v1.2 default look as a preset: VISION3 250D on the modern print - dense lower mids, true blacks, soft "
            "ARRI-like highlight roll-off, calm skies and warm, low-chroma skin. Fine 35 mm grain.",
            {
                {"negStock", "Kodak Vision3 250D 5207"},
                {"process", "Print"},
                {"printStock", "P6 Modern Print"},
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
                {"printStock", "Fujifilm Eterna-CP 3513DI (v1.2 legacy curve)"},
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
    };
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
