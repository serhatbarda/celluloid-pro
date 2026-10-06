// Celluloid Pro - colour science core: name lists, Pipeline::prepare (state assembly, grey
// balance, HDR calibration), CPU wrappers around the kernel functions, .cube export.
// The model is documented in SPEC section 4, docs/STOCKS.md and reference/model.py.
#include "Core.h"
#include "Bake.h"
#include "../kernels/ColorKernels.h"

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
        v.push_back(s);
    }
    return v;
}
std::vector<std::string> printStockNames() {
    std::vector<std::string> v;
    for (int i = 0; i < st::numPrints(); ++i) v.push_back(st::print(i).name);
    return v;
}

// ------------------------------------------------------------------------- pipeline
static std::atomic<uint64_t> gStateVersion{0};   // process-wide: versions never collide between instances

struct Pipeline::Cache {
    bool valid = false;
    LookParams last;
    std::shared_ptr<const ExposureBake> exp;
    std::shared_ptr<const DyeLutBake> dye;        // print or scan LUT
    std::shared_ptr<const ProjBake> proj;         // projection (print or reversal)
    std::vector<NegativeStockInfo> infos;
};

static bool sameParams(const LookParams& a, const LookParams& b) {
    return a.input == b.input && a.output == b.output && a.exposure == b.exposure && a.negStock == b.negStock &&
           a.pushPull == b.pushPull && a.interlayer == b.interlayer && a.negBleach == b.negBleach &&
           a.neutralize == b.neutralize && a.process == b.process && a.printStock == b.printStock &&
           a.printerR == b.printerR && a.printerG == b.printerG && a.printerB == b.printerB &&
           a.printExposure == b.printExposure && a.printBleach == b.printBleach && a.hdrPeakNits == b.hdrPeakNits &&
           a.hdrUnroll == b.hdrUnroll && a.grainAmount == b.grainAmount;
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

Pipeline::Pipeline() : m_cache(std::make_shared<Cache>()) { prepare(LookParams()); }

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
    if (kind == 0) { dye = printLut(ni, pi); proj = projectionLut(pi, mono); }
    else if (kind == 1) dye = scanLut(ni);
    else proj = reversalLut(ni);

    // ---- layout
    const size_t nOct = exp->oct.data.size();
    const size_t nNC = size_t(3) * kNCurve;
    const size_t nPL = dye ? dye->lut.data.size() : 0;
    const size_t nPC = kind == 2 ? 0 : size_t(3) * kNCurve;
    const size_t nPJ = proj ? proj->lut.data.size() : (kind == 1 ? size_t(3) * kNCurve : 0);   // scan: inversion LUT
    const size_t oOct = CEL_CS_HEADER, oNC = oOct + nOct, oPL = oNC + nNC, oPC = oPL + nPL, oPJ = oPC + nPC;
    const size_t total = oPJ + nPJ;
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
        for (int j = 0; j < 3; ++j) {
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
                s[oSC + size_t(j) * kNCurve + i] = float((fwdG(x) - gGrey) / gam);
            }
        }
        s[CEL_CS_SC_OFF] = float(oSC);
        s[CEL_CS_SC_N] = float(kNCurve);
        spec::Mat3 Ms;
        if (!mono) for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) Ms.m[i][j] = neg.scanMatrix[i][j];
        putMat(s, CEL_CS_MSCAN, Ms);
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
        const double tgt[3] = {std::log10(0.3127 / 0.3290 * tY), std::log10(tY), std::log10((1.0 - 0.3127 - 0.3290) / 0.3290 * tY)};
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
        CelF3 Xw = Grey::film(S, 0.18f * 16384.f);
        double Lref = kHdrGreyNits / std::max(double(Xg.y), 1e-6);
        double peak = p.output == OutputSpace::Rec2100_PQ ? double(p.hdrPeakNits) : 1000.0;
        s[CEL_CS_HDR_LREF] = float(Lref);
        s[CEL_CS_HDR_GMAX] = float(std::max(0.0, std::log10(peak / (Lref * std::max(double(Xw.y), 1e-6)))));
        s[CEL_CS_HDR_UNROLL] = p.hdrUnroll;
        s[CEL_CS_HDR_PEAK] = float(peak);
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
bool writeCubeLUT(const Pipeline& pipe, int size, const std::string& path, const std::string& title, std::string* err) {
    if (size != 17 && size != 33 && size != 65) {
        if (err) *err = "LUT size must be 17, 33 or 65";
        return false;
    }
    const int N = size;
    std::vector<float> rgb(size_t(N) * N * N * 3);
    parallelRows(N, [&](int b0, int b1) {
        for (int b = b0; b < b1; ++b)
            for (int g = 0; g < N; ++g)
                for (int r = 0; r < N; ++r) {
                    Float3 o = pipe.processColor({float(r) / (N - 1), float(g) / (N - 1), float(b) / (N - 1)});
                    float* p = &rgb[((size_t(b) * N + g) * N + r) * 3];
                    p[0] = o.r; p[1] = o.g; p[2] = o.b;
                }
    });
    std::ostringstream os;
    os.imbue(std::locale::classic());
    std::string t = title;
    for (char& c : t) if (c == '"' || c == '\n' || c == '\r') c = ' ';
    os << "TITLE \"" << t << "\"\n";
    os << "# Created by Celluloid Pro. Input: encoded camera code values [0,1]; output: encoded display values.\n";
    os << "LUT_3D_SIZE " << N << "\n";
    os << "DOMAIN_MIN 0.0 0.0 0.0\nDOMAIN_MAX 1.0 1.0 1.0\n";
    char buf[96];
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
