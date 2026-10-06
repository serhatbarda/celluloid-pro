// Celluloid Pro - spectral bakes + process-wide cache. Mirrors reference/model.py.
#include "Bake.h"
#include <algorithm>
#include <atomic>
#include <functional>
#include <cmath>
#include <cstdlib>
#include <map>
#include <mutex>
#include <thread>
#include <tuple>

namespace cel { namespace core { namespace bake {

using spec::NWL;
using spec::Spectrum;

namespace {
#include "UpsampleTable.inc"
}

// ------------------------------------------------------------------------- parametric curves
static double softplus(double z) { return z > 30.0 ? z : std::log1p(std::exp(std::min(z, 30.0))); }

double dev(double y, double gamma, double k, double R, double p) {
    double r = gamma * softplus(k * y) / k;
    return r / std::pow(1.0 + std::pow(r / R, p), 1.0 / p);
}

stocks::CurveDef pushCurve(const stocks::CurveDef& c0, double push, bool reversal) {
    stocks::CurveDef c = c0;
    c.gamma *= 1.0 + (push > 0 ? 0.12 : 0.10) * push;
    if (reversal) {
        c.x0 += 0.10 * push;
        c.R *= 1.0 - 0.03 * push;
        c.fog += 0.01 * push;
    } else {
        c.x0 -= 0.10 * push;
        c.fog += (push > 0 ? 0.035 : 0.01) * push;
    }
    return c;
}

double curveEval(const stocks::CurveDef& c, double x, bool reversal) {
    return reversal ? c.fog + dev(c.x0 - x, c.gamma, c.k, c.R, c.p) : c.fog + dev(x - c.x0, c.gamma, c.k, c.R, c.p);
}

Spectrum baseDensity(const stocks::BaseDef& b) {
    Spectrum s{};
    for (int i = 0; i < NWL; ++i) {
        double w = spec::wavelength(i);
        double tb = (w - 400.0) / 35.0, tr = (w - 700.0) / 60.0;
        s[size_t(i)] = b.d0 + b.blue * std::exp(-0.5 * tb * tb) + b.red * std::exp(-0.5 * tr * tr);
    }
    return s;
}

// ------------------------------------------------------------------------- helpers
static void parallelFor(int n, const std::function<void(int)>& fn) {
    unsigned hw = std::thread::hardware_concurrency();
    int nt = std::max(1, std::min(int(hw == 0 ? 2u : hw), n));
    static const int forced = [] { const char* e = std::getenv("CEL_BAKE_THREADS"); return e ? std::atoi(e) : 0; }();
    if (forced > 0) nt = std::min(nt, forced);
    if (nt == 1) { for (int i = 0; i < n; ++i) fn(i); return; }
    std::vector<std::thread> th;
    std::atomic<int> next{0};
    for (int t = 0; t < nt; ++t)
        th.emplace_back([&] { for (;;) { int i = next.fetch_add(1); if (i >= n) break; fn(i); } });
    for (auto& t : th) t.join();
}

static Spectrum mul(const Spectrum& a, const Spectrum& b) { Spectrum r{}; for (int i = 0; i < NWL; ++i) r[size_t(i)] = a[size_t(i)] * b[size_t(i)]; return r; }
static double sum(const Spectrum& a) { double s = 0; for (double v : a) s += v; return s; }

static bool solve3(double J[3][3], const double r[3], double out[3]) {
    spec::Mat3 M;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) M.m[i][j] = J[i][j];
    double det = M.m[0][0] * (M.m[1][1] * M.m[2][2] - M.m[1][2] * M.m[2][1]) - M.m[0][1] * (M.m[1][0] * M.m[2][2] - M.m[1][2] * M.m[2][0]) +
                 M.m[0][2] * (M.m[1][0] * M.m[2][1] - M.m[1][1] * M.m[2][0]);
    if (!(std::fabs(det) > 1e-300)) return false;
    spec::Vec3 v = M.inverse() * spec::Vec3{r[0], r[1], r[2]};
    out[0] = v[0]; out[1] = v[1]; out[2] = v[2];
    return true;
}

// sensitivities of a negative's three layers (B&W: identical)
static void negSens(const stocks::NegDef& n, Spectrum out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = spec::lobes(n.kind == stocks::kBW ? n.sens[0] : n.sens[i]);
}
static const stocks::CurveDef& negCurveDef(const stocks::NegDef& n, int i) { return n.kind == stocks::kBW ? n.curves[0] : n.curves[i]; }

double negLutMax(const stocks::NegDef& n) {
    double m = 0;
    int nl = n.kind == stocks::kBW ? 1 : 3;
    for (int i = 0; i < nl; ++i) m = std::max(m, n.curves[i].fog + n.curves[i].R);
    return m * 1.15 + 0.35;
}
double printLutMax(const stocks::PrintDef& p) {
    double m = 0;
    for (int i = 0; i < 3; ++i) m = std::max(m, p.curves[i].fog + p.curves[i].R);
    return m * 1.05 + 0.3;
}

// visual equivalent-neutral-density scale of a dye set under a lamp (model.visual_end_scale)
static void visualEndScale(const Spectrum eps[3], const Spectrum& base, const Spectrum& lamp, double s[3]) {
    const double (*C)[3] = spec::cmf();
    double w[3][NWL], ref[3];
    for (int c = 0; c < 3; ++c) {
        double t = 0;
        for (int l = 0; l < NWL; ++l) { w[c][l] = C[l][c] * lamp[size_t(l)] * std::pow(10.0, -base[size_t(l)]); t += w[c][l]; }
        ref[c] = std::log10(t) - 1.0;
    }
    s[0] = s[1] = s[2] = 1.0;
    for (int it = 0; it < 50; ++it) {
        double T[NWL];
        for (int l = 0; l < NWL; ++l) T[l] = std::pow(10.0, -(s[0] * eps[0][size_t(l)] + s[1] * eps[1][size_t(l)] + s[2] * eps[2][size_t(l)]));
        double X[3], r[3], J[3][3];
        for (int c = 0; c < 3; ++c) { X[c] = 0; for (int l = 0; l < NWL; ++l) X[c] += w[c][l] * T[l]; r[c] = std::log10(X[c]) - ref[c]; }
        if (std::max({std::fabs(r[0]), std::fabs(r[1]), std::fabs(r[2])}) < 1e-13) break;
        for (int c = 0; c < 3; ++c)
            for (int j = 0; j < 3; ++j) {
                double a = 0;
                for (int l = 0; l < NWL; ++l) a += w[c][l] * eps[j][size_t(l)] * T[l];
                J[c][j] = -a / X[c];
            }
        double d[3];
        if (!solve3(J, r, d)) break;
        for (int j = 0; j < 3; ++j) s[j] -= d[j];
    }
}

struct NegDyes { Spectrum eps[3], sec[3]; double mask[3], dsat[3]; Spectrum base; };

static Spectrum printerLamp() { return spec::illuminant(stocks::lamps().printer); }
static Spectrum projectorLamp() { return spec::illuminant(stocks::lamps().projector); }

// dye spectra of a negative in equivalent-neutral units (model.neg_dyes_scaled)
static NegDyes negDyes(const stocks::NegDef& n) {
    NegDyes D;
    D.base = baseDensity(n.base);
    for (int i = 0; i < 3; ++i) {
        D.mask[i] = n.kind == stocks::kBW ? 0.0 : n.mask[i];
        const stocks::CurveDef& c = negCurveDef(n, i);
        D.dsat[i] = c.fog + c.R;
    }
    if (n.kind == stocks::kBW) {
        Spectrum sil{};
        if (n.hasSilverSpectrum) sil = spec::lobes(n.silverSpectrum); else sil.fill(1.0);
        for (int i = 0; i < 3; ++i) { for (int l = 0; l < NWL; ++l) D.eps[i][size_t(l)] = sil[size_t(l)] / 3.0; D.sec[i].fill(0.0); }
        return D;
    }
    for (int i = 0; i < 3; ++i) {
        Spectrum p = spec::lobes(n.dyes[i].primary), s = spec::lobes(n.dyes[i].secondary);
        for (int l = 0; l < NWL; ++l) { D.eps[i][size_t(l)] = p[size_t(l)] + s[size_t(l)]; D.sec[i][size_t(l)] = s[size_t(l)]; }
    }
    double sc[3] = {1, 1, 1};
    if (n.kind == stocks::kReversal) {
        visualEndScale(D.eps, D.base, projectorLamp(), sc);
    } else {
        // printing-density END against the reference print (P1) sensitivities under the printer lamp
        Spectrum L = printerLamp();
        double w[3][NWL];
        for (int j = 0; j < 3; ++j) {
            Spectrum S = spec::lobes(stocks::print(0).sens[j]);
            for (int l = 0; l < NWL; ++l) w[j][l] = S[size_t(l)] * L[size_t(l)] * std::pow(10.0, -D.base[size_t(l)]);
        }
        auto logH = [&](const double sv[3], double d, double out[3]) {
            double T[NWL];
            for (int l = 0; l < NWL; ++l) {
                double dens = 0;
                for (int i = 0; i < 3; ++i)
                    dens += d * sv[i] * D.eps[i][size_t(l)] + D.mask[i] * std::max(D.dsat[i] - d, 0.0) * sv[i] * D.sec[i][size_t(l)];
                T[l] = std::pow(10.0, -dens);
            }
            for (int j = 0; j < 3; ++j) { double a = 0; for (int l = 0; l < NWL; ++l) a += w[j][l] * T[l]; out[j] = std::log10(a); }
        };
        auto resid = [&](const double sv[3], double r[3]) {
            double a[3], b[3];
            logH(sv, 1.0, a);
            logH(sv, 0.0, b);
            for (int j = 0; j < 3; ++j) r[j] = a[j] - b[j] + 1.0;
        };
        for (int it = 0; it < 60; ++it) {
            double r[3];
            resid(sc, r);
            if (std::max({std::fabs(r[0]), std::fabs(r[1]), std::fabs(r[2])}) < 1e-12) break;
            double J[3][3];
            for (int k = 0; k < 3; ++k) {
                double e[3] = {sc[0], sc[1], sc[2]};
                e[k] += 1e-6;
                double r2[3];
                resid(e, r2);
                for (int j = 0; j < 3; ++j) J[j][k] = (r2[j] - r[j]) / 1e-6;
            }
            double d[3];
            if (!solve3(J, r, d)) break;
            for (int j = 0; j < 3; ++j) sc[j] -= d[j];
        }
    }
    for (int i = 0; i < 3; ++i)
        for (int l = 0; l < NWL; ++l) { D.eps[i][size_t(l)] *= sc[i]; D.sec[i][size_t(l)] *= sc[i]; }
    return D;
}

// generic separable dye LUT: out = log10( sum_l 10^-base F0[i] F1[j] F2[k] w_c )
static Lut dyeLut(const std::vector<double> F[3], const Spectrum& base, const double w[3][NWL], double dmax) {
    const int N = kNLut3;
    Lut L;
    L.n = N;
    L.maxv = dmax;
    L.data.resize(size_t(N) * N * N * 3);
    double tb[NWL];
    for (int l = 0; l < NWL; ++l) tb[l] = std::pow(10.0, -base[size_t(l)]);
    parallelFor(N, [&](int i) {
        std::vector<double> B(size_t(N) * NWL);
        for (int j = 0; j < N; ++j)
            for (int l = 0; l < NWL; ++l) B[size_t(j) * NWL + l] = tb[l] * F[0][size_t(i) * NWL + l] * F[1][size_t(j) * NWL + l];
        double Bw[NWL];
        for (int c = 0; c < 3; ++c)
            for (int j = 0; j < N; ++j) {
                for (int l = 0; l < NWL; ++l) Bw[l] = B[size_t(j) * NWL + l] * w[c][l];
                for (int k = 0; k < N; ++k) {
                    const double* f2 = &F[2][size_t(k) * NWL];
                    double a = 0;
                    for (int l = 0; l < NWL; ++l) a += Bw[l] * f2[l];
                    L.data[((size_t(i) * N + j) * N + k) * 3 + c] = float(std::log10(std::max(a, 1e-30)));
                }
            }
    });
    return L;
}

static void negFactors(const NegDyes& D, double dmax, std::vector<double> F[3]) {
    const int N = kNLut3;
    for (int i = 0; i < 3; ++i) {
        F[i].resize(size_t(N) * NWL);
        for (int g = 0; g < N; ++g) {
            double d = dmax * g / (N - 1);
            double md = D.mask[i] * std::max(D.dsat[i] - d, 0.0);
            for (int l = 0; l < NWL; ++l) F[i][size_t(g) * NWL + l] = std::pow(10.0, -(d * D.eps[i][size_t(l)] + md * D.sec[i][size_t(l)]));
        }
    }
}

spec::Mat3 catToD65(const Spectrum& lamp) {
    spec::Vec3 X = spec::XYZof(lamp);
    spec::Vec3 Xw = {X[0] / X[1], 1.0, X[2] / X[1]};
    return spec::catMatrix(Xw, spec::xyToXYZ(0.3127, 0.3290), true);
}

// ------------------------------------------------------------------------- cache
namespace {
std::mutex gMutex;
std::map<std::tuple<int, int, int>, std::shared_ptr<const void>> gCache;   // (kind, a, b)

template <class T, class F> std::shared_ptr<const T> cached(int kind, int a, int b, F make) {
    {
        std::lock_guard<std::mutex> lk(gMutex);
        auto it = gCache.find({kind, a, b});
        if (it != gCache.end()) return std::static_pointer_cast<const T>(it->second);
    }
    std::shared_ptr<const T> v = make();       // bake outside the lock (other bakes may run concurrently)
    std::lock_guard<std::mutex> lk(gMutex);
    auto it = gCache.find({kind, a, b});
    if (it != gCache.end()) return std::static_pointer_cast<const T>(it->second);
    gCache[{kind, a, b}] = v;
    return v;
}

// reflectance spectra of the upsampling table (stock independent, built once)
const std::vector<double>& upsampleR() {
    static std::vector<double> R;
    static std::once_flag once;
    std::call_once(once, [] {
        const int NN = CEL_UPSAMPLE_N * CEL_UPSAMPLE_N;
        R.resize(size_t(NN) * NWL);
        for (int t = 0; t < NN; ++t) {
            double c0 = kUpsampleTable[t * 4 + 0], c1 = kUpsampleTable[t * 4 + 1], c2 = kUpsampleTable[t * 4 + 2];
            for (int l = 0; l < NWL; ++l) {
                double x = spec::tnorm(l);
                R[size_t(t) * NWL + l] = 0.5 + 0.5 * std::tanh(0.5 * (c0 + c1 * x + c2 * x * x));
            }
        }
    });
    return R;
}
} // namespace

void clearCache() { std::lock_guard<std::mutex> lk(gMutex); gCache.clear(); }

std::shared_ptr<const ExposureBake> exposureLut(int neg) {
    return cached<ExposureBake>(0, neg, 0, [neg] {
        const stocks::NegDef& n = stocks::negative(neg);
        Spectrum S[3];
        negSens(n, S);
        Spectrum I = spec::illuminant(n.balance);
        double w[3][NWL];
        for (int i = 0; i < 3; ++i) {
            Spectrum t = mul(S[i], I);
            double s = sum(t);
            for (int l = 0; l < NWL; ++l) w[i][l] = t[size_t(l)] / s;
        }
        const std::vector<double>& R = upsampleR();
        auto b = std::make_shared<ExposureBake>();
        const int NN = CEL_UPSAMPLE_N * CEL_UPSAMPLE_N;
        b->oct.n = CEL_UPSAMPLE_N;
        b->oct.data.resize(size_t(NN) * 3);
        for (int t = 0; t < NN; ++t) {
            const double* r = &R[size_t(t) * NWL];
            double a = kUpsampleTable[t * 4 + 3];
            for (int i = 0; i < 3; ++i) {
                double e = 0;
                for (int l = 0; l < NWL; ++l) e += r[l] * w[i][l];
                b->oct.data[size_t(t) * 3 + i] = float(e * a);
            }
        }
        return std::shared_ptr<const ExposureBake>(b);
    });
}

std::shared_ptr<const DyeLutBake> printLut(int neg, int prt) {
    return cached<DyeLutBake>(1, neg, prt, [neg, prt] {
        const stocks::NegDef& n = stocks::negative(neg);
        const stocks::PrintDef& p = stocks::print(prt);
        bool mono = n.kind == stocks::kBW;
        NegDyes D = negDyes(n);
        double dmax = negLutMax(n);
        std::vector<double> F[3];
        negFactors(D, dmax, F);
        Spectrum L = printerLamp();
        Spectrum S[3];
        for (int j = 0; j < 3; ++j) S[j] = spec::lobes(p.sens[j]);
        if (mono) { Spectrum t{}; for (int l = 0; l < NWL; ++l) t[size_t(l)] = S[0][size_t(l)] + S[1][size_t(l)] + S[2][size_t(l)]; S[0] = S[1] = S[2] = t; }
        double w[3][NWL];
        for (int j = 0; j < 3; ++j) {
            Spectrum t = mul(S[j], L);
            double s = sum(t);
            for (int l = 0; l < NWL; ++l) w[j][l] = t[size_t(l)] / s;
        }
        auto b = std::make_shared<DyeLutBake>();
        b->lut = dyeLut(F, D.base, w, dmax);
        return std::shared_ptr<const DyeLutBake>(b);
    });
}

std::shared_ptr<const DyeLutBake> scanLut(int neg) {
    return cached<DyeLutBake>(2, neg, 0, [neg] {
        const stocks::NegDef& n = stocks::negative(neg);
        NegDyes D = negDyes(n);
        double dmax = negLutMax(n);
        std::vector<double> F[3];
        negFactors(D, dmax, F);
        Spectrum L = spec::illuminant(stocks::lamps().scanner);
        double w[3][NWL];
        for (int j = 0; j < 3; ++j) {
            Spectrum t = mul(spec::lobes(stocks::scanner().sens[j]), L);
            double s = sum(t);
            for (int l = 0; l < NWL; ++l) w[j][l] = t[size_t(l)] / s;
        }
        auto b = std::make_shared<DyeLutBake>();
        b->lut = dyeLut(F, D.base, w, dmax);
        return std::shared_ptr<const DyeLutBake>(b);
    });
}

void printCurves(int prt, bool mono, std::vector<float>& out) {
    const stocks::PrintDef& p = stocks::print(prt);
    out.resize(size_t(3) * kNCurve);
    for (int j = 0; j < 3; ++j) {
        const stocks::CurveDef& c = mono ? p.curves[1] : p.curves[j];
        for (int i = 0; i < kNCurve; ++i) {
            double u = kPrtU0 + (kPrtU1 - kPrtU0) * i / (kNCurve - 1);
            out[size_t(j) * kNCurve + i] = float(c.fog + dev(u - c.x0, c.gamma, c.k, c.R, c.p));
        }
    }
}

static Lut projection(const Spectrum eps[3], const Spectrum& base, double dmax, const Spectrum& lamp, const double fog[3]) {
    const int N = kNLut3;
    std::vector<double> F[3];
    for (int i = 0; i < 3; ++i) {
        F[i].resize(size_t(N) * NWL);
        for (int g = 0; g < N; ++g) {
            double d = dmax * g / (N - 1);
            for (int l = 0; l < NWL; ++l) F[i][size_t(g) * NWL + l] = std::pow(10.0, -(d * eps[i][size_t(l)]));
        }
    }
    Spectrum clear{};
    for (int l = 0; l < NWL; ++l)
        clear[size_t(l)] = lamp[size_t(l)] * std::pow(10.0, -(base[size_t(l)] + fog[0] * eps[0][size_t(l)] + fog[1] * eps[1][size_t(l)] + fog[2] * eps[2][size_t(l)]));
    double Yb = spec::XYZof(clear)[1];
    const double (*C)[3] = spec::cmf();
    double w[3][NWL];
    for (int c = 0; c < 3; ++c)
        for (int l = 0; l < NWL; ++l) w[c][l] = C[l][c] * lamp[size_t(l)] / Yb;
    return dyeLut(F, base, w, dmax);
}

std::shared_ptr<const ProjBake> projectionLut(int prt, bool mono) {
    return cached<ProjBake>(3, prt, mono ? 1 : 0, [prt, mono] {
        const stocks::PrintDef& p = stocks::print(prt);
        Spectrum lamp = projectorLamp();
        Spectrum base = baseDensity(p.base);
        Spectrum eps[3];
        if (mono) {
            for (int i = 0; i < 3; ++i) eps[i].fill(1.0 / 3.0);
        } else {
            for (int i = 0; i < 3; ++i) {
                Spectrum a = spec::lobes(p.dyes[i].primary), s = spec::lobes(p.dyes[i].secondary);
                for (int l = 0; l < NWL; ++l) eps[i][size_t(l)] = a[size_t(l)] + s[size_t(l)];
            }
            double sc[3];
            visualEndScale(eps, base, lamp, sc);
            // fading happens after printing: on top of the fresh-dye END normalisation
            for (int i = 0; i < 3; ++i) for (auto& v : eps[i]) v *= sc[i] * p.fade[i];
        }
        auto b = std::make_shared<ProjBake>();
        std::vector<float> pc;
        printCurves(prt, mono, pc);
        double fog[3];
        for (int j = 0; j < 3; ++j) { fog[j] = pc[size_t(j) * kNCurve]; b->pmin[j] = fog[j]; }
        b->lut = projection(eps, base, printLutMax(p), lamp, fog);
        b->cat = catToD65(lamp);
        b->pcurve = std::move(pc);
        return std::shared_ptr<const ProjBake>(b);
    });
}

std::shared_ptr<const ProjBake> reversalLut(int neg) {
    return cached<ProjBake>(4, neg, 0, [neg] {
        const stocks::NegDef& n = stocks::negative(neg);
        NegDyes D = negDyes(n);
        Spectrum lamp = projectorLamp();
        double fog[3];
        for (int i = 0; i < 3; ++i) fog[i] = curveEval(n.curves[i], kNegX1, true);   // nominal D-min (clear end)
        auto b = std::make_shared<ProjBake>();
        for (int i = 0; i < 3; ++i) b->pmin[i] = fog[i];
        b->lut = projection(D.eps, D.base, negLutMax(n), lamp, fog);
        b->cat = catToD65(lamp);
        return std::shared_ptr<const ProjBake>(b);
    });
}

}}} // namespace cel::core::bake
