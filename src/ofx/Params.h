// Reafilm Pro - OFX parameter registry.
//
// Every parameter of the plugin is described ONCE here (id, type, group, label, range, default,
// hint, flags). describeInContext() defines the OFX parameters from this table, the look-preset
// engine validates and applies presets against it, and the self-test checks it against SPEC §7.
//
// Parameter and group IDs are PERMANENT (they are stored in user projects): never rename or
// remove one; append new parameters instead. Choice option order is permanent too (append only).
#pragma once
#include <string>
#include <vector>

// Build variant (SPEC §9): CEL_PERSONAL_BUILD=1 is the default personal, non-commercial build (no
// license check, no trial watermark, LUT export always on; the License group stays in the table as
// secret parameters so saved projects keep their IDs). CEL_PERSONAL_BUILD=0 is the licensed build
// (-DCEL_LICENSING=ON). src/ofx/CMakeLists.txt always defines it.
#ifndef CEL_PERSONAL_BUILD
#  define CEL_PERSONAL_BUILD 1
#endif

namespace cel { namespace ofxp {

constexpr bool kPersonalBuild = CEL_PERSONAL_BUILD != 0;
// License Status text of the personal build (also how tools detect the variant from --list-params).
constexpr const char* kPersonalLicenseStatus = "Personal build - non-commercial use, no license needed";

enum class PType { Group, Double, Int, Bool, Choice, String, Label, Push };

enum class StrKind { SingleLine, Directory };

struct ParamDef {
    std::string id;
    PType       type = PType::Double;
    std::string group;          // parent group id ("" = top level)
    std::string label;
    std::string hint;
    // numeric (Double/Int) and Choice/Bool defaults
    double def = 0.0, min = 0.0, max = 1.0;
    double increment = 0.01;    // Double: slider step
    int    digits = 2;          // Double: displayed digits
    std::vector<std::string> options;   // Choice
    // strings
    std::string defString;
    StrKind     strKind = StrKind::SingleLine;
    // flags
    bool animates = true;       // keyframable (look/texture params); false for choices that restructure
    bool evaluateOnChange = true;
    bool persistent = true;
    bool groupOpen = false;     // Group: expanded by default
    bool look = false;          // part of a look preset (set by lookPreset, compared for "(modified)")
    bool secret = false;        // hidden from the user (kOfxParamPropSecret); still defined and saved
};

// The full ordered table (groups precede their children). Built once on first use; choice
// options for colour spaces and stocks come from cel::core.
const std::vector<ParamDef>& paramDefs();
const ParamDef* findParam(const std::string& id);

// Case-insensitive choice lookup: returns the option index for a label (or a decimal index
// string), or -1 if not found.
int choiceIndex(const ParamDef& d, const std::string& labelOrIndex);

// Default value of a preset-able parameter as a double (choices: option index; bools: 0/1).
double numericDefault(const ParamDef& d);

// ---- parameter / group ids (permanent) -------------------------------------------------
namespace id {
// groups
constexpr const char* gLook = "groupLook";
constexpr const char* gInput = "groupInput";
constexpr const char* gNegative = "groupNegative";
constexpr const char* gPrint = "groupPrint";
constexpr const char* gHalation = "groupHalation";
constexpr const char* gBloom = "groupBloom";
constexpr const char* gGrain = "groupGrain";
constexpr const char* gLens = "groupLensGate";
constexpr const char* gOutput = "groupOutput";
constexpr const char* gLut = "groupLutExport";
constexpr const char* gLicense = "groupLicense";
constexpr const char* gAbout = "groupAbout";
// [Look]
constexpr const char* lookPreset = "lookPreset";
constexpr const char* lookPresetStatus = "lookPresetStatus";
constexpr const char* mode = "mode";
// [Input]
constexpr const char* inputSpace = "inputSpace";
constexpr const char* exposure = "exposure";
// [Negative]
constexpr const char* negStock = "negStock";
constexpr const char* pushPull = "pushPull";
constexpr const char* interlayer = "interlayer";
constexpr const char* negBleach = "negBleach";
constexpr const char* neutralize = "neutralize";
// [Print]
constexpr const char* process = "process";
constexpr const char* printStock = "printStock";
constexpr const char* printerR = "printerR";
constexpr const char* printerG = "printerG";
constexpr const char* printerB = "printerB";
constexpr const char* printExposure = "printExposure";
constexpr const char* printBleach = "printBleach";
// [Halation]
constexpr const char* halationAmount = "halationAmount";
constexpr const char* halationRadius = "halationRadius";
constexpr const char* halationThreshold = "halationThreshold";
constexpr const char* halationHue = "halationHue";
// [Bloom]
constexpr const char* bloomAmount = "bloomAmount";
constexpr const char* bloomRadius = "bloomRadius";
// [Grain]
constexpr const char* grainEnable = "grainEnable";
constexpr const char* grainFormat = "grainFormat";
constexpr const char* grainAmount = "grainAmount";
constexpr const char* grainSize = "grainSize";
constexpr const char* grainSoftness = "grainSoftness";
constexpr const char* grainAnimated = "grainAnimated";
constexpr const char* grainSeed = "grainSeed";
// [Lens & Gate]
constexpr const char* acutanceAmount = "acutanceAmount";
constexpr const char* acutanceRadius = "acutanceRadius";
constexpr const char* weaveAmount = "weaveAmount";
constexpr const char* weaveSpeed = "weaveSpeed";
constexpr const char* vignette = "vignette";
// [Output]
constexpr const char* outputSpace = "outputSpace";
constexpr const char* hdrPeakNits = "hdrPeakNits";
constexpr const char* hdrUnroll = "hdrUnroll";
// [LUT Export]
constexpr const char* lutSize = "lutSize";
constexpr const char* lutPath = "lutPath";
constexpr const char* lutExport = "lutExport";
// [License]
constexpr const char* licenseStatus = "licenseStatus";
constexpr const char* licenseKey = "licenseKey";
constexpr const char* licenseActivate = "licenseActivate";
constexpr const char* licenseRemove = "licenseRemove";
// [About]
constexpr const char* aboutVersion = "aboutVersion";
constexpr const char* aboutManual = "aboutManual";
// ---- v1.1: module switches (one checkbox at the top of every module; appended IDs) ----------
// Off = the module is bypassed with its values kept (see Params.cpp for each module's "off").
constexpr const char* exposureEnable = "exposureEnable";        // [Input]
constexpr const char* developmentEnable = "developmentEnable";  // [Negative]
constexpr const char* printEnable = "printEnable";              // [Print]
constexpr const char* halationEnable = "halationEnable";        // [Halation]
constexpr const char* bloomEnable = "bloomEnable";              // [Bloom]
constexpr const char* acutanceEnable = "acutanceEnable";        // [Lens & Gate]
constexpr const char* weaveEnable = "weaveEnable";              // [Lens & Gate]
constexpr const char* vignetteEnable = "vignetteEnable";        // [Lens & Gate]
constexpr const char* hdrUnrollEnable = "hdrUnrollEnable";      // [Output]
// ---- v1.1: film damage and film blur ----------------------------------------------------------
constexpr const char* gFilmDamage = "groupFilmDamage";
constexpr const char* damageEnable = "damageEnable";
constexpr const char* dustEnable = "dustEnable";
constexpr const char* dustAmount = "dustAmount";
constexpr const char* dustSize = "dustSize";
constexpr const char* dustNegative = "dustNegative";
constexpr const char* scratchEnable = "scratchEnable";
constexpr const char* scratchAmount = "scratchAmount";
constexpr const char* scratchPersistence = "scratchPersistence";
constexpr const char* scratchEmulsion = "scratchEmulsion";
constexpr const char* scratchNegative = "scratchNegative";
constexpr const char* hairEnable = "hairEnable";
constexpr const char* hairAmount = "hairAmount";
constexpr const char* damageAnimated = "damageAnimated";
constexpr const char* damageSeed = "damageSeed";
constexpr const char* gFilmBlur = "groupFilmBlur";
constexpr const char* filmBlurEnable = "filmBlurEnable";
constexpr const char* filmBlurRadius = "filmBlurRadius";
constexpr const char* filmBlurFrequency = "filmBlurFrequency";
constexpr const char* filmBlurDuration = "filmBlurDuration";
constexpr const char* filmBlurBreathing = "filmBlurBreathing";
constexpr const char* filmBlurChromatic = "filmBlurChromatic";
constexpr const char* filmBlurSeed = "filmBlurSeed";
// ---- v1.2 colour: camera emulation (own module) and print crossover ---------------------------
constexpr const char* gCameraEmulation = "groupCameraEmulation";
constexpr const char* cameraEmulationEnable = "cameraEmulationEnable";
constexpr const char* cameraEmulation = "cameraEmulation";
constexpr const char* cameraEmulationStrength = "cameraEmulationStrength";
constexpr const char* printCrossover = "printCrossover";
// ---- v1.2: film resolution (negative MTF + Integrate) ------------------------------------------
constexpr const char* gFilmResolution = "groupFilmResolution";
constexpr const char* filmResolutionEnable = "filmResolutionEnable";
constexpr const char* integrate = "integrate";
// ---- v1.3 WP-1: modular pipeline (stage exits) --------------------------------------------------
constexpr const char* pipelineStage = "pipelineStage";          // [Look]
constexpr const char* sceneEncoding = "sceneEncoding";          // [Output]
// ---- v1.3 print lab (WP-2): projection, process aim, rendering, stock provenance ---------------
constexpr const char* printLabEnable = "printLabEnable";        // [Print] module switch of the lab block
constexpr const char* projectionWhite = "projectionWhite";
constexpr const char* printAim = "printAim";
constexpr const char* printContrast = "printContrast";
constexpr const char* printRender = "printRender";
constexpr const char* negProvenance = "negProvenance";          // read-only label [Negative]
constexpr const char* printProvenance = "printProvenance";      // read-only label [Print]
// ---- v1.3 Tone: Zones (HDR-palette style), Filmic Contrast, Film Roll-off, Dye Saturation -------
constexpr const char* gTone = "groupTone";
constexpr const char* toneEnable = "toneEnable";                // Zones + Filmic Contrast
constexpr const char* zoneShadows = "zoneShadows";
constexpr const char* zoneShadowsRange = "zoneShadowsRange";
constexpr const char* zoneLights = "zoneLights";
constexpr const char* zoneLightsRange = "zoneLightsRange";
constexpr const char* zoneHighlights = "zoneHighlights";
constexpr const char* zoneHighlightsRange = "zoneHighlightsRange";
constexpr const char* zoneSpecular = "zoneSpecular";
constexpr const char* zoneSpecularRange = "zoneSpecularRange";
constexpr const char* zoneFalloff = "zoneFalloff";
constexpr const char* filmContrast = "filmContrast";
constexpr const char* contrastPivot = "contrastPivot";
constexpr const char* rolloffEnable = "rolloffEnable";
constexpr const char* rolloffStart = "rolloffStart";
constexpr const char* rolloffSoftness = "rolloffSoftness";
constexpr const char* dyeSatEnable = "dyeSatEnable";
constexpr const char* dyeSaturation = "dyeSaturation";
constexpr const char* dyeRichness = "dyeRichness";
constexpr const char* dyeSatBalance = "dyeSatBalance";
// ---- v1.3 ALEXA Rendering: fitted ALEXA Rec.709 display rendering + ALEXA Colour in the film chain ----
constexpr const char* gAlexaRendering = "groupAlexaRendering";
constexpr const char* alexaDisplayEnable = "alexaDisplayEnable"; // Pipeline Stage ALEXA Rec.709 on / off (A/B)
constexpr const char* alexaColourEnable = "alexaColourEnable";
constexpr const char* alexaColour = "alexaColour";
// ---- v1.3 Highlight Latitude: scene-linear clip reconstruction + highlight extension (before Sensor Match) ----
constexpr const char* gHighlightLatitude = "groupHighlightLatitude";
constexpr const char* latitudeEnable = "latitudeEnable";
constexpr const char* latitudeStops = "latitudeStops";
constexpr const char* latitudeSoftness = "latitudeSoftness";
constexpr const char* latitudeReconstruct = "latitudeReconstruct";
constexpr const char* latitudeDesaturate = "latitudeDesaturate";
constexpr const char* latitudeClip = "latitudeClip";
// ---- v1.3 Texture II: grain colour / tonal balance, chroma halation + Aura, Film Breath ----
constexpr const char* grainColour = "grainColour";
constexpr const char* grainShadows = "grainShadows";
constexpr const char* grainMids = "grainMids";
constexpr const char* grainHighlights = "grainHighlights";
constexpr const char* halationSaturation = "halationSaturation";
constexpr const char* halationBackground = "halationBackground";
constexpr const char* auraEnable = "auraEnable";
constexpr const char* auraRadius = "auraRadius";
constexpr const char* auraStrength = "auraStrength";
constexpr const char* auraGreen = "auraGreen";
constexpr const char* gFilmBreath = "groupFilmBreath";
constexpr const char* breathEnable = "breathEnable";
constexpr const char* breathProfile = "breathProfile";       // append only (tex::BreathProfile)
constexpr const char* breathExposure = "breathExposure";
constexpr const char* breathDevelopment = "breathDevelopment";
constexpr const char* breathPrinter = "breathPrinter";
constexpr const char* breathPeriod = "breathPeriod";
constexpr const char* breathSeed = "breathSeed";
// ---- v1.3 P1 extras: user presets, scene white balance, false colour, bloom threshold ----------
constexpr const char* userPresetName = "userPresetName";
constexpr const char* userPresetSave = "userPresetSave";
constexpr const char* userPresetLoad = "userPresetLoad";
constexpr const char* sceneWbEnable = "sceneWbEnable";
constexpr const char* sceneWbTemperature = "sceneWbTemperature";
constexpr const char* sceneWbTint = "sceneWbTint";
constexpr const char* falseColourEnable = "falseColourEnable";
constexpr const char* bloomThreshold = "bloomThreshold";
constexpr const char* bloomKeepHighlights = "bloomKeepHighlights";
} // namespace id

// v1.3 WP-1: options of pipelineStage (append only). 0..3 = core::Stage; the two extra entries are
// shortcuts: Texture Only = Mode Texture Only, Negative Only (clean) = Negative -> Scene with every texture
// module off (as Mode Color Only). ALEXA Rec.709 (appended in v1.3 ALEXA Rendering) = core::Stage::AlexaRec709.
enum class StageChoice : int { Full = 0, NegativeLogScan, NegativeToScene, PrintOnly, TextureOnly, NegativeClean, AlexaRec709, Count };

// LUT sizes offered by lutSize, in option order.
constexpr int kLutSizes[3] = {17, 33, 65};

}} // namespace cel::ofxp
