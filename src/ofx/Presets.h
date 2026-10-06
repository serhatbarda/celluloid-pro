// Celluloid Pro - curated look presets (data-driven).
//
// A preset is a complete look recipe: a name, a one-line description and a list of
// (parameter id, value) pairs. Values are strings so the table stays readable and robust:
//   - numbers:  "0.85"            (Double / Int params; clamped to the parameter range)
//   - booleans: "true" / "false"
//   - choices:  the option LABEL ("CN 500T High Speed", "Lab Scan", "Super 35"), matched
//               case-insensitively; a decimal option index also works but is discouraged.
//
// Applying a preset sets EVERY look parameter (ParamDef::look == true): the ones listed to the
// preset's value, all others back to their defaults, so a preset always produces the same image.
// Input/output colour space, exposure, mode, HDR, LUT, license and grain seed are never touched.
// Afterwards the read-only "Preset Status" label shows the preset name, or "<name> (modified)"
// as soon as any look parameter differs from the recipe (the cleaner of the two UX options in
// SPEC §7: the user keeps the context of what they started from).
//
// The look designer owns the table in Presets.cpp. The order of presets is the order of the
// lookPreset choice param after "Custom": APPEND new presets, never reorder or delete (saved
// projects store the choice index).
#pragma once
#include <string>
#include <vector>

namespace cel { namespace ofxp {

struct PresetValue {
    std::string param;
    std::string value;
};

struct LookPreset {
    std::string name;
    std::string description;
    std::vector<PresetValue> values;
};

// All presets in choice order (index 0 of the lookPreset param is "Custom", preset i is option i+1).
const std::vector<LookPreset>& lookPresets();

// A preset resolved against the parameter registry: one numeric value per look parameter
// (choices = option index, bools = 0/1), in the order of the registry's look parameters.
struct ResolvedValue {
    std::string param;
    double value;
};
// Resolves preset `index` (0-based into lookPresets()). Invalid entries are skipped and
// described in *problems (if given). Returns an empty vector for an out-of-range index.
std::vector<ResolvedValue> resolvePreset(size_t index, std::vector<std::string>* problems = nullptr);

// Validates every preset (unknown params, non-look params, unparsable or out-of-range values,
// unknown choice labels, duplicate names). Returns true when the table is clean.
bool validatePresets(std::vector<std::string>* problems);

}} // namespace cel::ofxp
