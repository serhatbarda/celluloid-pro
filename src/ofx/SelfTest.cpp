// Celluloid Pro - self test of the OFX parameter registry and the look-preset table.
// Checks the registry against SPEC §7 (ids, groups, ranges, defaults, hints), the choice lists
// against the core enumerations, every preset against the registry, and the path helpers.
#include "Params.h"
#include "Platform.h"
#include "Presets.h"
#include "../core/Core.h"
#include "../texture/Texture.h"

#include <cmath>
#include <cstdio>
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
        {"pushPull", "groupNegative", -2, 3, 0},
        {"interlayer", "groupNegative", 0, 2, 1},
        {"negBleach", "groupNegative", 0, 1, 0},
        {"neutralize", "groupNegative", 0, 1, 0},
        {"printerR", "groupPrint", -12, 12, 0},
        {"printerG", "groupPrint", -12, 12, 0},
        {"printerB", "groupPrint", -12, 12, 0},
        {"printExposure", "groupPrint", -12, 12, 0},
        {"printBleach", "groupPrint", 0, 1, 0},
        {"halationAmount", "groupHalation", 0, 2, 0.5},
        {"halationRadius", "groupHalation", 2, 80, 20},
        {"halationThreshold", "groupHalation", 0, 6, 3.5},
        {"halationHue", "groupHalation", -1, 1, 0},
        {"bloomAmount", "groupBloom", 0, 2, 0.15},
        {"bloomRadius", "groupBloom", 5, 200, 40},
        {"grainAmount", "groupGrain", 0, 3, 1},
        {"grainSize", "groupGrain", 0.5, 3, 1},
        {"grainSoftness", "groupGrain", 0, 1, 0.5},
        {"grainSeed", "groupGrain", 0, 9999, 0},
        {"acutanceAmount", "groupLensGate", -1, 2, 0.3},
        {"acutanceRadius", "groupLensGate", 0.5, 10, 2},
        {"weaveAmount", "groupLensGate", 0, 2, 0},
        {"weaveSpeed", "groupLensGate", 0.25, 4, 1},
        {"vignette", "groupLensGate", 0, 1, 0},
        {"hdrPeakNits", "groupOutput", 300, 4000, 1000},
        {"hdrUnroll", "groupOutput", 0, 1, 1},
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
    check(defLabel("negStock") == "CN 200T Studio Tungsten", "negStock default must be CN 200T (got " + defLabel("negStock") + ")");
    check(defLabel("process") == "Print", "process default must be Print");
    check(defLabel("printStock") == "P1 Standard Projection", "printStock default must be P1 (got " + defLabel("printStock") + ")");
    check(defLabel("grainFormat") == "Super 35", "grainFormat default must be Super 35");
    check(defLabel("lutSize") == "33", "lutSize default must be 33");
    check(hasBool("grainEnable", true), "grainEnable default true");
    check(hasBool("grainAnimated", true), "grainAnimated default true");

    // ---- choice lists match the core / texture enumerations (order = enum order) ----
    auto count = [](const char* pid) { const ParamDef* d = findParam(pid); return d ? int(d->options.size()) : -1; };
    check(count("inputSpace") == int(core::InputSpace::Count), "inputSpace options != InputSpace::Count");
    check(count("outputSpace") == int(core::OutputSpace::Count), "outputSpace options != OutputSpace::Count");
    check(count("mode") == int(core::Mode::Count), "mode options != Mode::Count");
    check(count("process") == int(core::Process::Count), "process options != Process::Count");
    check(count("grainFormat") == int(tex::GrainFormat::Count), "grainFormat options != GrainFormat::Count");
    check(count("negStock") == int(core::negativeStocks().size()), "negStock options != negativeStocks()");
    check(count("printStock") == int(core::printStockNames().size()), "printStock options != printStockNames()");
    check(count("lookPreset") == int(lookPresets().size()) + 1, "lookPreset options != presets + Custom");
    check(count("negStock") == 11, "SPEC §4.6: 11 negative stocks");
    check(count("printStock") == 5, "SPEC §4.6: 5 print stocks");

    // every group of SPEC §7 exists
    for (const char* g : {"groupLook", "groupInput", "groupNegative", "groupPrint", "groupHalation", "groupBloom",
                          "groupGrain", "groupLensGate", "groupOutput", "groupLutExport", "groupLicense", "groupAbout"})
        check(groupsSeen.count(g) == 1, std::string("missing group ") + g);
    for (const char* p : {"lookPreset", "mode", "inputSpace", "negStock", "process", "printStock", "grainEnable",
                          "grainFormat", "grainAnimated", "outputSpace", "lutSize", "lutPath", "lutExport",
                          "licenseStatus", "licenseKey", "licenseActivate", "licenseRemove", "aboutVersion",
                          "aboutManual"})
        check(findParam(p) != nullptr, std::string("missing param ") + p);

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
    for (const char* p : {"inputSpace", "outputSpace", "exposure", "mode", "hdrPeakNits", "hdrUnroll", "grainSeed", "lutSize"})
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
    // empty = <Documents>/Celluloid LUTs/ of the exporting user, resolved at export time (no per-user
    // absolute path in the cached descriptor or in saved projects)
    check(lutDefault.empty(), "lutPath default is empty (resolved at export time)");
    check(defaultLutDirectory().find("Celluloid LUTs") != std::string::npos, "default LUT folder is <Documents>/Celluloid LUTs/");

    std::printf("ofx self test: %d checks, %d failures (%zu params, %zu presets)\n", g_checks, g_fail, defs.size(),
                lookPresets().size());
    return g_fail == 0 ? 0 : 1;
}
