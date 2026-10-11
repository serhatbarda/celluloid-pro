// Celluloid Pro - colour science core: name lists, Pipeline::prepare (state assembly, grey
// balance, HDR calibration), CPU wrappers around the kernel functions, .cube export.
// The model is documented in SPEC section 4, docs/STOCKS.md and reference/model.py.
#include "Core.h"
#include "Bake.h"
#include "../kernels/ColorKernels.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <locale>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace cel { namespace core {

namespace {
#include "CameraEmulation.inc"
}

using namespace bake;
namespace st = stocks;

// ------------------------------------------------------------------------- names
std::vector<std::string> inputSpaceNames() {
    return {"Fujifilm F-Log2 C / F-Gamut C", "Fujifilm F-Log2 / F-Gamut", "Fujifilm F-Log / F-Gamut",
            "DaVinci Intermediate / DaVinci Wide Gamut", "ARRI LogC3 (EI 800) / ARRI Wide Gamut 3",
            "ARRI LogC4 / ARRI Wide Gamut 4", "Sony S-Log3 / S-Gamut3.Cine", "Panasonic V-Log / V-Gamut",
            "Canon Log 3 / Cinema Gamut", "Blackmagic Film Gen 5 / BMD Wide Gamut Gen 5", "ACEScct / AP1",
            "Linear Rec.2020"};
}
std::vector<std::string> outputSpaceNames() {
    return {"Rec.709 (Gamma 2.4)", "Rec.709 (sRGB)", "P3-D65 (Gamma 2.6)", "DCI-P3 (Gamma 2.6, D65 sim)",
            "Rec.2100 PQ (HDR)", "Rec.2100 HLG (HDR)"};
}
std::vector<std::string> stageNames() {
    return {"Full Chain", "Negative -> Log Scan", "Negative -> Scene", "Print Only"};
}
std::vector<std::string> sceneEncodingNames() {
    return {"ARRI LogC4 / ARRI Wide Gamut 4", "ARRI LogC3 (EI 800) / ARRI Wide Gamut 3", "ACEScct / AP1",
            "DaVinci Intermediate / DaVinci Wide Gamut", "Linear Rec.2020"};
}
std::vector<NegativeStockInfo> negativeStocks() {
    std::vector<NegativeStockInfo> v;
    for (int i = 0; i < st::numNegatives(); ++i) {
        const st::NegDef& n = st::negative(i);
        NegativeStockInfo s;
        s.name = n.name;
        s.description = n.description;
        s.reversal = n.kind == st::kReversal;
        s.monochrome = n.kind == st::kBW;
        s.grainSizeScale = float(n.grainSizeScale);
        s.halationScale = float(n.halationScale);
        s.bloomScale = float(n.bloomScale);
        s.provenance = n.provenance ? n.provenance : "";
        v.push_back(s);
    }
    return v;
}
std::vector<std::string> printStockNames() {
    std::vector<std::string> v;
    for (int i = 0; i < st::numPrints(); ++i) v.push_back(st::print(i).name);
    return v;
}
// v1.3 print lab (WP-2): provenance and choice labels
std::vector<std::string> printStockProvenance() {
    std::vector<std::string> v;
    for (int i = 0; i < st::numPrints(); ++i) v.push_back(st::print(i).provenance ? st::print(i).provenance : "");
    return v;
}
std::vector<std::string> projectionWhiteNames() { return {"D65 (Adapted)", "Theatre Xenon (Unadapted)", "D60 Sim"}; }
std::vector<std::string> printAimNames() { return {"Nominal", "Rich", "Soft"}; }
std::vector<std::string> printRenderNames() { return {"Contact Print", "Release Print (IP / IN)", "Telecine / Scan"}; }

// ------------------------------------------------------------------------- pipeline
static std::atomic<uint64_t> gStateVersion{0};   // process-wide: versions never collide between instances

struct Pipeline::Cache {
    bool valid = false;
    LookParams last;
    std::shared_ptr<const ExposureBake> exp;
    std::shared_ptr<const DyeLutBake> dye;        // print or scan LUT
    std::shared_ptr<const ProjBake> proj;         // projection (print or reversal)
    std::vector<NegativeStockInfo> infos;
    struct HoldEntry {                            // hue-hold anchor (depends on the film, not on
        LookParams key;                           // printer points / exposure / output / grain)
        std::vector<float> anchor;
    };
    std::vector<HoldEntry> hold;                  // most recently used first (switching between
                                                  // a few looks stays a cache hit)
};
static constexpr size_t kHoldCacheEntries = 8;

static bool sameFilm(const LookParams& a, const LookParams& b) {
    return a.negStock == b.negStock && a.pushPull == b.pushPull && a.interlayer == b.interlayer &&
           a.negBleach == b.negBleach && a.neutralize == b.neutralize && a.process == b.process &&
           a.printStock == b.printStock && a.printBleach == b.printBleach && a.printCrossover == b.printCrossover &&
           a.projectionWhite == b.projectionWhite && a.printAim == b.printAim && a.printRender == b.printRender &&
           a.printContrast == b.printContrast;   // v1.3 print lab
}

static bool sameParams(const LookParams& a, const LookParams& b) {
    return a.input == b.input && a.output == b.output && a.exposure == b.exposure && a.negStock == b.negStock &&
           a.pushPull == b.pushPull && a.interlayer == b.interlayer && a.negBleach == b.negBleach &&
           a.neutralize == b.neutralize && a.process == b.process && a.printStock == b.printStock &&
           a.printerR == b.printerR && a.printerG == b.printerG && a.printerB == b.printerB &&
           a.printExposure == b.printExposure && a.printBleach == b.printBleach && a.hdrPeakNits == b.hdrPeakNits &&
           a.hdrUnroll == b.hdrUnroll && a.grainAmount == b.grainAmount && a.cameraEmulation == b.cameraEmulation &&
           a.emulationStrength == b.emulationStrength && a.printCrossover == b.printCrossover && a.stage == b.stage &&
           a.sceneEncoding == b.sceneEncoding &&
           a.projectionWhite == b.projectionWhite && a.printAim == b.printAim && a.printRender == b.printRender &&
           a.printContrast == b.printContrast;   // v1.3 print lab
}

static float clampf(float v, float lo, float hi) { return std::isfinite(v) ? std::min(std::max(v, lo), hi) : lo; }

static LookParams sanitize(LookParams p) {
    if (int(p.input) < 0 || int(p.input) >= int(InputSpace::Count)) p.input = InputSpace::FLog2C;
    if (int(p.output) < 0 || int(p.output) >= int(OutputSpace::Count)) p.output = OutputSpace::Rec709_G24;
    if (int(p.process) < 0 || int(p.process) >= int(Process::Count)) p.process = Process::NegativeToPrint;
    p.negStock = std::min(std::max(p.negStock, 0), st::numNegatives() - 1);
    p.printStock = std::min(std::max(p.printStock, 0), st::numPrints() - 1);
    p.exposure = std::isfinite(p.exposure) ? clampf(p.exposure, -8.f, 8.f) : 0.f;
    p.pushPull = std::isfinite(p.pushPull) ? clampf(p.pushPull, -2.f, 3.f) : 0.f;
    p.interlayer = std::isfinite(p.interlayer) ? clampf(p.interlayer, 0.f, 2.f) : 1.f;
    p.negBleach = clampf(p.negBleach, 0.f, 1.f);
    p.neutralize = clampf(p.neutralize, 0.f, 1.f);
    p.printerR = std::isfinite(p.printerR) ? clampf(p.printerR, -12.f, 12.f) : 0.f;
    p.printerG = std::isfinite(p.printerG) ? clampf(p.printerG, -12.f, 12.f) : 0.f;
    p.printerB = std::isfinite(p.printerB) ? clampf(p.printerB, -12.f, 12.f) : 0.f;
    p.printExposure = std::isfinite(p.printExposure) ? clampf(p.printExposure, -12.f, 12.f) : 0.f;
    p.printBleach = clampf(p.printBleach, 0.f, 1.f);
    p.hdrPeakNits = std::isfinite(p.hdrPeakNits) ? clampf(p.hdrPeakNits, 300.f, 4000.f) : 1000.f;
    p.hdrUnroll = std::isfinite(p.hdrUnroll) ? clampf(p.hdrUnroll, 0.f, 1.f) : 1.f;
    p.grainAmount = clampf(p.grainAmount, 0.f, 3.f);
    if (p.cameraEmulation < 0 || p.cameraEmulation >= int(CameraEmulation::Count)) p.cameraEmulation = 0;
    p.emulationStrength = clampf(p.emulationStrength, 0.f, 1.f);
    p.printCrossover = std::isfinite(p.printCrossover) ? clampf(p.printCrossover, -1.f, 1.f) : 0.f;
    // v1.3 stage exits: reversal stocks have no negative stage (full chain); every exit uses the print
    // path's tables (printing density is defined by the print stock), so Process is normalised to Print
    if (p.stage < 0 || p.stage >= int(Stage::Count)) p.stage = 0;
    if (p.sceneEncoding < 0 || p.sceneEncoding >= int(SceneEncoding::Count)) p.sceneEncoding = 0;
    if (st::negative(p.negStock).kind == st::kReversal) p.stage = 0;
    if (p.stage != 0) p.process = Process::NegativeToPrint;
    // v1.3 print lab
    if (p.projectionWhite < 0 || p.projectionWhite >= int(ProjectionWhite::Count)) p.projectionWhite = 0;
    if (p.printAim < 0 || p.printAim >= int(PrintAim::Count)) p.printAim = 0;
    if (p.printRender < 0 || p.printRender >= int(PrintRender::Count)) p.printRender = 0;
    p.printContrast = std::isfinite(p.printContrast) ? clampf(p.printContrast, -1.f, 1.f) : 0.f;
    return p;
}

static spec::Gamut inputGamut(InputSpace s) {
    switch (s) {
    case InputSpace::FLog2C: return spec::Gamut::FGamutC;
    case InputSpace::FLog2: case InputSpace::FLog: return spec::Gamut::FGamut;
    case InputSpace::DaVinciIntermediate: return spec::Gamut::DWG;
    case InputSpace::ArriLogC3: return spec::Gamut::AWG3;
    case InputSpace::ArriLogC4: return spec::Gamut::AWG4;
    case InputSpace::SLog3SGamut3Cine: return spec::Gamut::SGamut3Cine;
    case InputSpace::VLog: return spec::Gamut::VGamut;
    case InputSpace::CanonLog3: return spec::Gamut::CinemaGamut;
    case InputSpace::BMDFilmGen5: return spec::Gamut::BMDWG5;
    case InputSpace::ACEScct: return spec::Gamut::AP1;
    default: return spec::Gamut::Rec2020;
    }
}

static spec::Mat3 outputMatrix(OutputSpace o) {
    spec::Gamut g = spec::Gamut::Rec2020;
    if (o == OutputSpace::Rec709_G24 || o == OutputSpace::Rec709_sRGB) g = spec::Gamut::Rec709;
    else if (o == OutputSpace::P3D65_G26) g = spec::Gamut::P3D65;
    else if (o == OutputSpace::P3DCI_G26) g = spec::Gamut::DCIP3;
    spec::Mat3 M = spec::npm(g).inverse();
    if (o == OutputSpace::P3DCI_G26) {   // D65 white simulated inside the DCI white encoding
        spec::Vec3 w = M * spec::xyToXYZ(0.3127, 0.3290);
        double mx = std::max({w[0], w[1], w[2]});
        for (auto& r : M.m) for (double& v : r) v /= mx;
    }
    return M;
}

static void putMat(float* s, int o, const spec::Mat3& M) {
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) s[o + i * 3 + j] = float(M.m[i][j]);
}

// The initial state uses the product default negative (SPEC section 4.6: CN 200T Studio Tungsten, index 1;
// LookParams' field default in the fixed Core.h is index 0), so the first render of a new OFX
// instance with default settings is a bake-cache hit.
static LookParams productDefaultLook() {
    LookParams p;
    p.negStock = 1;
    return p;
}

Pipeline::Pipeline() : m_cache(std::make_shared<Cache>()) { prepare(productDefaultLook()); }

const NegativeStockInfo& Pipeline::negStock() const {
    if (m_cache->infos.empty()) m_cache->infos = negativeStocks();
    return m_cache->infos[size_t(m_negIndex)];
}

namespace {
struct Grey {
    static CelF3 film(const std::vector<float>& s, float g) {
        CelcFilm f = celc_film_xyz(s.data(), cel_f3(g, g, g), cel_f3(0, 0, 0), 0);
        return f.X;
    }
};

// v1.3 monotone black (reference/model.py bake_black_monotone, ColorState.h NT block). Below grey a print's layers
// reach their D-max at different rates, so the projected neutral turns bluer / greener in the last stops before
// black while its luminance hardly rises; a display channel with a negative matrix weight can then fall slightly
// as the scene gets brighter. The display-linear neutral ramp v = Mout XYZ is made rising from black to grey
// and (v_mono - v) / Y is stored per reference exposure. Off (BLACK_N = 0, bit-identical output) unless
// the ramp drops by more than kBlackOn relative.
void bakeBlackMonotone(float* s, const std::vector<CelF3>& X, float xr0, int off) {
    constexpr double kBlackOn = 1e-4;
    const int n = int(X.size());
    std::vector<double> v(size_t(3) * size_t(n));
    for (int k = 0; k < n; ++k) {
        CelF3 r = celc_mat3(s, CEL_CS_MOUT, X[size_t(k)]);
        v[size_t(3 * k)] = r.x; v[size_t(3 * k + 1)] = r.y; v[size_t(3 * k + 2)] = r.z;
    }
    int kg = -1;
    for (int k = 0; k < n; ++k)
        if (CEL_HOLD_X0 + (CEL_HOLD_X1 - CEL_HOLD_X0) * double(k) / double(n - 1) <= double(xr0)) kg = k;
    if (kg < 1) return;
    double worst = 0.0, run[3] = {v[size_t(3 * kg)], v[size_t(3 * kg + 1)], v[size_t(3 * kg + 2)]};
    for (int k = kg - 1; k >= 0; --k)
        for (int c = 0; c < 3; ++c) {
            run[c] = std::min(run[c], v[size_t(3 * (k + 1) + c)]);
            double a = v[size_t(3 * k + c)];
            worst = std::max(worst, (a - run[c]) / std::max(std::fabs(a), 1e-12));
        }
    if (worst <= kBlackOn) return;
    std::vector<double> vm(v);
    for (int k = std::min(kg, n - 2); k >= 0; --k)
        for (int c = 0; c < 3; ++c) {
            // fold the ramp: it falls by |v[k+1] - v[k]| per sample towards black (its own fall, or the mirrored
            // step where it turns up); the offset stays within twice the reversal, noise in D-max moves it by noise
            vm[size_t(3 * k + c)] = vm[size_t(3 * (k + 1) + c)] - std::fabs(v[size_t(3 * (k + 1) + c)] - v[size_t(3 * k + c)]);
        }
    bool any = false;
    for (int k = 0; k < n; ++k) {
        double iy = 1.0 / std::max(double(X[size_t(k)].y), 1e-12);
        for (int c = 0; c < 3; ++c) {
            float d = float((vm[size_t(3 * k + c)] - v[size_t(3 * k + c)]) * iy);
            s[off + c * n + k] = d;
            any = any || d != 0.f;
        }
    }
    if (!any) return;
    s[CEL_CS_BLACK_OFF] = float(off);
    s[CEL_CS_BLACK_N] = float(n);
}
}

// Print Crossover (v1.2, SPEC 4.4): the classic print crossover - slightly different contrast of the print's
// layers around the middle of the scale (the trait of the owner's 2383 references: warm highlights, cool
// shadows). Each layer's density table is scaled about the mid density Dm:
//   g_j = 1 + c a_j kPrintXoverGamma,  a = (+1, 0, -1) for the R- (cyan), G-, B- (yellow) layer,
//   x = (D - Dm) / (E - Dm) in [0, 1] toward the layer's end E (fog below Dm, D-max above),
//   D' = Dm + (E - Dm) g x / (1 + (g - 1) x)
// i.e. slope g at the pivot, the layer's own fog and D-max kept, strictly monotonic for any g > 0. (v1.2 fix:
// the first version scaled linearly and clamped at fog / D-max; the steeper layer then reached its fog stops
// earlier than the others, and the display red of a warm grey ramp turned down by ~4e-5 near +5 stops.)
// c > 0: less cyan / more yellow below Dm (scene highlights, warm), more cyan / less yellow above it (shadows,
// cool); the effect fades out toward fog and D-max. The grey balance solved afterwards keeps 18 % grey neutral.
// c = 0 leaves the table untouched (bit-identical).
// == reference/model.py print_crossover
static void printCrossover(float* pc, float c, bool mono) {
    if (mono) return;
    static const double a[3] = {1.0, 0.0, -1.0};
    for (int j = 0; j < 3; ++j) {
        const double g = 1.0 + double(c) * a[j] * kPrintXoverGamma;
        if (g == 1.0) continue;
        float* t = pc + size_t(j) * kNCurve;
        const double lo = t[0], hi = t[kNCurve - 1], dm = kPrintXoverDm;
        for (int i = 0; i < kNCurve; ++i) {
            const double D = t[i];
            const double end = D < dm ? lo : hi, span = dm - end;
            double r = D;
            if (D != dm && std::fabs(span) > 1e-9) {
                const double x = (dm - D) / span;
                r = dm - span * (g * x / (1.0 + (g - 1.0) * x));
            }
            t[i] = float(std::min(std::max(r, std::min(lo, hi)), std::max(lo, hi)));
        }
    }
}

// ---- v1.3 stage exits (SPEC 4.7) --------------------------------------------------------------------
// The neutral stage axis: for a neutral scene at log10 level X (knots over [grey - 4, grey + 3]) the printing
// density of each print layer (Log Scan, rows 0..2, made strictly increasing) and the scene-referred level
// Y(X) it maps back to (row 3). Y follows the scene exactly (Y = X) wherever the G layer still has at least
// a quarter of its mid-scale gamma and compresses smoothly where the film has lost the information (deep
// toe, extreme shoulder): dY/dX = r / (1 + r^4)^(1/4), r = slope / (0.25 gamma_mid), floored at 1e-3. Every
// channel maps through the same Y, so neutrals come back neutral at every level (grey-balanced inverse),
// finite in the toe, and the film's own colour (layer crosstalk, interlayer, curve differences between the
// layers away from neutral, grain, halation in exposure) stays in. == the kernel's celc_ns_* lookups.
static constexpr int kStageN = 1401;               // 0.005 log10 per knot, grey is knot 800
static constexpr double kStageX0 = kLog10Grey - 4.0, kStageX1 = kLog10Grey + 3.0;
static constexpr double kStageGammaMin = 0.25;     // share of the mid-scale gamma below which Y compresses

static spec::Gamut sceneGamut(SceneEncoding e) {
    switch (e) {
    case SceneEncoding::LogC3_AWG3: return spec::Gamut::AWG3;
    case SceneEncoding::ACEScct_AP1: return spec::Gamut::AP1;
    case SceneEncoding::DaVinciIntermediate: return spec::Gamut::DWG;
    case SceneEncoding::LinearRec2020: return spec::Gamut::Rec2020;
    default: return spec::Gamut::AWG4;
    }
}
static int sceneTransfer(SceneEncoding e) {
    switch (e) {
    case SceneEncoding::LogC3_AWG3: return CEL_IN_LOGC3;
    case SceneEncoding::ACEScct_AP1: return CEL_IN_ACESCCT;
    case SceneEncoding::DaVinciIntermediate: return CEL_IN_DAVINCI;
    case SceneEncoding::LinearRec2020: return CEL_IN_LINEAR;
    default: return CEL_IN_LOGC4;
    }
}

static void buildStageAxis(float* s, const LookParams& p, int oNS, int oPL, int plN, float plMax) {
    const int N = kStageN;
    s[CEL_CS_STAGE] = float(p.stage);
    s[CEL_CS_NS_OFF] = float(oNS);
    s[CEL_CS_NS_N] = float(N);
    s[CEL_CS_NS_X0] = float(kStageX0);
    s[CEL_CS_NS_X1] = float(kStageX1);
    const CelF3 hc = celc_lut3(s, oPL, plN, plMax, cel_f3(s[CEL_CS_DMIN], s[CEL_CS_DMIN + 1], s[CEL_CS_DMIN + 2]));
    s[CEL_CS_HCLEAR] = hc.x; s[CEL_CS_HCLEAR + 1] = hc.y; s[CEL_CS_HCLEAR + 2] = hc.z;
    float* T = s + oNS;
    const double dx = (kStageX1 - kStageX0) / (N - 1);
    for (int k = 0; k < N; ++k) {
        const float g = float(std::pow(10.0, kStageX0 + dx * k));
        CelcNeg ng = celc_negative(s, cel_f3(g, g, g), cel_f3(0, 0, 0), 0);
        CelF3 pd = celc_printing_density(s, ng);
        for (int j = 0; j < 3; ++j) {
            float v = celc_comp(pd, j);
            if (k > 0) v = std::max(v, T[j * N + k - 1] + 1e-5f);   // strictly increasing (flat toe / shoulder)
            T[j * N + k] = v;
        }
    }
    const int kg = int(std::lround((kLog10Grey - kStageX0) / dx));
    const int k1 = std::min(N - 1, kg + 30), k0 = std::max(0, kg - 30);
    const double gmid = std::max(1e-3, double(T[N + k1] - T[N + k0]) / ((k1 - k0) * dx));
    auto slope = [&](int k) {   // dY/dX on the segment k .. k+1 (G layer)
        const double r = double(T[N + k + 1] - T[N + k]) / dx / (kStageGammaMin * gmid);
        return std::max(1e-3, r / std::pow(1.0 + r * r * r * r, 0.25));
    };
    std::vector<double> Y(static_cast<size_t>(N));
    Y[size_t(kg)] = kStageX0 + dx * kg;
    for (int k = kg; k < N - 1; ++k) Y[size_t(k + 1)] = Y[size_t(k)] + dx * slope(k);
    for (int k = kg; k > 0; --k) Y[size_t(k - 1)] = Y[size_t(k)] - dx * slope(k - 1);
    for (int k = 0; k < N; ++k) T[3 * N + k] = float(Y[size_t(k)]);
    for (int k = 1; k < N; ++k) T[3 * N + k] = std::max(T[3 * N + k], T[3 * N + k - 1] + 1e-6f);
    // Negative -> Scene output encoding
    const SceneEncoding enc = SceneEncoding(p.sceneEncoding);
    s[CEL_CS_SCENE_TF] = float(sceneTransfer(enc));
    putMat(s, CEL_CS_MSCENE, spec::rgbToRgb(spec::Gamut::Rec2020, sceneGamut(enc)));
    // First-order colour masking: the film layers' own primaries (spectral sensitivities and dye crosstalk)
    // are taken back to scene colorimetry by the inverse of the log-log Jacobian at 18 % grey of the chain
    // WITHOUT interlayer effects, rows normalised to 1 (neutrals unchanged). What stays is the film's colour:
    // interlayer (Color Density) saturation, the non-linear crosstalk of saturated colours, the curve shapes.
    // Applied to the log10 levels (celc_ns_scene), so it never produces negative light.
    spec::Mat3 Mm;
    const bool mono = celc_i(s, CEL_CS_MONO) != 0;
    s[CEL_CS_MASK_ON] = 0.f;   // the Jacobian is measured unmasked
    if (!mono) {
        float inter[9];
        for (int i = 0; i < 9; ++i) { inter[i] = s[CEL_CS_INTER + i]; s[CEL_CS_INTER + i] = 0.f; }
        const float d = 0.02f, up = float(std::pow(10.0, double(d))), g = 0.18f;
        spec::Mat3 J;
        for (int k = 0; k < 3; ++k) {
            float a[3] = {g, g, g}, b[3] = {g, g, g};
            a[k] *= up; b[k] /= up;
            CelF3 A = celc_ns_scene(s, celc_printing_density(s, celc_negative(s, cel_f3(a[0], a[1], a[2]), cel_f3(0, 0, 0), 0)));
            CelF3 B = celc_ns_scene(s, celc_printing_density(s, celc_negative(s, cel_f3(b[0], b[1], b[2]), cel_f3(0, 0, 0), 0)));
            for (int j = 0; j < 3; ++j)
                J.m[j][k] = std::log10(std::max(double(celc_comp(A, j)), 1e-12) / std::max(double(celc_comp(B, j)), 1e-12)) / (2.0 * double(d));
        }
        for (int i = 0; i < 9; ++i) s[CEL_CS_INTER + i] = inter[i];
        spec::Mat3 Ji = J.inverse();
        bool ok = true;
        for (int i = 0; i < 3 && ok; ++i) {
            const double sum = Ji.m[i][0] + Ji.m[i][1] + Ji.m[i][2];
            ok = std::isfinite(sum) && std::fabs(sum) > 0.1;
            if (ok) for (int j = 0; j < 3; ++j) Ji.m[i][j] /= sum;
        }
        if (ok) Mm = Ji;
    }
    putMat(s, CEL_CS_MMASK, Mm);
    putMat(s, CEL_CS_MMASKI, Mm.inverse());
    s[CEL_CS_MASK_ON] = mono ? 0.f : 1.f;
}

void Pipeline::prepare(const LookParams& pin) {
    const LookParams p = sanitize(pin);
    Cache& C = *m_cache;
    if (C.valid && sameParams(p, C.last)) return;        // nothing changed: same state, same version

    const int ni = p.negStock, pi = p.printStock;
    const st::NegDef& neg = st::negative(ni);
    const st::PrintDef& prt = st::print(pi);
    const bool mono = neg.kind == st::kBW;
    const bool rev = neg.kind == st::kReversal;
    const int kind = rev ? 2 : (p.process == Process::NegativeScan ? 1 : 0);
    m_negIndex = ni;

    // ---- cached bakes
    auto exp = exposureLut(ni);
    std::shared_ptr<const DyeLutBake> dye;
    std::shared_ptr<const ProjBake> proj;
    if (kind == 0) {
        PrintVariant pv;   // v1.3 print lab (WP-2)
        pv.white = p.projectionWhite; pv.aim = p.printAim; pv.render = p.printRender; pv.contrast = p.printContrast;
        dye = printLut(ni, pi, pv.render);
        proj = projectionLut(pi, mono, pv);
    }
    else if (kind == 1) dye = scanLut(ni);
    else proj = reversalLut(ni);

    // ---- layout
    const size_t nOct = exp->oct.data.size();
    const size_t nNC = size_t(3) * kNCurve;
    const size_t nPL = dye ? dye->lut.data.size() : 0;
    const size_t nPC = kind == 2 ? 0 : size_t(3) * kNCurve;
    const size_t nPJ = proj ? proj->lut.data.size() : (kind == 1 ? size_t(3) * kNCurve : 0);   // scan: inversion LUT
    const size_t oOct = CEL_CS_HEADER, oNC = oOct + nOct, oPL = oNC + nNC, oPC = oPL + nPL, oPJ = oPC + nPC;
    // hue hold (ColorState.h HA / HN blocks)
    double holdStrength = 0.0, holdChroma = 0.0;
    if (!mono) {
        if (kind == 0) { holdStrength = prt.hueHold; holdChroma = prt.chromaHold; }
        else if (kind == 2) { holdStrength = neg.hueHold; holdChroma = neg.chromaHold; }
        else { holdStrength = st::scanner().hueHold; holdChroma = st::scanner().chromaHold; }
    }
    const bool hold = holdStrength > 0.0;
    const size_t nHA = hold ? size_t(kHoldN) * kHoldN * 4 : 0, nHN = hold ? size_t(3) * kHoldNX : 0;
    const size_t oHA = oPJ + nPJ, oHN = oHA + nHA;
    // v1.3 stage axis (ColorState.h NS block): only when a stage exit is active
    const bool exitStage = p.stage != int(Stage::Full);
    const size_t nNS = exitStage ? size_t(4) * kStageN : 0;
    const size_t oNS = oHN + nHN;
    // v1.3 monotone black (ColorState.h NT block): with the hue hold's neutral table
    const size_t nNT = hold ? size_t(3) * kHoldNX : 0;
    const size_t oNT = oNS + nNS;
    const size_t total = oNT + nNT;
    const bool sameBlocks = C.valid && C.exp == exp && C.dye == dye && C.proj == proj && m_state.size() == total;
    std::vector<float>& S = m_state;
    if (!sameBlocks) {
        S.assign(total, 0.f);
        std::memcpy(&S[oOct], exp->oct.data.data(), nOct * sizeof(float));
        if (nPL) std::memcpy(&S[oPL], dye->lut.data.data(), nPL * sizeof(float));
        if (proj) std::memcpy(&S[oPJ], proj->lut.data.data(), nPJ * sizeof(float));
    }
    std::fill(S.begin(), S.begin() + CEL_CS_HEADER, 0.f);
    float* s = S.data();
    s[CEL_CS_MAGIC] = CEL_CS_MAGIC_VALUE;
    s[CEL_CS_LAYOUT] = CEL_CS_LAYOUT_VALUE;
    s[CEL_CS_TOTAL] = float(total);
    s[CEL_CS_INPUT] = float(int(p.input));
    s[CEL_CS_OUTPUT] = float(int(p.output));
    s[CEL_CS_KIND] = float(kind);
    s[CEL_CS_MONO] = mono ? 1.f : 0.f;
    s[CEL_CS_HDR] = p.output == OutputSpace::Rec2100_PQ ? 1.f : (p.output == OutputSpace::Rec2100_HLG ? 2.f : 0.f);
    s[CEL_CS_GAIN] = float(std::exp2(double(p.exposure)));
    putMat(s, CEL_CS_MIN, spec::rgbToRgb(inputGamut(p.input), spec::Gamut::Rec2020));
    // ---- camera emulation (v1.2): only for footage of the source camera family (Fujifilm log inputs)
    {
        const bool fuji = p.input == InputSpace::FLog2C || p.input == InputSpace::FLog2 || p.input == InputSpace::FLog;
        const bool on = p.cameraEmulation == int(CameraEmulation::FujiXTransToAlexa) && fuji && p.emulationStrength > 0.f;
        s[CEL_CS_EMU_ON] = on ? 1.f : 0.f;
        const double k = on ? double(p.emulationStrength) : 0.0;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                s[CEL_CS_EMU_A + i * 3 + j] = float((i == j ? 1.0 : 0.0) + k * (kEmuFujiArriA[i][j] - (i == j ? 1.0 : 0.0)));
                s[CEL_CS_EMU_B + i * 3 + j] = float(k * kEmuFujiArriB[i][j]);
            }
    }
    s[CEL_CS_HOLD_ONSET] = float(kHoldOnset);
    s[CEL_CS_OCT_OFF] = float(oOct);
    s[CEL_CS_OCT_N] = float(exp->oct.n);

    // ---- negative / reversal curves (push, neutralize): cheap, rebuilt every prepare
    st::CurveDef cs[3];
    for (int i = 0; i < 3; ++i) cs[i] = pushCurve(mono ? neg.curves[0] : neg.curves[i], p.pushPull, rev);
    {
        std::vector<double> d(nNC);
        for (int i = 0; i < 3; ++i)
            for (int k = 0; k < kNCurve; ++k)
                d[size_t(i) * kNCurve + k] = curveEval(cs[i], kNegX0 + (kNegX1 - kNegX0) * k / (kNCurve - 1), rev);
        if (p.neutralize > 0.f) {
            double fog[3], dg[3];
            for (int i = 0; i < 3; ++i) { fog[i] = cs[i].fog; dg[i] = curveEval(cs[i], kLog10Grey, rev); }
            for (int k = 0; k < kNCurve; ++k) {
                double cn[3], cm = 0;
                for (int i = 0; i < 3; ++i) { cn[i] = (d[size_t(i) * kNCurve + k] - fog[i]) / (dg[i] - fog[i]); cm += cn[i] / 3.0; }
                for (int i = 0; i < 3; ++i)
                    d[size_t(i) * kNCurve + k] = fog[i] + (dg[i] - fog[i]) * ((1.0 - p.neutralize) * cn[i] + p.neutralize * cm);
            }
        }
        for (size_t k = 0; k < nNC; ++k) s[oNC + k] = float(d[k]);
    }
    s[CEL_CS_NC_OFF] = float(oNC);
    s[CEL_CS_NC_N] = float(kNCurve);
    s[CEL_CS_NC_X0] = float(kNegX0);
    s[CEL_CS_NC_X1] = float(kNegX1);
    for (int i = 0; i < 3; ++i) {
        s[CEL_CS_NIGAM + i] = float(1.0 / cs[i].gamma);
        s[CEL_CS_LW + i] = float(kLayerW[i]);
        float a = s[oNC + size_t(i) * kNCurve], b = s[oNC + size_t(i) * kNCurve + kNCurve - 1];
        s[CEL_CS_DMIN + i] = std::min(a, b);
        for (int j = 0; j < 3; ++j) s[CEL_CS_INTER + i * 3 + j] = i == j ? 0.f : float(neg.inter[i][j] * p.interlayer);
    }
    s[CEL_CS_REVSIGN] = rev ? -1.f : 1.f;
    const double pushf = std::pow(1.25, double(p.pushPull));
    double gmean = 0;
    for (int i = 0; i < 3; ++i) {
        double g = (mono ? neg.gran[0] : neg.gran[i]) * p.grainAmount * pushf;
        s[CEL_CS_GSIG + i] = float(g);
        gmean += g / 3.0;
    }
    s[CEL_CS_GRAIN_ON] = gmean > 0 ? 1.f : 0.f;
    s[CEL_CS_TX_GSIG] = float(2.5 * gmean);
    s[CEL_CS_NB] = float(p.negBleach * neg.silver);

    // ---- process specific
    spec::Mat3 Mout = outputMatrix(p.output);
    putMat(s, CEL_CS_MOUT, Mout);
    spec::Mat3 Mi = Mout.inverse();
    for (int j = 0; j < 3; ++j) s[CEL_CS_LUM + j] = float(Mi.m[1][j]);
    if (dye) { s[CEL_CS_PL_OFF] = float(oPL); s[CEL_CS_PL_N] = float(dye->lut.n); s[CEL_CS_PL_MAX] = float(dye->lut.maxv); }
    if (proj) {
        s[CEL_CS_PJ_OFF] = float(oPJ); s[CEL_CS_PJ_N] = float(proj->lut.n); s[CEL_CS_PJ_MAX] = float(proj->lut.maxv);
        putMat(s, CEL_CS_MXYZ, proj->cat);
    }
    double scanGamma = 1.0;
    if (kind == 0) {
        const std::vector<float>& pc = proj->pcurve;
        std::memcpy(&s[oPC], pc.data(), nPC * sizeof(float));
        if (p.printCrossover != 0.f) printCrossover(&s[oPC], p.printCrossover, mono);
        s[CEL_CS_PC_OFF] = float(oPC); s[CEL_CS_PC_N] = float(kNCurve);
        s[CEL_CS_PC_U0] = float(kPrtU0); s[CEL_CS_PC_U1] = float(kPrtU1);
        for (int j = 0; j < 3; ++j) s[CEL_CS_PMIN + j] = pc[size_t(j) * kNCurve];
        s[CEL_CS_PB] = float((1.0 - (1.0 - p.printBleach) * (1.0 - prt.silver)) * prt.silverDensity);
    } else if (kind == 1) {
        const st::ScannerDef& sc = st::scanner();
        scanGamma = sc.gamma;
        // neutral-referenced balanced inversion (reference/model.py scan_prepare): every channel is
        // mapped through the neutral ramp onto the G channel's density scale and inverted with the
        // G channel's grey gamma -> neutrals stay neutral at all exposures, latitude shape is kept
        const int K = kScnNFwd;
        std::vector<float> D(size_t(K) * 3);
        for (int k = 0; k < K; ++k) {
            float x = float(kScnXA + (kScnXB - kScnXA) * k / (K - 1));
            CelF3 d = celc_lut1_3(s, int(oNC), kNCurve, float(kNegX0), float(kNegX1), cel_f3(x, x, x));
            CelF3 l = celc_lut3(s, int(oPL), dye->lut.n, float(dye->lut.maxv), d);
            float v[3] = {-l.x, -l.y, -l.z};
            for (int j = 0; j < 3; ++j) D[size_t(k) * 3 + j] = k ? std::max(v[j], D[size_t(k - 1) * 3 + j]) : v[j];
        }
        auto fwdG = [&](double x) {   // uniform-grid interpolation of the G channel (== celc_lut1)
            double t = std::min(std::max((x - kScnXA) / (kScnXB - kScnXA) * (K - 1), 0.0), double(K - 1));
            int i = std::min(int(std::floor(t)), K - 2);
            double f = t - i;
            return double(D[size_t(i) * 3 + 1]) + (double(D[size_t(i + 1) * 3 + 1]) - D[size_t(i) * 3 + 1]) * f;
        };
        const double gGrey = fwdG(kLog10Grey);
        const double gam = (fwdG(kLog10Grey + 0.15) - fwdG(kLog10Grey - 0.15)) / 0.3;
        const size_t oSC = oPJ;   // scan has no projection block: the inversion LUT takes its place
        // part of the negative's own crossover is kept: each channel blends the neutral-referenced
        // inversion with its own per-channel inversion (own grey density / gamma); stock contrast
        const double alpha = mono ? 0.0 : neg.scanCrossover, contrast = neg.scanContrast;
        auto fwdJ = [&](int j, double x) {
            double t = std::min(std::max((x - kScnXA) / (kScnXB - kScnXA) * (K - 1), 0.0), double(K - 1));
            int i = std::min(int(std::floor(t)), K - 2);
            double f = t - i;
            return double(D[size_t(i) * 3 + j]) + (double(D[size_t(i + 1) * 3 + j]) - D[size_t(i) * 3 + j]) * f;
        };
        for (int j = 0; j < 3; ++j) {
            const double jGrey = fwdJ(j, kLog10Grey);
            const double jGam = (fwdJ(j, kLog10Grey + 0.15) - fwdJ(j, kLog10Grey - 0.15)) / 0.3;
            const double d0 = D[size_t(j)], d1 = D[size_t(K - 1) * 3 + j];
            s[CEL_CS_SD0 + j] = float(d0);
            s[CEL_CS_SD1 + j] = float(d1);
            int k = 1;
            for (int i = 0; i < kNCurve; ++i) {
                double t = double(float(d0)) + (double(float(d1)) - double(float(d0))) * i / (kNCurve - 1);
                while (k < K - 1 && double(D[size_t(k) * 3 + j]) < t) ++k;
                double a0 = D[size_t(k - 1) * 3 + j], a1 = D[size_t(k) * 3 + j];
                double f = a1 > a0 ? std::min(1.0, std::max(0.0, (t - a0) / (a1 - a0))) : 0.0;
                double x = kScnXA + (kScnXB - kScnXA) * (k - 1 + f) / (K - 1);
                double neutral = (fwdG(x) - gGrey) / gam, own = (fwdJ(j, x) - jGrey) / jGam;
                s[oSC + size_t(j) * kNCurve + i] = float(((1.0 - alpha) * neutral + alpha * own) * contrast);
            }
        }
        s[CEL_CS_SC_OFF] = float(oSC);
        s[CEL_CS_SC_N] = float(kNCurve);
        spec::Mat3 Ms;
        if (!mono) for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) Ms.m[i][j] = neg.scanMatrix[i][j];
        putMat(s, CEL_CS_MSCAN, Ms);
        spec::Mat3 Ms2;
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) Ms2.m[i][j] = mono ? 0.0 : neg.scanRoot[i][j];
        putMat(s, CEL_CS_MSCAN2, Ms2);
        double tgt = -std::log10(sc.greyY), lo = -5.0, hi = 5.0;
        for (int it = 0; it < 80; ++it) {
            double mid = 0.5 * (lo + hi);
            if (dev(mid, sc.gamma, sc.k, sc.R, sc.p) < tgt) lo = mid; else hi = mid;
        }
        double ws = 0.5 * (lo + hi);
        for (int k = 0; k < kNCurve; ++k) {
            double w = kScnW0 + (kScnW1 - kScnW0) * k / (kNCurve - 1);
            float v = float(dev(ws - w, sc.gamma, sc.k, sc.R, sc.p));
            for (int j = 0; j < 3; ++j) s[oPC + size_t(j) * kNCurve + k] = v;
        }
        s[CEL_CS_PC_OFF] = float(oPC); s[CEL_CS_PC_N] = float(kNCurve);
        s[CEL_CS_PC_U0] = float(kScnW0); s[CEL_CS_PC_U1] = float(kScnW1);
        putMat(s, CEL_CS_MXYZ, spec::npm(spec::Gamut::Rec2020));
    }

    // ---- automatic grey balance (points 0, exposure 0): 18% grey -> neutral D65 at the target Y
    const float gain = s[CEL_CS_GAIN];
    s[CEL_CS_GAIN] = 1.f;
    double off[3] = {0, 0, 0};
    if (kind != 1) {
        const double tY = kind == 0 ? prt.greyY : neg.greyY;
        // target neutral = the projection white (D65 unless a print lab projection white is chosen, v1.3)
        const spec::Vec3 W = kind == 0 ? proj->white : spec::xyToXYZ(0.3127, 0.3290);
        const double tgt[3] = {std::log10(W[0] * tY), std::log10(W[1] * tY), std::log10(W[2] * tY)};
        const double dt = -std::log10(tY);
        auto setOff = [&](const double o[3]) { for (int j = 0; j < 3; ++j) s[CEL_CS_OFF + j] = float(o[j]); };
        setOff(off);
        if (kind == 0) {   // per-layer bisection on the print curve as the starting point
            CelcNeg ng = celc_negative(s, cel_f3(0.18f, 0.18f, 0.18f), cel_f3(0, 0, 0), 0);
            CelF3 h = celc_lut3(s, int(oPL), dye->lut.n, float(dye->lut.maxv), ng.d);
            for (int j = 0; j < 3; ++j) {
                double lo = kPrtU0, hi = kPrtU1;
                for (int it = 0; it < 60; ++it) {
                    double mid = 0.5 * (lo + hi);
                    float P = celc_lut1(s, int(oPC), kNCurve, j, float(kPrtU0), float(kPrtU1), float(mid));
                    if (double(P) - s[CEL_CS_PMIN + j] < dt) lo = mid; else hi = mid;
                }
                off[j] = 0.5 * (lo + hi) - (double(celc_comp(h, j)) - ng.silver);
            }
        } else {
            for (int j = 0; j < 3; ++j) {
                double lo = -4.0, hi = 4.0;
                for (int it = 0; it < 60; ++it) {
                    double mid = 0.5 * (lo + hi);
                    float D = celc_lut1(s, int(oNC), kNCurve, j, float(kNegX0), float(kNegX1), float(kLog10Grey + mid));
                    if (double(D) - s[CEL_CS_DMIN + j] > dt) lo = mid; else hi = mid;
                }
                off[j] = 0.5 * (lo + hi);
            }
        }
        auto f = [&](const double o[3], double out[3]) {
            setOff(o);
            CelF3 X = Grey::film(S, 0.18f);
            out[0] = std::log10(std::max(double(X.x), 1e-12));
            out[1] = std::log10(std::max(double(X.y), 1e-12));
            out[2] = std::log10(std::max(double(X.z), 1e-12));
        };
        const double hs = 1e-3;
        for (int it = 0; it < 40; ++it) {
            double y0[3];
            f(off, y0);
            if (mono) {
                double r = y0[1] - tgt[1];
                if (std::fabs(r) < 2e-6) break;
                double o2[3] = {off[0] + hs, off[1] + hs, off[2] + hs}, y1[3];
                f(o2, y1);
                double dy = (y1[1] - y0[1]) / hs;
                double st = std::fabs(dy) > 1e-9 ? -r / dy : 0.0;
                st = std::min(0.5, std::max(-0.5, st));
                for (double& v : off) v += st;
                continue;
            }
            double r[3] = {y0[0] - tgt[0], y0[1] - tgt[1], y0[2] - tgt[2]};
            if (std::max({std::fabs(r[0]), std::fabs(r[1]), std::fabs(r[2])}) < 2e-6) break;
            spec::Mat3 J;
            for (int k = 0; k < 3; ++k) {
                double o2[3] = {off[0], off[1], off[2]}, y1[3];
                o2[k] += hs;
                f(o2, y1);
                for (int j = 0; j < 3; ++j) J.m[j][k] = (y1[j] - y0[j]) / hs;
            }
            spec::Vec3 d = J.inverse() * spec::Vec3{r[0], r[1], r[2]};
            if (!(std::isfinite(d[0]) && std::isfinite(d[1]) && std::isfinite(d[2]))) break;
            for (int j = 0; j < 3; ++j) off[j] -= std::min(0.5, std::max(-0.5, d[size_t(j)]));
        }
        setOff(off);
    }

    // ---- HDR calibration on the balanced state: mid grey -> 15 nits, unroll to peak
    {
        CelF3 Xg = Grey::film(S, 0.18f);
        // the unroll reaches the display peak at +10 stops over grey (reference/model.py calibrate_hdr)
        CelF3 Xw = Grey::film(S, 0.18f * 1024.f);
        double Lref = kHdrGreyNits / std::max(double(Xg.y), 1e-6);
        double peak = p.output == OutputSpace::Rec2100_PQ ? double(p.hdrPeakNits) : 1000.0;
        double t10 = (10.0 * std::log10(2.0) - CEL_HDR_E0) / CEL_HDR_ER;
        s[CEL_CS_HDR_LREF] = float(Lref);
        s[CEL_CS_HDR_GMAX] = float(std::max(0.0, std::log10(peak / (Lref * std::max(double(Xw.y), 1e-6)))) * (1.0 + t10 * t10) / (t10 * t10));
        s[CEL_CS_HDR_UNROLL] = p.hdrUnroll;
        s[CEL_CS_HDR_PEAK] = float(peak);
    }

    // ---- hue hold anchor (points 0, cached per film): film XYZ of every scene direction at 18 %
    //      grey luminance, normalised to Y = 1, + validity weight (reference/model.py bake_hold_anchor)
    s[CEL_CS_HOLD] = 0.f;
    s[CEL_CS_HOLD_CHROMA] = 0.f;
    if (hold) {
        size_t hit = C.hold.size();
        for (size_t e = 0; e < C.hold.size(); ++e)
            if (sameFilm(C.hold[e].key, p) && C.hold[e].anchor.size() == nHA) { hit = e; break; }
        if (hit < C.hold.size()) {
            if (hit != 0) std::rotate(C.hold.begin(), C.hold.begin() + long(hit), C.hold.begin() + long(hit) + 1);
        } else {
            std::vector<float> A(nHA);
            const double e1[3] = {1.0 / std::sqrt(2.0), -1.0 / std::sqrt(2.0), 0.0};
            const double e2[3] = {1.0 / std::sqrt(6.0), 1.0 / std::sqrt(6.0), -2.0 / std::sqrt(6.0)};
            const double e3[3] = {1.0 / std::sqrt(3.0), 1.0 / std::sqrt(3.0), 1.0 / std::sqrt(3.0)};
            const double lum[3] = {0.26270021, 0.67799807, 0.05930172};
            const int N = kHoldN;
            CelF3 g0 = Grey::film(S, 0.18f);
            const float gx = celc_labf(g0.x / g0.y / CEL_D65_X), gy = celc_labf(1.f), gz = celc_labf(g0.z / g0.y / CEL_D65_Z);
            parallelRows(N, [&](int r0, int r1) {
                for (int jv = r0; jv < r1; ++jv)
                    for (int iu = 0; iu < N; ++iu) {
                        double u = -1.0 + 2.0 * iu / (N - 1), v = -1.0 + 2.0 * jv / (N - 1);
                        double n = 1.0 - std::fabs(u) - std::fabs(v);
                        double a = n < 0 ? (1.0 - std::fabs(v)) * (u >= 0 ? 1.0 : -1.0) : u;
                        double b = n < 0 ? (1.0 - std::fabs(u)) * (v >= 0 ? 1.0 : -1.0) : v;
                        double d[3], Y = 0.0, L1 = 0.0;
                        for (int c = 0; c < 3; ++c) { d[c] = a * e1[c] + b * e2[c] + n * e3[c]; Y += lum[c] * d[c]; L1 += std::fabs(d[c]); }
                        bool ok = Y > 0.02 * L1;
                        double k = ok ? 0.18 / Y : 0.0;
                        CelF3 rgb = ok ? cel_f3(float(d[0] * k), float(d[1] * k), float(d[2] * k)) : cel_f3(0.18f, 0.18f, 0.18f);
                        CelcFilm f = celc_film_xyz(S.data(), rgb, cel_f3(0, 0, 0), 0);
                        float iy = 1.f / std::max(f.X.y, 1e-12f);
                        // CIELAB-scaled (a, b) relative to the grey neutral (celc_hold, model.bake_hold_anchor)
                        float qx = celc_labf(f.X.x * iy / CEL_D65_X), qy = celc_labf(1.f), qz = celc_labf(f.X.z * iy / CEL_D65_Z);
                        float* q = &A[(size_t(jv) * N + iu) * 4];
                        q[0] = 2.5f * ((qx - qy) - (gx - gy));
                        q[1] = (qy - qz) - (gy - gz);
                        q[2] = 0.f;
                        q[3] = ok ? float(std::min(1.0, std::max(0.0, (Y / L1 - 0.02) / 0.08))) : 0.f;
                    }
            });
            C.hold.insert(C.hold.begin(), Cache::HoldEntry{p, std::move(A)});
            if (C.hold.size() > kHoldCacheEntries) C.hold.pop_back();
        }
        std::memcpy(&S[oHA], C.hold.front().anchor.data(), nHA * sizeof(float));
        CelF3 g = Grey::film(S, 0.18f);
        s[CEL_CS_HOLD_N0] = g.x / g.y; s[CEL_CS_HOLD_N0 + 1] = 1.f; s[CEL_CS_HOLD_N0 + 2] = g.z / g.y;
        s[CEL_CS_HOLD_A_OFF] = float(oHA);
        s[CEL_CS_HOLD_A_N] = float(kHoldN);
    }

    // ---- user offsets on top of the balance
    const double pts[3] = {double(p.printerR) - p.printExposure, double(p.printerG) - p.printExposure,
                           double(p.printerB) - p.printExposure};
    for (int j = 0; j < 3; ++j) {
        double o = off[j];
        if (kind == 0) o += kPoint * pts[j];
        else if (kind == 1) o = -0.075 / scanGamma * pts[j];
        s[CEL_CS_OFF + j] = float(o);
    }
    // ---- hue hold neutral table (with the user's printer points): film neutral XYZ (Y = 1) over
    //      the reference log exposure (reference/model.py bake_hold_neutral)
    s[CEL_CS_BLACK_N] = 0.f;
    if (hold) {
        const float xr0 = celc_film_xyz(s, cel_f3(0.18f, 0.18f, 0.18f), cel_f3(0, 0, 0), 0).xref;
        std::vector<CelF3> nx(static_cast<size_t>(kHoldNX));
        for (int k = 0; k < kHoldNX; ++k) {
            double x = CEL_HOLD_X0 + (CEL_HOLD_X1 - CEL_HOLD_X0) * k / (kHoldNX - 1);
            float gv = float(0.18 * std::pow(10.0, x - double(xr0)));
            CelF3 X = Grey::film(S, gv);
            nx[size_t(k)] = X;
            float iy = 1.f / std::max(X.y, 1e-12f);
            s[oHN + k] = X.x * iy;
            s[oHN + kHoldNX + k] = 1.f;
            s[oHN + 2 * kHoldNX + k] = X.z * iy;
        }
        s[CEL_CS_HOLD_N_OFF] = float(oHN);
        s[CEL_CS_HOLD_N_N] = float(kHoldNX);
        s[CEL_CS_HOLD] = float(holdStrength);
        s[CEL_CS_HOLD_CHROMA] = float(holdChroma);
        bakeBlackMonotone(s, nx, xr0, int(oNT));
    }
    if (exitStage) buildStageAxis(s, p, int(oNS), int(oPL), dye->lut.n, float(dye->lut.maxv));
    s[CEL_CS_GAIN] = gain;

    C.exp = exp; C.dye = dye; C.proj = proj;
    C.last = p;
    C.valid = true;
    m_p = p;
    m_version = ++gStateVersion;
}

Float3 Pipeline::decodeToScene(Float3 e) const {
    CelF3 r = celc_decode_px(m_state.data(), cel_f3(e.r, e.g, e.b));
    return {r.x, r.y, r.z};
}
Float3 Pipeline::sceneToOutput(Float3 sc, Float3 n) const {
    CelF3 r = celc_scene_to_output(m_state.data(), cel_f3(sc.r, sc.g, sc.b), cel_f3(n.r, n.g, n.b), 1);
    return {r.x, r.y, r.z};
}
Float3 Pipeline::processColor(Float3 e) const {
    CelF3 l = celc_decode_px(m_state.data(), cel_f3(e.r, e.g, e.b));
    CelF3 r = celc_scene_to_output(m_state.data(), l, cel_f3(0, 0, 0), 0);
    return {r.x, r.y, r.z};
}

// ------------------------------------------------------------------------- .cube export
// Scene-linear input (Linear Rec.2020) has no camera log curve: a uniform [0,1] lattice would clip
// everything above 1.0 (+2.5 stops over grey) and put all shadows below 18 % grey into ~6 lattice
// intervals. Such LUTs carry a 1D log2 shaper in the same .cube (Resolve / Adobe 1D+3D layout:
// LUT_1D_INPUT_RANGE over scene-linear values, shaper output = 3D lattice coordinate in [0,1]):
//   t(x) = (log2(max(x, lo) + o) - log2(lo + o)) / R,  o = 0.18 * 2^-7,  lo = -o / 2,
//   R = log2(hi + o) - log2(lo + o),  hi = 0.18 * 2^8.5   (~ -8.5 .. +8.5 stops around grey)
namespace {
constexpr double kShO = 0.18 / 128.0, kShLo = -0.5 * kShO, kShHi = 0.18 * 362.03867196751236;
inline double shaperR() { return std::log2(kShHi + kShO) - std::log2(kShLo + kShO); }
inline double shaperFwd(double x) {
    return (std::log2(std::max(x, kShLo) + kShO) - std::log2(kShLo + kShO)) / shaperR();
}
inline double shaperInv(double t) { return std::exp2(t * shaperR() + std::log2(kShLo + kShO)) - kShO; }
constexpr int kShaperSize = 16384;
bool cubeUsesShaper(InputSpace in) { return in == InputSpace::LinearRec2020; }
} // namespace

bool writeCubeLUT(const Pipeline& pipe, int size, const std::string& path, const std::string& title, std::string* err) {
    if (size != 17 && size != 33 && size != 65) {
        if (err) *err = "LUT size must be 17, 33 or 65";
        return false;
    }
    const int N = size;
    const bool shaper = cubeUsesShaper(pipe.params().input);
    std::vector<float> axis(static_cast<size_t>(N), 0.0f);
    for (int i = 0; i < N; ++i) axis[size_t(i)] = float(shaper ? shaperInv(double(i) / (N - 1)) : double(i) / (N - 1));
    std::vector<float> rgb(size_t(N) * N * N * 3);
    parallelRows(N, [&](int b0, int b1) {
        for (int b = b0; b < b1; ++b)
            for (int g = 0; g < N; ++g)
                for (int r = 0; r < N; ++r) {
                    Float3 o = pipe.processColor({axis[size_t(r)], axis[size_t(g)], axis[size_t(b)]});
                    float* p = &rgb[((size_t(b) * N + g) * N + r) * 3];
                    p[0] = o.r; p[1] = o.g; p[2] = o.b;
                }
    });
    std::ostringstream os;
    os.imbue(std::locale::classic());
    std::string t = title;
    for (char& c : t) if (c == '"' || c == '\n' || c == '\r') c = ' ';
    os << "TITLE \"" << t << "\"\n";
    char buf[128];
    if (shaper) {
        os << "# Created by Celluloid Pro. Input: scene-linear Rec.2020 through a 1D log2 shaper "
              "(-8.5 .. +8.5 stops around 18 % grey); output: encoded display values.\n";
        std::snprintf(buf, sizeof(buf), "LUT_1D_SIZE %d\nLUT_1D_INPUT_RANGE %.9g %.9g\n", kShaperSize, kShLo, kShHi);
        os << buf;
        os << "LUT_3D_SIZE " << N << "\nLUT_3D_INPUT_RANGE 0.0 1.0\n";
        for (int k = 0; k < kShaperSize; ++k) {
            const double x = kShLo + (kShHi - kShLo) * double(k) / (kShaperSize - 1);
            const double v = std::min(std::max(shaperFwd(x), 0.0), 1.0);
            std::snprintf(buf, sizeof(buf), "%.7f %.7f %.7f\n", v, v, v);
            os << buf;
        }
    } else {
        os << "# Created by Celluloid Pro. Input: encoded camera code values [0,1]; output: encoded display values.\n";
        os << "LUT_3D_SIZE " << N << "\n";
        os << "DOMAIN_MIN 0.0 0.0 0.0\nDOMAIN_MAX 1.0 1.0 1.0\n";
    }
    for (size_t i = 0; i < size_t(N) * N * N; ++i) {
        std::snprintf(buf, sizeof(buf), "%.6f %.6f %.6f\n", double(rgb[i * 3]), double(rgb[i * 3 + 1]), double(rgb[i * 3 + 2]));
        os << buf;
    }
    try {
        // UTF-8 path on every platform (Windows: wide-character path, non-ASCII folders work)
        std::filesystem::path fp = std::filesystem::u8path(path);
        if (fp.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(fp.parent_path(), ec);
        }
        std::ofstream f(fp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot open '" + path + "' for writing";
            return false;
        }
        const std::string data = os.str();
        f.write(data.data(), std::streamsize(data.size()));
        f.close();
        if (!f) {
            if (err) *err = "write error on '" + path + "'";
            return false;
        }
    } catch (const std::exception& e) {
        if (err) *err = std::string("LUT export failed: ") + e.what();
        return false;
    }
    return true;
}

}} // namespace cel::core
