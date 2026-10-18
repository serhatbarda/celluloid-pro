// Reafilm Pro - self test of the OFX parameter registry and the look-preset table.
// Checks the registry against SPEC §7 (ids, groups, ranges, defaults, hints), the choice lists
// against the core enumerations, every preset against the registry, and the path helpers.
#include "Params.h"
#include "Platform.h"
#include "Presets.h"
#include "UserPresets.h"
#include "../core/Core.h"
#include "../texture/Texture.h"
#include "../texture/TextureDesign.h"   // kBloomFilterCount

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace cel;
using namespace cel::ofxp;

static int g_fail = 0;
static int g_checks = 0;

static void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

// v1.3 P1: set / clear an environment variable for the user preset folder tests
static void setEnv(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    if (value.empty()) unsetenv(name);
    else setenv(name, value.c_str(), 1);
#endif
}

struct Spec {
    const char* id;
    const char* group;
    double mn, mx, def;
};

int main() {
    const std::vector<ParamDef>& defs = paramDefs();

    // ---- structure ----
    std::set<std::string> ids, groupsSeen;
    for (const ParamDef& d : defs) {
        check(!d.id.empty(), "param with empty id");
        check(ids.insert(d.id).second, "duplicate param id " + d.id);
        check(!d.label.empty(), d.id + ": empty label");
        if (d.type == PType::Group) {
            groupsSeen.insert(d.id);
            check(d.group.empty(), d.id + ": nested groups are not used");
        } else {
            check(!d.group.empty(), d.id + ": parameter outside a group");
            check(groupsSeen.count(d.group) == 1, d.id + ": group " + d.group + " not defined before it");
            check(!d.hint.empty(), d.id + ": missing hint");
            if (d.type == PType::Double || d.type == PType::Int) {
                check(d.min < d.max, d.id + ": empty range");
                check(d.def >= d.min && d.def <= d.max, d.id + ": default outside range");
            }
            if (d.type == PType::Choice) {
                check(!d.options.empty(), d.id + ": choice without options");
                check(d.def >= 0 && d.def < double(d.options.size()), d.id + ": default option out of range");
                std::set<std::string> opts(d.options.begin(), d.options.end());
                check(opts.size() == d.options.size(), d.id + ": duplicate option labels");
            }
        }
    }

    // ---- SPEC §7 table: group, range, default ----
    const Spec spec[] = {
        {"exposure", "groupInput", -4, 4, 0},
        {"cameraEmulationStrength", "groupCameraEmulation", 0, 1, 1},   // v1.2
        {"pushPull", "groupNegative", -2, 3, 0},
        {"interlayer", "groupNegative", 0, 2, 1},
        {"negBleach", "groupNegative", 0, 1, 0},
        {"neutralize", "groupNegative", 0, 1, 0},
        {"filmAge", "groupNegative", 0, 1, 0},
        {"printerR", "groupPrint", -12, 12, 0},
        {"printerG", "groupPrint", -12, 12, 0},
        {"printerB", "groupPrint", -12, 12, 0},
        {"printExposure", "groupPrint", -12, 12, 0},
        {"printBleach", "groupPrint", 0, 1, 0},
        {"printCrossover", "groupPrint", -1, 1, 0},   // v1.2
        {"printContrast", "groupPrint", -1, 1, 0},    // v1.3
        {"halationAmount", "groupHalation", 0, 2, 0.5},
        {"halationRadius", "groupHalation", 2, 80, 20},
        {"halationThreshold", "groupHalation", 0, 6, 0},   // v1.2: 0 = physical (threshold-free)
        {"halationHue", "groupHalation", -1, 1, 0},
        {"bloomAmount", "groupBloom", 0, 2, 0.15},
        {"bloomRadius", "groupBloom", 5, 200, 40},
        {"grainAmount", "groupGrain", 0, 3, 1},
        {"grainSize", "groupGrain", 0.25, 3, 1},
        {"grainSoftness", "groupGrain", 0, 1, 0.5},
        {"grainSeed", "groupGrain", 0, 9999, 0},
        {"acutanceAmount", "groupLensGate", -1, 2, 0.35},   // v1.2
        {"acutanceRadius", "groupLensGate", 0.5, 10, 2},
        {"integrate", "groupFilmResolution", 0, 1, 0.5},   // v1.2 film resolution
        {"weaveAmount", "groupLensGate", 0, 2, 0},
        {"weaveSpeed", "groupLensGate", 0.25, 4, 1},
        {"vignette", "groupLensGate", 0, 1, 0},
        {"hdrPeakNits", "groupOutput", 300, 4000, 1000},
        {"hdrUnroll", "groupOutput", 0, 1, 1},
        // v1.1 film damage / film blur
        {"dustAmount", "groupFilmDamage", 0, 2, 0.6},
        {"dustSize", "groupFilmDamage", 0.5, 3, 1},
        {"dustNegative", "groupFilmDamage", 0, 1, 0.35},
        {"scratchAmount", "groupFilmDamage", 0, 2, 0.5},
        {"scratchPersistence", "groupFilmDamage", 0, 1, 0.6},
        {"scratchEmulsion", "groupFilmDamage", 0, 1, 0.3},
        {"scratchNegative", "groupFilmDamage", 0, 1, 0.25},
        {"hairAmount", "groupFilmDamage", 0, 2, 0.5},
        {"damageSeed", "groupFilmDamage", 0, 9999, 0},
        {"filmBlurRadius", "groupFilmBlur", 0.5, 12, 3},
        {"filmBlurFrequency", "groupFilmBlur", 0, 1, 0.3},
        {"filmBlurDuration", "groupFilmBlur", 12, 240, 24},
        {"filmBlurBreathing", "groupFilmBlur", 0, 1, 0.2},
        {"filmBlurChromatic", "groupFilmBlur", 0, 1, 0.3},
        {"filmBlurSeed", "groupFilmBlur", 0, 9999, 0},
        // v1.3 Tone (neutral defaults)
        {"zoneShadows", "groupTone", -4, 4, 0},
        {"zoneLights", "groupTone", -4, 4, 0},
        {"zoneHighlights", "groupTone", -4, 4, 0},
        {"zoneSpecular", "groupTone", -4, 4, 0},
        {"zoneFalloff", "groupTone", 0.25, 4, 1},
        {"filmContrast", "groupTone", 0.5, 2, 1},
        {"contrastPivot", "groupTone", -3, 3, 0},
        {"rolloffStart", "groupTone", 0.5, 8, 3},
        {"rolloffSoftness", "groupTone", -1, 1, 0},
        {"dyeSaturation", "groupTone", 0, 2, 1},
        {"dyeRichness", "groupTone", 0, 1, 0.25},
        {"dyeSatBalance", "groupTone", -1, 1, 0},
        {"alexaColour", "groupAlexaRendering", 0, 1, 1},   // v1.3 ALEXA Rendering
        {"latitudeStops", "groupHighlightLatitude", 0, 4, 2.5},   // v1.3 Highlight Latitude
        {"latitudeSoftness", "groupHighlightLatitude", 0, 1, 0.4},
        {"latitudeReconstruct", "groupHighlightLatitude", 0, 1, 1},
        {"latitudeDesaturate", "groupHighlightLatitude", 0, 1, 0.3},
        {"latitudeClip", "groupHighlightLatitude", 0.6, 1, 0.854},
        // v1.3 Texture II (neutral defaults: existing projects render unchanged, except the grain structure)
        {"grainColour", "groupGrain", 0, 1.5, 1},
        {"grainShadows", "groupGrain", 0, 2, 1},
        {"grainMids", "groupGrain", 0, 2, 1},
        {"grainHighlights", "groupGrain", 0, 2, 1},
        {"halationSaturation", "groupHalation", 0, 2, 1},
        {"halationBackground", "groupHalation", -1, 1, 0},
        {"auraRadius", "groupHalation", 40, 400, 150},
        {"auraStrength", "groupHalation", 0, 2, 0.5},
        {"auraGreen", "groupHalation", 0, 1, 0.35},
        {"breathExposure", "groupFilmBreath", 0, 0.5, 0.05},
        {"breathDevelopment", "groupFilmBreath", 0, 0.3, 0.03},
        {"breathPrinter", "groupFilmBreath", 0, 3, 0.5},
        {"breathPeriod", "groupFilmBreath", 4, 240, 24},
        {"breathSeed", "groupFilmBreath", 0, 9999, 0},
        {"leakAmount", "groupLightLeaks", 0, 2, 1},   // v1.10
        {"leakFrequency", "groupLightLeaks", 0, 1, 0.3},
        {"leakSize", "groupLightLeaks", 0.3, 2, 1},
        {"leakSeed", "groupLightLeaks", 1, 9999, 1},
        // v1.3 P1 extras (neutral defaults)
        {"sceneWbTemperature", "groupInput", 2000, 15000, 6500},
        {"sceneWbTint", "groupInput", -100, 100, 0},
        {"bloomThreshold", "groupBloom", 0, 8, 0},
        {"bloomKeepHighlights", "groupBloom", 0, 1, 0},
        // v1.3 reference look: diffusion filter grade, digital sharpening removal (0 = off)
        {"bloomFilterGrade", "groupBloom", 0.125, 2, 0.25},
        {"sharpenRemoval", "groupFilmResolution", 0, 1.5, 0},
        {"diagWipePosition", "groupOutput", 0, 1, 0.5},
    };
    for (const Spec& s : spec) {
        const ParamDef* d = findParam(s.id);
        check(d != nullptr, std::string("missing param ") + s.id);
        if (!d) continue;
        check(d->group == s.group, std::string(s.id) + ": wrong group " + d->group);
        check(d->min == s.mn && d->max == s.mx, std::string(s.id) + ": range differs from SPEC");
        check(d->def == s.def, std::string(s.id) + ": default differs from SPEC");
    }
    for (const char* p : {"printerR", "printerG", "printerB"})
        check(findParam(p) && findParam(p)->increment == 0.5, std::string(p) + ": step must be 0.5");

    auto defLabel = [](const char* pid) -> std::string {
        const ParamDef* d = findParam(pid);
        if (!d || d->type != PType::Choice || d->options.empty()) return "";
        return d->options[size_t(d->def)];
    };
    auto hasBool = [](const char* pid, bool def) {
        const ParamDef* d = findParam(pid);
        return d && d->type == PType::Bool && (d->def != 0.0) == def;
    };
    check(defLabel("lookPreset") == "Custom", "lookPreset default must be Custom");
    check(defLabel("mode") == "Full", "mode default must be Full");
    check(findParam("inputSpace") && findParam("inputSpace")->def == double(int(core::InputSpace::FLog2C)),
          "inputSpace default must be F-Log2 C");
    check(defLabel("negStock") == "Kodak Vision3 250D 5207", "negStock default must be Vision3 250D 5207 (got " + defLabel("negStock") + ")");
    check(defLabel("process") == "Print", "process default must be Print");
    check(defLabel("printStock") == "Kodak Vision 2383", "printStock default must be Kodak Vision 2383 (got " + defLabel("printStock") + ")");
    check(defLabel("filmStock") == "Kodak Vision3 250D 5207" && defLabel("printFilm") == "Kodak Vision 2383",
          "the visible stock lists must default to the saved stocks' defaults");
    check(defLabel("cameraEmulation") == "Fujifilm X-H2S -> ARRI ALEXA / AMIRA", "cameraEmulation default must be X-H2S -> ALEXA");
    check(defLabel("grainFormat") == "Super 35", "grainFormat default must be Super 35");
    check(defLabel("lutSize") == "33", "lutSize default must be 33");
    check(hasBool("grainEnable", true), "grainEnable default true");
    check(hasBool("grainAnimated", true), "grainAnimated default true");
    check(hasBool("stillPhoto", false), "stillPhoto default false (moving footage)");
    check(hasBool("pushUnder", false), "pushUnder default false (Push / Pull = development only)");
    check(hasBool("crossProcess", false), "crossProcess default false (normal processing)");

    // ---- module switches (v1.1, SPEC §7): one Bool per module, placed directly before the controls it
    // switches, default ON for every module that existed in v1.0 (projects saved before the switch
    // existed load the default and must render as before), part of looks exactly like their module.
    struct Switch {
        const char* id;
        const char* group;
        bool def, look;
        std::vector<const char*> controls;   // the module's controls, which must follow the switch
    };
    const std::vector<Switch> switches = {
        {"exposureEnable", "groupInput", true, false, {"exposure"}},
        {"developmentEnable", "groupNegative", true, true, {"pushPull", "pushUnder", "interlayer", "negBleach", "neutralize"}},
        {"printEnable", "groupPrint", true, true, {"printerR", "printerG", "printerB", "printExposure", "printBleach"}},
        {"halationEnable", "groupHalation", true, true, {"halationAmount", "halationRadius", "halationThreshold", "halationHue",
                                                         "halationSaturation", "halationBackground", "auraEnable"}},
        // v1.3 Aura: sub-module of halation, OFF by default
        {"auraEnable", "groupHalation", false, true, {"auraRadius", "auraStrength", "auraGreen"}},
        {"bloomEnable", "groupBloom", true, true, {"bloomAmount", "bloomRadius", "bloomThreshold", "bloomKeepHighlights",
                                                   "bloomFilter", "bloomFilterGrade"}},
        {"grainEnable", "groupGrain", true, true, {"grainAmount", "grainSize", "grainSoftness", "grainAnimated", "grainSeed",
                                                   "grainColour", "grainShadows", "grainMids", "grainHighlights"}},
        {"acutanceEnable", "groupLensGate", true, true, {"acutanceAmount", "acutanceRadius"}},
        // v1.2: film resolution (negative MTF + print/scan system); grainFormat moved to the Look
        // group as "Gauge" (it scales grain, halation and acutance), so no switch owns it
        {"filmResolutionEnable", "groupFilmResolution", true, true, {"integrate", "sharpenRemoval"}},
        {"weaveEnable", "groupLensGate", true, true, {"weaveAmount", "weaveSpeed"}},
        {"vignetteEnable", "groupLensGate", true, true, {"vignette"}},
        {"hdrUnrollEnable", "groupOutput", true, false, {"hdrUnroll"}},
        // v1.1 film damage (master + sub-module switches) and film blur: new modules, OFF by default
        {"damageEnable", "groupFilmDamage", false, true, {"dustEnable", "scratchEnable", "hairEnable", "damageAnimated", "damageSeed"}},
        {"dustEnable", "groupFilmDamage", true, true, {"dustAmount", "dustSize", "dustNegative"}},
        {"scratchEnable", "groupFilmDamage", true, true, {"scratchAmount", "scratchPersistence", "scratchEmulsion", "scratchNegative"}},
        {"hairEnable", "groupFilmDamage", true, true, {"hairAmount"}},
        {"filmBlurEnable", "groupFilmBlur", false, true, {"filmBlurRadius", "filmBlurFrequency", "filmBlurDuration", "filmBlurBreathing",
                                                         "filmBlurChromatic", "filmBlurSeed"}},
        // v1.2 camera emulation: ON by default (part of the v1.2 default look), not part of looks
        {"cameraEmulationEnable", "groupCameraEmulation", true, false, {"cameraEmulation", "cameraEmulationStrength"}},
        // v1.3 ALEXA Rendering: the stage's A/B switch (on, only acts in that stage) and ALEXA Colour (off, look)
        {"alexaDisplayEnable", "groupAlexaRendering", true, false, {}},
        {"alexaColourEnable", "groupAlexaRendering", false, true, {"alexaColour"}},
        // v1.3 Highlight Latitude: new module, OFF by default, a camera setting (not part of looks)
        {"latitudeEnable", "groupHighlightLatitude", false, false, {"latitudeStops", "latitudeSoftness", "latitudeReconstruct",
                                                                   "latitudeDesaturate", "latitudeClip"}},
        // v1.3 Film Breath: new module, OFF by default
        {"breathEnable", "groupFilmBreath", false, true, {"breathProfile", "breathExposure", "breathDevelopment", "breathPrinter",
                                                         "breathPeriod", "breathSeed"}},
        // v1.3 P1 extras: new modules, OFF by default, not part of looks
        {"sceneWbEnable", "groupInput", false, false, {"sceneWbTemperature", "sceneWbTint"}},
        // v1.10 Light Leaks: new module, OFF by default
        {"leakEnable", "groupLightLeaks", false, true, {"leakAmount", "leakFrequency", "leakSize", "leakRollEnd", "leakSeed"}},
        {"falseColourEnable", "groupOutput", false, false, {}},
        {"exposureZebras", "groupOutput", false, false, {}},   // v1.11 diagnostic overlay
    };
    auto position = [&](const std::string& pid) {
        for (size_t i = 0; i < defs.size(); ++i)
            if (defs[i].id == pid) return int(i);
        return -1;
    };
    for (const Switch& sw : switches) {
        const ParamDef* d = findParam(sw.id);
        check(d != nullptr, std::string("missing module switch ") + sw.id);
        if (!d) continue;
        check(d->type == PType::Bool && (d->def != 0.0) == sw.def, std::string(sw.id) + ": Bool with default " + (sw.def ? "on" : "off"));
        check(d->group == sw.group, std::string(sw.id) + ": wrong group " + d->group);
        check(d->look == sw.look, std::string(sw.id) + ": look flag must match its module");
        check(!d->animates, std::string(sw.id) + ": module switches are not keyframable");
        check(d->label.rfind("Enable ", 0) == 0, std::string(sw.id) + ": label must start with \"Enable \"");
        const int ps = position(sw.id);
        for (const char* c : sw.controls) {
            const ParamDef* cd = findParam(c);
            check(cd && cd->group == sw.group && position(c) > ps, std::string(c) + " must follow its switch " + sw.id);
            if (cd) check(cd->look == sw.look || std::string(c).find("Seed") != std::string::npos, std::string(c) + ": look flag differs from its switch");
        }
    }

    // ---- choice lists match the core / texture enumerations (order = enum order) ----
    auto count = [](const char* pid) { const ParamDef* d = findParam(pid); return d ? int(d->options.size()) : -1; };
    check(count("inputSpace") == int(core::InputSpace::Count), "inputSpace options != InputSpace::Count");
    check(count("outputSpace") == int(core::OutputSpace::Count), "outputSpace options != OutputSpace::Count");
    check(count("mode") == int(core::Mode::Count), "mode options != Mode::Count");
    check(count("process") == int(core::Process::Count), "process options != Process::Count");
    check(count("scanLook") == int(core::ScanLook::Count), "scanLook options != ScanLook::Count");   // v1.12
    check(defLabel("scanLook") == "Neutral", "scanLook default must be Neutral");
    check(count("grainFormat") == int(tex::GrainFormat::Count), "grainFormat options != GrainFormat::Count");
    check(count("breathProfile") == int(tex::BreathProfile::Count), "breathProfile options != BreathProfile::Count");
    check(count("negStock") == int(core::negativeStocks().size()), "negStock options != negativeStocks()");
    check(count("printStock") == int(core::printStockNames().size()), "printStock options != printStockNames()");
    check(count("lookPreset") == int(lookPresets().size()) + 1, "lookPreset options != presets + Custom");
    check(count("negStock") == 26, "SPEC §4.6: 26 negative stocks (v1.3: 11 designed + 15 data-sheet / style)");
    check(count("printStock") == 15, "SPEC §4.6: 15 print stocks (6 designed + 5 data-sheet film + 4 photo papers)");
    // real film only: the visible lists hold the data-sheet stocks (plus the Kodachrome style), every stock maps to one
    check(count("filmStock") == 15 && int(realNegStocks().size()) == 15, "Film Stock: 15 real negatives");
    check(count("printFilm") == 8 && int(realPrintStocks().size()) == 8, "Print Stock: 8 real prints (4 film, 4 paper)");
    for (int i = 0; i < int(core::negativeStocks().size()); ++i)
        check(realNegFor(i) >= 0 && realNegFor(i) < 15 && (core::negativeStocks()[size_t(i)].monochrome ==
              core::negativeStocks()[size_t(realNegStocks()[size_t(realNegFor(i))])].monochrome) &&
              (core::negativeStocks()[size_t(i)].reversal ==
              core::negativeStocks()[size_t(realNegStocks()[size_t(realNegFor(i))])].reversal),
              "negative " + std::to_string(i) + " must map to a real stock of the same kind");
    for (int k = 0; k < 15; ++k) check(realNegFor(realNegStocks()[size_t(k)]) == k, "real negatives map to themselves");
    for (int k = 0; k < 4; ++k) check(realPrintFor(realPrintStocks()[size_t(k)]) == k, "real prints map to themselves");
    for (const char* p : {"filmStock", "printFilm"})
        check(findParam(p) && !findParam(p)->persistent && !findParam(p)->look, std::string(p) + " must be a non-saved view");
    check(count("cameraEmulation") == int(core::CameraEmulation::Count) - 1, "cameraEmulation options != CameraEmulation::Count - 1");
    check(count("pipelineStage") == int(StageChoice::Count), "pipelineStage options != StageChoice::Count");
    check(count("bloomFilter") == tex::kBloomFilterCount, "bloomFilter options != kBloomFilterCount");
    check(count("halationShow") == 3, "halationShow options: Off, Effect Only, Mask");

    // v1.3 diagnostic views: in their module's group (after its switch), off, not look params, not
    // keyframable, labelled as diagnostics
    {
        const struct { const char* id; const char* group; const char* sw; } dv[] = {
            {"halationShow", "groupHalation", "halationEnable"},       {"bloomShow", "groupBloom", "bloomEnable"},
            {"grainShow", "groupGrain", "grainEnable"},                {"acutanceShow", "groupLensGate", "acutanceEnable"},
            {"filmResolutionShow", "groupFilmResolution", "filmResolutionEnable"},
            {"textureDiffShow", "groupOutput", nullptr},               {"diagWipe", "groupOutput", nullptr},
            {"diagWipePosition", "groupOutput", nullptr}};
        for (const auto& e : dv) {
            const ParamDef* d = findParam(e.id);
            check(d != nullptr, std::string("missing diagnostic view ") + e.id);
            if (!d) continue;
            check(d->group == e.group, std::string(e.id) + ": wrong group " + d->group);
            const bool pos = std::string(e.id) == "diagWipePosition";   // a moving wipe may be keyframed
            check(!d->look && (pos || !d->animates), std::string(e.id) + ": a diagnostic view is not a look param nor keyframable");
            check(pos || d->def == 0.0, std::string(e.id) + ": diagnostic views default off");
            check(d->hint.rfind("DIAGNOSTIC VIEW", 0) == 0, std::string(e.id) + ": tooltip must start with DIAGNOSTIC VIEW");
            check(isDiagnosticViewParam(e.id) && !userPresetStores(*d), std::string(e.id) + ": never stored in user presets");
            if (e.sw) check(position(e.id) > position(e.sw), std::string(e.id) + " must follow " + e.sw);
        }
    }

    // every group of SPEC §7 exists
    for (const char* g : {"groupLook", "groupInput", "groupCameraEmulation", "groupNegative", "groupPrint", "groupHalation", "groupBloom",
                          "groupGrain", "groupLensGate", "groupFilmDamage", "groupFilmBlur", "groupOutput", "groupLutExport",
                          "groupLicense", "groupAbout"})
        check(groupsSeen.count(g) == 1, std::string("missing group ") + g);
    for (const char* p : {"lookPreset", "mode", "inputSpace", "negStock", "process", "printStock", "grainEnable",
                          "grainFormat", "grainAnimated", "outputSpace", "lutSize", "lutPath", "lutExport",
                          "licenseStatus", "licenseKey", "licenseActivate", "licenseRemove", "aboutVersion",
                          "aboutManual"})
        check(findParam(p) != nullptr, std::string("missing param ") + p);
    // build variant (SPEC §9): the personal build keeps the License group (saved projects) but
    // secret; the licensed build shows it. Nothing else is ever secret except the full stock lists
    // (real film only: Film Stock / Print Stock show them).
    for (const ParamDef& d : defs) {
        const bool inLicense = d.id == "groupLicense" || d.group == "groupLicense";
        const bool fullStocks = d.id == "negStock" || d.id == "printStock";
        check(d.secret == (fullStocks || (kPersonalBuild && inLicense)),
              d.id + (d.secret ? " must not be secret" : " must be secret in the personal build"));
    }

    // ---- presets ----
    std::vector<std::string> problems;
    check(validatePresets(&problems), "preset table has problems");
    for (const std::string& p : problems) std::printf("  preset problem: %s\n", p.c_str());
    check(lookPresets().size() >= 3, "at least 3 presets");
    size_t lookCount = 0;
    for (const ParamDef& d : defs) lookCount += d.look ? 1 : 0;
    for (size_t i = 0; i < lookPresets().size(); ++i)
        check(resolvePreset(i).size() == lookCount, "preset " + lookPresets()[i].name + " does not resolve all look params");
    // look params never include colour-space / exposure / license / LUT parameters
    for (const char* p : {"inputSpace", "outputSpace", "exposure", "exposureEnable", "mode", "hdrPeakNits", "hdrUnroll",
                          "hdrUnrollEnable", "stillPhoto", "grainSeed", "damageSeed", "filmBlurSeed", "lutSize"})
        check(findParam(p) && !findParam(p)->look, std::string(p) + " must not be a look (preset) parameter");

    // ---- platform helpers ----
    check(fileComponent("CN 200T Studio Tungsten") == "CN-200T-Studio-Tungsten", "fileComponent basic");
    check(fileComponent("  Rec.709 / Gamma 2.4 ") == "Rec-709-Gamma-2-4", "fileComponent punctuation");
    check(fileComponent("***") == "Untitled", "fileComponent empty");
    check(normalizeUserPath("  \"/tmp/x\" ") == "/tmp/x", "normalizeUserPath trims quotes/space");
#if !defined(_WIN32)
    check(joinPath("/a/b/", "c.cube") == "/a/b/c.cube", "joinPath trailing separator");
    check(joinPath("/", "c.cube") == "/c.cube", "joinPath root");
#endif
    check(!defaultLutDirectory().empty(), "default LUT directory");
    const std::string lutDefault = findParam("lutPath") ? findParam("lutPath")->defString : "";
    // empty = <Documents>/Reafilm LUTs/ of the exporting user, resolved at export time (no per-user
    // absolute path in the cached descriptor or in saved projects)
    check(lutDefault.empty(), "lutPath default is empty (resolved at export time)");
    check(defaultLutDirectory().find("Reafilm LUTs") != std::string::npos, "default LUT folder is <Documents>/Reafilm LUTs/");

    // ---- v1.3 P1 user presets: JSON round trip, tolerance, folders ----
    {
        std::vector<UserPresetValue> cur;
        for (const ParamDef& d : defs) {
            if (!userPresetStores(d)) continue;
            UserPresetValue v;
            v.param = d.id;
            v.num = d.def;
            v.text = d.defString;
            if (d.id == "exposure") v.num = 1.25;
            if (d.id == "negStock") v.num = 3;
            if (d.id == "grainEnable") v.num = 0;
            if (d.id == "halationRadius") v.num = 20.1 + 0.2;   // not exactly representable in short decimal
            if (d.id == "userPresetName") v.text = "x";
            cur.push_back(v);
        }
        check(!findParam("lutPath") || !userPresetStores(*findParam("lutPath")), "user presets do not store lutPath");
        check(!userPresetStores(*findParam("falseColourEnable")), "user presets do not store False Colour");
    check(!userPresetStores(*findParam("exposureZebras")), "user presets do not store Exposure Zebras");
        check(!userPresetStores(*findParam("licenseKey")), "user presets do not store the license key");
        check(userPresetStores(*findParam("inputSpace")) && userPresetStores(*findParam("sceneWbEnable")),
              "user presets store input and module switches");
        const std::string js = userPresetToJson("My \"Look\" 1", "test", cur);
        std::vector<UserPresetValue> back;
        std::vector<std::string> notes;
        std::string nm, err;
        check(userPresetFromJson(js, back, &nm, &notes, &err), "user preset round trip parses: " + err);
        check(nm == "My \"Look\" 1", "user preset name round trip");
        check(back.size() == cur.size() && notes.empty(), "user preset round trip: same values, no notes");
        for (const std::string& n : notes) std::printf("  note: %s\n", n.c_str());
        for (size_t i = 0; i < back.size() && i < cur.size(); ++i)
            if (back[i].param != cur[i].param || back[i].num != cur[i].num || back[i].text != cur[i].text)
                std::printf("  differs: %s %.17g / %.17g\n", cur[i].param.c_str(), cur[i].num, back[i].num);
        bool same = back.size() == cur.size();
        for (size_t i = 0; same && i < back.size(); ++i)
            same = back[i].param == cur[i].param && back[i].num == cur[i].num && back[i].text == cur[i].text;
        check(same, "user preset round trip is exact");
        check(js.find("\"negStock\": \"") != std::string::npos, "choices are stored as labels");
        // tolerance: unknown keys ignored + reported, missing keys at defaults, labels or indices, clamping
        const std::string old = "\xEF\xBB\xBF{\"format\":\"reafilm-preset\",\"version\":1,\"future\":[1,{\"a\":null}],"
                                "\"params\":{\"exposure\":9.5,\"negStock\":2,\"process\":\"lab scan\",\"noSuchParam\":3,"
                                "\"grainEnable\":false,\"printStock\":\"No Such Print\"}}";
        back.clear(); notes.clear();
        check(userPresetFromJson(old, back, nullptr, &notes, &err), "tolerant user preset parses: " + err);
        auto val = [&](const char* pid) {
            for (const UserPresetValue& v : back) if (v.param == pid) return v.num;
            return -999.0;
        };
        check(val("exposure") == 4.0, "user preset numbers are clamped to the range");
        check(val("negStock") == 2.0, "user preset choice by index");
        check(val("process") == double(int(core::Process::NegativeScan)), "user preset choice by label (case-insensitive)");
        check(val("grainEnable") == 0.0, "user preset bool");
        check(val("printStock") == findParam("printStock")->def, "unknown choice label keeps the default");
        check(val("halationRadius") == findParam("halationRadius")->def && val("sceneWbEnable") == 0.0,
              "missing params return to their defaults");
        bool unknownNoted = false, futureNoted = false;
        for (const std::string& n : notes) {
            unknownNoted |= n.find("noSuchParam") != std::string::npos;
            futureNoted |= n.find("future") != std::string::npos;
        }
        check(unknownNoted && futureNoted, "unknown keys are reported");
        check(!userPresetFromJson("{\"params\":{}}", back, nullptr, nullptr, &err), "a file without the format tag is refused");
        check(!userPresetFromJson("{\"format\":\"reafilm-preset\",\"params\":{", back, nullptr, nullptr, &err), "truncated JSON is refused");
        check(userPresetFileName("My Look 2") == "My-Look-2.json", "user preset file name");

        // folders: the override, then (Linux) the XDG folder with the legacy Celluloid fallback
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path tmp = fs::temp_directory_path(ec) / ("reafilm_selftest_" + std::to_string(std::rand()));
        const std::string tdir = tmp.string();
        setEnv("REAFILM_PRESET_DIR", tdir);
        check(userPresetDirectory() == tdir && legacyUserPresetDirectory().empty(), "REAFILM_PRESET_DIR override");
        check(ensureDirectory(tdir, &err), "preset folder created");
        check(writeTextFile(joinPath(tdir, userPresetFileName("Round Trip")), js, &err), "preset written: " + err);
        check(writeTextFile(joinPath(tdir, userPresetFileName("Round Trip")), js, &err), "preset replaced: " + err);
        const std::string found = findUserPresetFile("Round Trip");
        std::string text;
        check(!found.empty() && readTextFile(found, text) && text == js, "preset found and read back");
        check(listUserPresets() == std::vector<std::string>{"Round-Trip"}, "preset listed");
        setEnv("REAFILM_PRESET_DIR", "");
        setEnv("CELLULOID_PRESET_DIR", "");
#if !defined(_WIN32) && !defined(__APPLE__)
        const std::string xdgOld = envVar("XDG_CONFIG_HOME");
        setEnv("XDG_CONFIG_HOME", tdir);
        check(userPresetDirectory() == joinPath(joinPath(tdir, "reafilm"), "presets"), "Linux preset folder");
        const std::string legacy = legacyUserPresetDirectory();
        check(legacy == joinPath(joinPath(tdir, "celluloid"), "presets"), "Linux legacy preset folder");
        check(ensureDirectory(legacy, &err) && writeTextFile(joinPath(legacy, "Old-One.json"), js, &err), "legacy preset written");
        check(findUserPresetFile("Old One") == joinPath(legacy, "Old-One.json"), "legacy folder is the fallback");
        check(ensureDirectory(userPresetDirectory(), &err) &&
                  writeTextFile(joinPath(userPresetDirectory(), "Old-One.json"), js, &err) &&
                  findUserPresetFile("Old One") == joinPath(userPresetDirectory(), "Old-One.json"),
              "the Reafilm folder wins over the legacy folder");
        setEnv("XDG_CONFIG_HOME", xdgOld);
#endif
        fs::remove_all(tmp, ec);
    }

    std::printf("ofx self test: %d checks, %d failures (%zu params, %zu presets)\n", g_checks, g_fail, defs.size(),
                lookPresets().size());
    return g_fail == 0 ? 0 : 1;
}
