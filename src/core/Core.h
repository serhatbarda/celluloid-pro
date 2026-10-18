// Reafilm Pro - colour science core (per-pixel, no spatial operations).
// Interface owned by the architect. Host-side implementation in src/core/*.cpp; the per-pixel
// math lives in src/kernels/ColorKernels.h (portable dialect, shared with GPU backends).
// See SPEC.md section 4 for the model and section 6 for the look targets.
#pragma once
#include "../common/Common.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cel { namespace core {

// ---- enumerations: ORDER IS THE OFX CHOICE-PARAM ORDER, append only ----------
enum class InputSpace : int {
    FLog2C = 0,          // Fujifilm F-Log2 C / F-Gamut C   (default)
    FLog2,               // Fujifilm F-Log2 / F-Gamut
    FLog,                // Fujifilm F-Log / F-Gamut
    DaVinciIntermediate, // Blackmagic DaVinci Intermediate / DaVinci Wide Gamut
    ArriLogC3,           // ARRI LogC3 (EI 800) / ARRI Wide Gamut 3
    ArriLogC4,           // ARRI LogC4 / ARRI Wide Gamut 4
    SLog3SGamut3Cine,    // Sony S-Log3 / S-Gamut3.Cine
    VLog,                // Panasonic V-Log / V-Gamut
    CanonLog3,           // Canon Log 3 / Cinema Gamut
    BMDFilmGen5,         // Blackmagic Film Gen 5 / BMD Wide Gamut Gen 5
    ACEScct,             // ACEScct / AP1
    LinearRec2020,       // scene-linear Rec.2020 (testing / pipelines)
    Rec709Video,         // v1.5: display-referred Rec.709 video (camcorder / phone), highlights expanded
    SRGB,                // v1.5: display-referred sRGB (photos / JPEG), highlights expanded
    Count
};

enum class OutputSpace : int {
    Rec709_G24 = 0,      // Rec.709 primaries, gamma 2.4 (default)
    Rec709_sRGB,         // sRGB piecewise EOTF^-1
    P3D65_G26,           // P3-D65, gamma 2.6
    P3DCI_G26,           // DCI-P3 primaries + DCI white encoding, gamma 2.6, D65 white simulated (neutrals stay D65)
    Rec2100_PQ,          // Rec.2020 primaries, ST.2084 PQ, peak = hdrPeakNits
    Rec2100_HLG,         // Rec.2020 primaries, HLG (1000 nit reference)
    // v1.3 P1 extras (appended): SDR display variants. Kernels see the base encoding (Rec709_G24 / P3D65_G26)
    // plus a gamma override (CEL_CS_OUT_GAMMA); D60 sim = neutrals shown at the ACES D60 white
    Rec709_G22,          // Rec.709 primaries, pure gamma 2.2
    P3D65_G22,           // P3-D65, pure gamma 2.2 (Apple / P3 displays in gamma 2.2)
    Rec709_G24_D60,      // Rec.709 gamma 2.4, D60 white simulated (neutrals = D60)
    P3D65_G26_D60,       // P3-D65 gamma 2.6, D60 white simulated
    Count
};

enum class Process : int {
    NegativeToPrint = 0, // negative -> print stock -> projection (default)
    NegativeScan,        // negative -> neutral lab scan (no print stock character)
    Count
};
// Reversal (slide) stocks ignore Process and the print stock: they are direct positives.

enum class Mode : int {
    Full = 0,            // colour + texture
    ColorOnly,           // colour pipeline only (what the LUT export contains)
    TextureOnly,         // input is ALREADY display-referred in OutputSpace; only texture is added
    Count
};

struct LookParams {
    InputSpace  input  = InputSpace::FLog2C;
    OutputSpace output = OutputSpace::Rec709_G24;
    float exposure     = 0.f;   // stops, applied in scene-linear before everything

    // negative / development
    int   negStock     = 0;     // index into negativeStockNames()
    float pushPull     = 0.f;   // stops of development, -2 .. +3
    float interlayer   = 1.f;   // multiplier on the stock's interlayer (colour density) strength, 0 .. 2
    float negBleach    = 0.f;   // bleach bypass on the negative, 0 .. 1
    float neutralize   = 0.f;   // 0 .. 1 removes per-layer curve differences (crossover / tints)

    // print / lab
    Process process    = Process::NegativeToPrint;
    int   printStock   = 0;     // index into printStockNames()
    float printerR     = 0.f;   // printer points, +1 point = +0.025 log exposure on that print layer
    float printerG     = 0.f;   //   (more red light -> more cyan dye -> LESS red in the image; UI explains)
    float printerB     = 0.f;
    float printExposure= 0.f;   // printer points on all three layers; positive = BRIGHTER print (UI convention)
    float printBleach  = 0.f;   // bleach bypass on the print, 0 .. 1

    // output
    float hdrPeakNits  = 1000.f; // PQ only, 300 .. 4000
    float hdrUnroll    = 1.f;    // PQ/HLG: 0 = SDR print look placed in HDR, 1 = full highlight unroll

    // grain amplitude (spatial structure comes from cel::tex). 0 disables.
    float grainAmount  = 0.f;    // 0 .. 3, 1 = stock's nominal granularity

    // ---- v1.2 (appended, SPEC 13): defaults = off, so every v1.1 caller renders unchanged
    int   cameraEmulation = 0;   // CameraEmulation: 0 off, 1 Fujifilm X-Trans -> ARRI ALEXA (scene-linear, before the film)
    float emulationStrength = 1.f; // 0 .. 1 blend of the emulation
    float printCrossover = 0.f;  // -1 .. 1: + = warm highlights / cool shadows on the print (0 = the stock as designed)

    // ---- v1.3 WP-1 (appended, SPEC 13): pipeline stage exits; defaults = the full chain (v1.2 render)
    int   stage = 0;             // core::Stage
    int   sceneEncoding = 0;     // core::SceneEncoding (Negative -> Scene output)
    // ---- v1.3 print lab (appended, SPEC 13; WP-2): defaults = the v1.2 print path, bit-identical
    int   projectionWhite = 0;   // ProjectionWhite: how the projected print's white is shown on the display
    int   printAim = 0;          // PrintAim: process aim of the print development
    int   printRender = 0;       // PrintRender: contact print / release print (IP-IN duplication) / telecine
    float printContrast = 0.f;   // -1 .. 1: print development in process-aim units, added to printAim

    // ---- v1.3 Tone (appended, SPEC 13): defaults = neutral, bit-identical to the render without them
    // Zones (HDR-palette style) + Filmic Contrast: scene-linear, after Exposure, before the negative (SPEC 4.9).
    // Exposure offsets in stops; ranges = zone positions in stops relative to 18 % grey of the scene luminance.
    float zoneShadows = 0.f, zoneShadowsRange = -2.f;       // acts BELOW its range
    float zoneLights = 0.f, zoneLightsRange = 1.f;          // acts ABOVE its range (as Highlights / Specular)
    float zoneHighlights = 0.f, zoneHighlightsRange = 3.f;
    float zoneSpecular = 0.f, zoneSpecularRange = 5.f;
    float zoneFalloff = 1.f;     // stops: width of every zone's transition, 0.25 .. 4
    float filmContrast = 1.f;    // log-exposure contrast about the pivot, 0.5 .. 2 (1 = off)
    float contrastPivot = 0.f;   // stops relative to 18 % grey, -3 .. 3
    // Film Roll-off: the print's highlight shoulder (approach to D-min), Print process only
    float rolloffStart = 3.f;    // stops above grey where the reshaped shoulder begins, 0.5 .. 8
    float rolloffSoftness = 0.f; // -1 (harder, reaches white sooner) .. +1 (softer, longer); 0 = the stock
    // Dye Saturation (subtractive, in the density of the viewed film)
    float dyeSaturation = 1.f;   // 0 .. 2, 1 = the stock
    float dyeRichness = 0.25f;   // 0 .. 1: how much more saturation adds dye (0 = same luminance, more = darker, denser colour)
    float dyeSatBalance = 0.f;   // -1 (shadows) .. +1 (highlights): where the saturation change acts most
    // ---- v1.3 ALEXA Rendering (appended, SPEC 13): defaults = off, bit-identical
    float alexaColour = 0.f;     // 0 .. 1: ALEXA Colour strength (scene-linear, before the negative; 0 = off)
    bool  alexaBlock = false;    // 1.4: build the ALEXA block anyway (the Before / After Wipe shows the source through it)
    // ---- v1.3 Highlight Latitude (appended, SPEC 13): off by default, bit-identical. Scene-linear in the camera's
    // own gamut right after the log decode (before Sensor Match, Exposure, the negative and the ALEXA rendering);
    // Fujifilm F-Log2 C / F-Log2 input only
    bool  latitudeOn = false;
    float latitudeStops = 2.5f;      // 0 .. 4 stops of extra headroom (clip level placed this much higher)
    float latitudeSoftness = 0.4f;   // 0 .. 1: extension width below the clip, 0.5 .. 3 stops
    float latitudeReconstruct = 1.f; // 0 .. 1: clip reconstruction (lagging channels of near-white clips -> white)
    float latitudeDesaturate = 0.3f; // 0 .. 1: desaturation of the stretched top stops toward white
    float latitudeClip = 0.854f;     // F-Log2 code value of the sensor clip (X-H2S: 0.854 = 10-bit 874)
    // ---- v1.3 P1 extras (appended, SPEC 13): defaults = off, bit-identical
    // Scene White Balance: von Kries (CAT16) adaptation in scene light, composed into the input matrix (before
    // Sensor Match, Exposure and the negative). The footage is treated as lit by a Planckian source at
    // sceneWbKelvin (+ tint off the locus) and adapted to 6500 K: higher = warmer, + tint = more magenta.
    bool  sceneWbOn = false;
    float sceneWbKelvin = 6500.f;    // 2000 .. 15000 K
    float sceneWbTint = 0.f;         // -100 .. 100 (x 0.0002 Duv)
    // False Colour: diagnostic exposure zones of the scene luminance (stops rel. to 18 % grey) instead of the
    // rendering. Never part of looks, presets or LUT exports.
    bool  falseColour = false;
    // ---- Lab & Gate: colour timer (appended, SPEC 13): defaults = off, bit-identical. Print process only.
    // Colour Head: dichroic C / M / Y filtration of the printer light in CC (1 CC = 0.01 density), relative to the
    // balanced filter pack; integrated over the printer lamp, the grey negative and the print layers' sensitivities.
    float headCyan = 0.f, headMagenta = 0.f, headYellow = 0.f;   // -60 .. 60 CC
    float headPreserve = 1.f;    // 0 .. 1: share of the exposure change the filtration causes that is compensated
    // Print Black / Print White / De-Fog: density at the print's ends (after the grey balance, not re-timed)
    float printBlack = 0.f;      // -1.5 .. +0.25: density added to the print's D-max end (- = milkier blacks)
    float printWhite = 0.f;      // -0.15 .. +0.40: density added at the D-min end (+ = greyer, dimmer whites)
    float printDefog = 0.f;      // 0 .. 1: share of the print's fog density removed from the white end
    // Print Toning: dye toning of the print's shadows / highlights (hue in degrees of the image colour, 0 = red)
    float toneShadowHue = 220.f, toneShadowAmount = 0.f;   // amount 0 .. 1 (1 = 0.10 density)
    float toneHighlightHue = 50.f, toneHighlightAmount = 0.f;
    // v1.8 darkroom: cross-processing (a reversal stock developed as a negative, then printed or scanned)
    bool  crossProcess = false;
    // v1.8 Shot Under / Over: the scene was exposed -pushPull stops (rated speed) and the lab timed the print for it
    bool  pushRated = false;
    // v1.8 Expired: film past its date, 0 .. 1 (base fog strongest in the fast blue layer, speed and contrast
    // loss, coarser grain); 1 = a roll some ten years out of date, stored warm
    float filmAge = 0.f;
    // v1.11 Exposure Zebras (diagnostic overlay, never in looks, presets or LUTs): stripes over the rendering where
    // this look prints the shadows as solid black (blue) or the highlights as paper white (red)
    bool  exposureZebras = false;
};

// v1.3 print lab choices (ORDER IS THE OFX CHOICE-PARAM ORDER, append only); model in docs/STOCKS.md 5c.1
enum class ProjectionWhite : int {
    D65 = 0,             // projector white adapted to D65 (CAT16), the v1.2 rendering (default)
    TheatreXenon,        // unadapted: the xenon projector's own white, as measured off the screen
    D60Sim,              // adapted to the ACES D60 white (grade-room simulation), display-fitted
    Count
};
enum class PrintAim : int {
    Nominal = 0,         // print developed at the process aim (default)
    Rich,                // high in the lab's tolerance: more contrast and D-max
    Soft,                // low in the lab's tolerance: less contrast and D-max
    Count
};
enum class PrintRender : int {
    ContactPrint = 0,    // printed from the camera negative, projected (default)
    ReleasePrint,        // printed from a duplicate negative (negative -> IP -> IN -> print), projected
    Telecine,            // the print transferred on a telecine / scanner: neutral axis, black at D-max
    Count
};

// v1.3 WP-1: where the colour chain starts and stops (ORDER = append only). Reversal stocks have no
// negative stage: every exit renders the full chain for them. Every exit uses the print path's tables
// (Process is ignored), see SPEC 4.7.
enum class Stage : int {
    Full = 0,            // scene -> negative -> print / scan -> display (v1.2)
    NegativeLogScan,     // scene -> negative -> Cineon-style printing-density code values (Log Scan)
    NegativeToScene,     // scene -> negative -> re-linearised scene, encoded in SceneEncoding (for an own DRT)
    PrintOnly,           // input = Log Scan code values -> print -> display (OutputSpace)
    AlexaRec709,         // v1.3: scene -> ALEXA Rec.709 display rendering (no film; textures display-referred)
    Count
};
enum class SceneEncoding : int {
    LogC4_AWG4 = 0,      // ARRI LogC4 / ARRI Wide Gamut 4 (default)
    LogC3_AWG3,          // ARRI LogC3 (EI 800) / ARRI Wide Gamut 3
    ACEScct_AP1,         // ACEScct / AP1
    DaVinciIntermediate, // DaVinci Intermediate / DaVinci Wide Gamut
    LinearRec2020,       // scene-linear Rec.2020
    Count
};
std::vector<std::string> stageNames();           // UI labels, same order as Stage
std::vector<std::string> sceneEncodingNames();   // same order as SceneEncoding

enum class CameraEmulation : int {
    Off = 0,
    FujiXTransToAlexa,          // Fujifilm X-Trans (X-H2S; X-T4 data) -> ARRI ALEXA / AMIRA (ALEV III)
    Count
};

struct NegativeStockInfo {
    std::string name;
    std::string description;   // one line, shown in docs/tooltips
    bool  reversal;            // true = slide film, direct positive
    bool  monochrome;          // true = black & white
    float grainSizeScale;      // relative grain size (1 = 200-speed 35mm reference)
    float halationScale;       // 1 = normal anti-halation backing, >1 = weak/no backing
    float bloomScale;
    std::string provenance;    // v1.3: data source (data sheet + year / designed / style approximation), for the UI
};

std::vector<std::string> inputSpaceNames();      // UI labels, same order as InputSpace
std::vector<std::string> outputSpaceNames();     // same order as OutputSpace
std::vector<NegativeStockInfo> negativeStocks(); // order = negStock index
std::vector<std::string> printStockNames();      // order = printStock index
std::vector<std::string> printStockProvenance(); // v1.3: same order, data source of each print stock
std::vector<std::string> projectionWhiteNames(); // v1.3: same order as ProjectionWhite
std::vector<std::string> printAimNames();        // v1.3: same order as PrintAim
std::vector<std::string> printRenderNames();     // v1.3: same order as PrintRender
// Lab & Gate: the Colour Head filtration (CC) as printer points per print layer (R, G, B; 1 point = 0.025
// log E; before Preserve Exposure), for the given stock pair. The lab-lights display of the UI.
void colourHeadPoints(int negStock, int printStock, float cyan, float magenta, float yellow, float out[3]);

class Pipeline {
public:
    Pipeline();
    // Precompute everything that depends on params (stock tables, spectral bakes into 3D LUTs,
    // matrices, grey balance) into ONE float buffer (see state()). Expensive bakes must be cached
    // internally (e.g. keyed by stock pair) so dragging printer points/exposure stays interactive
    // (< 2 ms when only cheap params change). Not thread-safe; call before rendering.
    void prepare(const LookParams& p);
    const LookParams& params() const { return m_p; }
    const NegativeStockInfo& negStock() const;

    // The kernel `state` buffer (header + colour state + LUTs), layout private to core and read
    // by the colour kernels in src/kernels/ColorKernels.h. stateVersion() changes whenever the
    // content changes (GPU backends re-upload only then).
    const std::vector<float>& state() const { return m_state; }
    uint64_t stateVersion() const { return m_version; }

    // ---- CPU convenience wrappers around the SAME kernel functions (tests, LUT export) ----
    Float3 decodeToScene(Float3 encoded) const;
    Float3 sceneToOutput(Float3 sceneLinear, Float3 grainNoise) const;
    Float3 processColor(Float3 encoded) const;      // decode + sceneToOutput, no grain

private:
    LookParams m_p;
    std::vector<float> m_state;
    uint64_t m_version = 0;
    int m_negIndex = 0;
    struct Cache;                                  // bake cache (core-private)
    std::shared_ptr<Cache> m_cache;
};

// Write an Adobe/Resolve .cube 3D LUT of processColor() over [0,1]^3 input code values
// (Linear Rec.2020 input: with a 1D log2 shaper in the same file, see Core.cpp).
// size: 17, 33 or 65. Returns false and fills err on failure.
bool writeCubeLUT(const Pipeline& pipe, int size, const std::string& path, const std::string& title, std::string* err);

}} // namespace cel::core
