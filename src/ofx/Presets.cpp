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
// The table. 1.4 (owner request): the table was rebuilt to one preset per real camera stock, each on its own
// negative + print pairing; Signature 250D / 2383 stays first and unchanged. Saved projects store the menu index,
// so a project saved before 1.4 may show another preset's name; its saved values (and so its render) are unchanged.
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
        // ---- 1.4 (owner request): one preset per real camera stock, each built on its own negative + print pairing
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
            "Studio Modern",
            "VISION3 200T on the high-D-max Fujifilm Eterna-CP 3521XD print - dense lower mids, true deep blacks, soft "
            "highlight roll-off, calm skies and warm, low-chroma skin. Fine 35 mm grain.",
            {
                {"negStock", "Kodak Vision3 200T 5213"},
                {"process", "Print"},
                {"printStock", "Fujifilm Eterna-CP 3521XD"},
                {"dyeSaturation", "0.75"},
                {"rolloffSoftness", "0.3"},
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
            "Classic release-print warmth: VISION2 500T printed on 2383 with Print Crossover - golden highlights, "
            "cool teal-leaning shadows, rich contrast and a livelier grain. The character of a photochemical 2383 chain.",
            {
                {"negStock", "Kodak Vision2 500T 5218"},
                {"process", "Print"},
                {"printStock", "Kodak Vision 2383"},
                {"printCrossover", "0.5"},
                {"toneShadowAmount", "0.2"},
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
            "Sunlit 35 mm on a soft, low-contrast scan: ETERNA Vivid 160 on a Soft-aim 2383 scanned on the telecine, "
            "full Print Crossover, yellow-green mids and golden highlights, open lifted blacks from a light Black "
            "Pro-Mist veil plus halation, a gentle film edge and a slow telecine float.",
            {
                {"negStock", "Fujifilm Eterna Vivid 160 8543"},
                {"process", "Print"},
                {"printStock", "Kodak Vision 2383"},
                {"printRender", "Telecine / Scan"},
                {"printAim", "Soft"},
                {"printCrossover", "1"},
                {"printerR", "0.5"},
                {"printerG", "-1"},
                {"printerB", "2"},
                {"filmContrast", "0.88"},
                {"rolloffSoftness", "0.4"},
                {"dyeSaturation", "0.9"},
                {"toneHighlightHue", "55"},
                {"toneHighlightAmount", "0.2"},
                {"bloomFilter", "Black Pro-Mist"},
                {"bloomFilterGrade", "0.125"},
                {"halationAmount", "0.7"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "0.75"},
                {"grainSoftness", "0.55"},
                {"integrate", "0.5"},
                {"sharpenRemoval", "0.75"},
                {"acutanceAmount", "0.15"},
                {"weaveStyle", "Telecine (Float)"},
                {"weaveAmount", "0.15"},
                {"vignette", "0.1"},
            },
        },
        {
            "Theatrical 2383 Warm",
            "A warm theatrical release: VISION2 Expression 500T through the IP / IN release chain onto 2383, the colour "
            "head dialled warm with exposure preserved, gentle golden highlight toning, projector weave and lens "
            "fall-off.",
            {
                {"negStock", "Kodak Vision2 Expression 500T 5229"},
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
            "Scope Night 500T / 2393",
            "VISION3 500T pushed half a stop on Premier 2393 in a 2.39 scope gate: inky printed blacks, teal shadows "
            "under warm practicals, wide red halation, a Black Magic glow, Super 35 grain and pin-registered weave.",
            {
                {"negStock", "Kodak Vision3 500T 5219"},
                {"pushPull", "0.5"},
                {"process", "Print"},
                {"printStock", "Kodak Vision Premier 2393"},
                {"printBlack", "-0.3"},
                {"headCyan", "-3"},
                {"toneShadowHue", "195"},
                {"toneShadowAmount", "0.35"},
                {"halationAmount", "1"},
                {"halationRadius", "28"},
                {"halationSaturation", "1.2"},
                {"bloomFilter", "Hollywood Black Magic"},
                {"bloomFilterGrade", "0.25"},
                {"bloomAmount", "0.25"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "1.1"},
                {"grainShadows", "1.3"},
                {"acutanceAmount", "0.3"},
                {"weaveStyle", "Camera (Pin-Registered)"},
                {"weaveAmount", "0.15"},
                {"vignette", "0.2"},
                {"gateEnable", "true"},
                {"gateAspect", "2.39 Scope"},
            },
        },
        {
            "Portra 400 Editorial",
            "Portra 400 through the lab scanner like a fashion editorial: luminous warm skin, open shadows, gentle "
            "contrast, a white Pro-Mist sheen on the highlights and fine 35 mm stills grain.",
            {
                {"negStock", "Kodak Portra 400"},
                {"process", "Lab Scan"},
                {"zoneShadows", "0.3"},
                {"filmContrast", "0.92"},
                {"rolloffSoftness", "0.5"},
                {"dyeSaturation", "0.9"},
                {"dyeRichness", "0.35"},
                {"halationAmount", "0.35"},
                {"bloomFilter", "Pro-Mist (white)"},
                {"bloomFilterGrade", "0.25"},
                {"bloomAmount", "0.2"},
                {"grainEnable", "true"},
                {"grainFormat", "35 mm Stills"},
                {"grainAmount", "0.8"},
                {"grainColour", "0.8"},
                {"acutanceAmount", "0.3"},
            },
        },
        {
            "Super 8 Ektachrome Diary",
            "Ektachrome 100D in the Super 8 gauge: sunny, saturated reversal colour, big lively grain, a rounded camera "
            "rebate, nervous projector weave, an old camera's exposure breathing and heavy mechanical vignetting.",
            {
                {"negStock", "Kodak Ektachrome 100D 5294"},
                {"dyeRichness", "0.4"},
                {"toneHighlightHue", "45"},
                {"toneHighlightAmount", "0.2"},
                {"halationAmount", "0.8"},
                {"halationRadius", "14"},
                {"bloomAmount", "0.25"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 8"},
                {"grainAmount", "1.1"},
                {"grainSoftness", "0.6"},
                {"integrate", "0.6"},
                {"acutanceAmount", "0.2"},
                {"weaveStyle", "Projector"},
                {"weaveAmount", "0.7"},
                {"weaveSpeed", "1.5"},
                {"vignetteMechanical", "0.45"},
                {"vignetteMechSoftness", "0.6"},
                {"gateEnable", "true"},
                {"gateAspect", "1.33 Academy"},
                {"gateCorner", "40"},
                {"gateSoftness", "6"},
                {"breathEnable", "true"},
                {"breathProfile", "Old Camera"},
            },
        },
        {
            "E100 Slide Projector",
            "Ektachrome E100 projected under an unadapted xenon lamp: crystal-clear slide colour, deep blacks, short "
            "latitude, fine stills grain and the soft light fall-off of a carousel projector - expose carefully.",
            {
                {"negStock", "Kodak Ektachrome E100"},
                {"projectionWhite", "Theatre Xenon (Unadapted)"},
                {"dyeSaturation", "1.1"},
                {"halationAmount", "0.2"},
                {"bloomAmount", "0.1"},
                {"grainEnable", "true"},
                {"grainFormat", "35 mm Stills"},
                {"grainAmount", "0.6"},
                {"grainSoftness", "0.4"},
                {"acutanceAmount", "0.4"},
                {"vignette", "0.3"},
            },
        },
        {
            "Kodachrome Travelogue",
            "A Kodachrome 64 travel reel in 16 mm: rich, dense reds and blues, a warm sunlit cast, crisp 16 mm grain, "
            "printer-gate Academy frame, projector weave and flicker.",
            {
                {"negStock", "Kodachrome 64 Style"},
                {"dyeRichness", "0.5"},
                {"toneHighlightHue", "40"},
                {"toneHighlightAmount", "0.15"},
                {"halationAmount", "0.4"},
                {"bloomAmount", "0.12"},
                {"grainEnable", "true"},
                {"grainFormat", "16 mm"},
                {"grainAmount", "0.9"},
                {"grainSoftness", "0.35"},
                {"acutanceAmount", "0.4"},
                {"weaveStyle", "Projector"},
                {"weaveAmount", "0.4"},
                {"vignetteMechanical", "0.25"},
                {"gateEnable", "true"},
                {"gateAspect", "1.33 Academy"},
                {"gateSource", "Printer Gate (Clear Print Border)"},
                {"breathEnable", "true"},
                {"breathProfile", "Projector"},
            },
        },
        {
            "Double-X Silver Print",
            "Double-X 5222 pushed a stop through the IP / IN release chain onto 2383: hard silver contrast, split toning "
            "(cool shadows, warm highlights), a Black Magic glow, coarse Super 35 grain, light dust and an Academy frame.",
            {
                {"negStock", "Kodak Double-X 5222"},
                {"pushPull", "1"},
                {"process", "Print"},
                {"printStock", "Kodak Vision 2383"},
                {"printRender", "Release Print (IP / IN)"},
                {"filmContrast", "1.1"},
                {"toneShadowHue", "215"},
                {"toneShadowAmount", "0.15"},
                {"toneHighlightHue", "40"},
                {"toneHighlightAmount", "0.15"},
                {"halationAmount", "0.3"},
                {"halationSaturation", "0"},
                {"bloomFilter", "Hollywood Black Magic"},
                {"bloomAmount", "0.2"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "1.3"},
                {"grainSoftness", "0.4"},
                {"acutanceAmount", "0.5"},
                {"weaveAmount", "0.3"},
                {"vignette", "0.25"},
                {"gateEnable", "true"},
                {"gateAspect", "1.33 Academy"},
                {"damageEnable", "true"},
                {"dustAmount", "0.3"},
                {"scratchAmount", "0.15"},
                {"hairAmount", "0.1"},
            },
        },
        {
            "Eterna 500 Skip Bleach",
            "ETERNA 500 with a partial bleach bypass on the 3521XD print, 1.85 flat: steely, desaturated colour with "
            "retained silver, dense blacks, a hard edge, Glimmerglass sparkle and gritty grain.",
            {
                {"negStock", "Fujifilm Eterna 500 8573"},
                {"process", "Print"},
                {"printStock", "Fujifilm Eterna-CP 3521XD"},
                {"printBleach", "0.45"},
                {"dyeSaturation", "0.85"},
                {"toneShadowHue", "205"},
                {"toneShadowAmount", "0.2"},
                {"halationAmount", "0.45"},
                {"bloomFilter", "Glimmerglass"},
                {"bloomFilterGrade", "0.25"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "1.2"},
                {"grainSoftness", "0.3"},
                {"acutanceAmount", "0.6"},
                {"weaveStyle", "Camera (Pin-Registered)"},
                {"weaveAmount", "0.1"},
                {"gateEnable", "true"},
                {"gateAspect", "1.85 Flat"},
            },
        },
        {
            "Reala Green Day",
            "Fujifilm Reala 500D on the gentle Eterna-CP 3513DI print, scanned on the telecine: the Fuji greens and "
            "clean skin, a minty shadow tone, airy highlights under a white Pro-Mist and soft Super 35 grain.",
            {
                {"negStock", "Fujifilm Reala 500D 8592"},
                {"process", "Print"},
                {"printStock", "Fujifilm Eterna-CP 3513DI"},
                {"printRender", "Telecine / Scan"},
                {"headMagenta", "-3"},
                {"printBlack", "-0.15"},
                {"toneShadowHue", "165"},
                {"toneShadowAmount", "0.2"},
                {"dyeSatBalance", "0.2"},
                {"halationAmount", "0.4"},
                {"bloomFilter", "Pro-Mist (white)"},
                {"bloomFilterGrade", "0.125"},
                {"bloomAmount", "0.18"},
                {"grainEnable", "true"},
                {"grainFormat", "Super 35"},
                {"grainAmount", "0.8"},
                {"grainSoftness", "0.6"},
                {"acutanceAmount", "0.25"},
                {"vignette", "0.1"},
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
