// Reafilm Pro - user presets (v1.3 P1): the complete parameter set of a node as a versioned JSON file.
//
// File format (UTF-8 JSON, one object):
//   { "format": "reafilm-preset", "version": 1, "plugin": "<version string>", "name": "<preset name>",
//     "params": { "<param id>": <value>, ... } }
// Values: numbers for Double / Int params, true / false for Bool params, the option LABEL (string) for Choice
// params (a number is read as the option index), strings for String params. Param ids are the permanent
// registry ids (Params.h).
//
// Loading is tolerant: unknown keys (top level or params) are ignored and reported, params missing from the
// file return to their defaults (new modules default to off, so an old preset renders as it did), numbers
// are clamped to the parameter range, unknown choice labels keep the default and are reported.
//
// Saved: every persistent Double / Int / Bool / Choice / String parameter except the License group, the LUT
// folder (a per-machine path), the preset name field itself and the False Colour diagnostic view.
#pragma once
#include "Params.h"

#include <string>
#include <vector>

namespace cel { namespace ofxp {

constexpr const char* kUserPresetFormat = "reafilm-preset";
constexpr int kUserPresetVersion = 1;

// true for the parameters a user preset stores (and restores)
bool userPresetStores(const ParamDef& d);

// One stored value. num: Double / Int / Bool (0/1) / Choice (option index); text: String values.
struct UserPresetValue {
    std::string param;
    double num = 0.0;
    std::string text;
};

// Serialises `values` (taken from the node; order kept) as the preset JSON.
std::string userPresetToJson(const std::string& name, const std::string& pluginVersion,
                             const std::vector<UserPresetValue>& values);

// Parses a preset file and resolves it against the registry: returns one value for EVERY parameter
// userPresetStores() accepts (registry order; missing ones at their defaults). False + *err for a file that
// is not a Reafilm preset (bad JSON, wrong "format", no "params" object). Notes about ignored or invalid
// entries go to *notes.
bool userPresetFromJson(const std::string& json, std::vector<UserPresetValue>& out, std::string* name,
                        std::vector<std::string>* notes, std::string* err);

// File name of a preset name: fileComponent(name) + ".json" ("My Look 2" -> "My-Look-2.json").
std::string userPresetFileName(const std::string& name);

// Resolves a preset name to an existing file: the Reafilm folder first, then the legacy Celluloid folder.
// "" when neither has it.
std::string findUserPresetFile(const std::string& name);

// Preset names (file names without .json) in the Reafilm folder and the legacy folder, sorted, unique.
std::vector<std::string> listUserPresets();

}} // namespace cel::ofxp
