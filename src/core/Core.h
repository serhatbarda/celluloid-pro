// Celluloid Pro - colour science core (per-pixel, no spatial operations).
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
    Count
};

enum class OutputSpace : int {
    Rec709_G24 = 0,      // Rec.709 primaries, gamma 2.4 (default)
    Rec709_sRGB,         // sRGB piecewise EOTF^-1
    P3D65_G26,           // P3-D65, gamma 2.6
    P3DCI_G26,           // DCI-P3 primaries + DCI white encoding, gamma 2.6, D65 white simulated (neutrals stay D65)
    Rec2100_PQ,          // Rec.2020 primaries, ST.2084 PQ, peak = hdrPeakNits
    Rec2100_HLG,         // Rec.2020 primaries, HLG (1000 nit reference)
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
};

struct NegativeStockInfo {
    std::string name;
    std::string description;   // one line, shown in docs/tooltips
    bool  reversal;            // true = slide film, direct positive
    bool  monochrome;          // true = black & white
    float grainSizeScale;      // relative grain size (1 = 200-speed 35mm reference)
    float halationScale;       // 1 = normal anti-halation backing, >1 = weak/no backing
    float bloomScale;
};

std::vector<std::string> inputSpaceNames();      // UI labels, same order as InputSpace
std::vector<std::string> outputSpaceNames();     // same order as OutputSpace
std::vector<NegativeStockInfo> negativeStocks(); // order = negStock index
std::vector<std::string> printStockNames();      // order = printStock index

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
