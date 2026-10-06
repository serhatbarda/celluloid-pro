// Celluloid Pro - spectral foundations (host side). Mirrors reference/spectral.py.
#include "Spectral.h"
#include <algorithm>
#include <cmath>

namespace cel { namespace core { namespace spec {

namespace {
#include "SpectralTables.inc"
}

Mat3 Mat3::diag(double a, double b, double c) {
    Mat3 r;
    r.m[0][0] = a; r.m[1][1] = b; r.m[2][2] = c;
    return r;
}
Mat3 Mat3::operator*(const Mat3& o) const {
    Mat3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double s = 0;
            for (int k = 0; k < 3; ++k) s += m[i][k] * o.m[k][j];
            r.m[i][j] = s;
        }
    return r;
}
Vec3 Mat3::operator*(const Vec3& v) const {
    return {m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2], m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
            m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]};
}
Mat3 Mat3::inverse() const {
    const double (&a)[3][3] = m;
    double det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
                 a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    Mat3 r;
    double id = 1.0 / det;
    r.m[0][0] = (a[1][1] * a[2][2] - a[1][2] * a[2][1]) * id;
    r.m[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * id;
    r.m[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * id;
    r.m[1][0] = (a[1][2] * a[2][0] - a[1][0] * a[2][2]) * id;
    r.m[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * id;
    r.m[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * id;
    r.m[2][0] = (a[1][0] * a[2][1] - a[1][1] * a[2][0]) * id;
    r.m[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * id;
    r.m[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * id;
    return r;
}

double wavelength(int i) { return 380.0 + 5.0 * i; }
double tnorm(int i) { return (wavelength(i) - 580.0) / 200.0; }
const double (*cmf())[3] { return kCMF; }
const Spectrum& d65() {
    static const Spectrum s = [] { Spectrum r{}; for (int i = 0; i < NWL; ++i) r[size_t(i)] = kD65[i]; return r; }();
    return s;
}

Spectrum lobes(const LobeSet& s) {
    Spectrum out{};
    for (int k = 0; k < s.n; ++k) {
        const Lobe& L = s.l[k];
        for (int i = 0; i < NWL; ++i) {
            double w = wavelength(i);
            double sig = w < L.mu ? L.sl : L.sr;
            double t = (w - L.mu) / sig;
            out[size_t(i)] += L.amp * std::exp(-0.5 * t * t);
        }
    }
    return out;
}

static const double kC2 = 1.4388e-2;
Spectrum planck(double T) {
    Spectrum s{};
    for (int i = 0; i < NWL; ++i) {
        double lam = wavelength(i) * 1e-9;
        s[size_t(i)] = 1.0 / (std::pow(lam, 5.0) * (std::exp(kC2 / (lam * T)) - 1.0));
    }
    return s;
}

// CIE 015:2018 daylight locus and basis functions (M1, M2 rounded to 3 decimals as the CIE recommends)
Spectrum daylight(double T) {
    double xd = T <= 7000.0 ? -4.6070e9 / (T * T * T) + 2.9678e6 / (T * T) + 0.09911e3 / T + 0.244063
                            : -2.0064e9 / (T * T * T) + 1.9018e6 / (T * T) + 0.24748e3 / T + 0.237040;
    double yd = -3.0 * xd * xd + 2.870 * xd - 0.275;
    double mm = 0.0241 + 0.2562 * xd - 0.7341 * yd;
    double m1 = (-1.3515 - 1.7703 * xd + 5.9114 * yd) / mm;
    double m2 = (0.0300 - 31.4424 * xd + 30.0717 * yd) / mm;
    m1 = std::round(m1 * 1000.0) / 1000.0;
    m2 = std::round(m2 * 1000.0) / 1000.0;
    Spectrum s{};
    for (int i = 0; i < NWL; ++i) s[size_t(i)] = kDaylightBasis[i][0] + m1 * kDaylightBasis[i][1] + m2 * kDaylightBasis[i][2];
    return s;
}

static double smoothEdge(double w, double lo, double hi) {
    double t = std::min(1.0, std::max(0.0, (w - lo) / (hi - lo)));
    return t * t * (3.0 - 2.0 * t);
}

Spectrum illuminant(const IllumDef& d) {
    Spectrum s = d.type == 0 ? planck(d.cct) : (d.type == 1 ? daylight(d.cct) : d65());
    for (int i = 0; i < NWL; ++i) {
        double w = wavelength(i);
        if (d.uv0 >= 0.0) s[size_t(i)] *= smoothEdge(w, d.uv0, d.uv1);
        if (d.ir0 >= 0.0) s[size_t(i)] *= 1.0 - smoothEdge(w, d.ir0, d.ir1) * d.irdepth;
    }
    double mx = *std::max_element(s.begin(), s.end());
    for (auto& v : s) v /= mx;
    return s;
}

Vec3 XYZof(const Spectrum& s) {
    Vec3 r{0, 0, 0};
    for (int i = 0; i < NWL; ++i)
        for (int c = 0; c < 3; ++c) r[size_t(c)] += s[size_t(i)] * kCMF[i][c];
    return r;
}

Vec3 xyToXYZ(double x, double y, double Y) { return {x / y * Y, Y, (1.0 - x - y) / y * Y}; }

struct Prim { double r[2], g[2], b[2], w[2]; };
static Prim prim(Gamut g) {
    const double D65x = 0.3127, D65y = 0.3290;
    switch (g) {
    case Gamut::Rec2020: case Gamut::FGamut: return {{0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}, {D65x, D65y}};
    case Gamut::Rec709: return {{0.640, 0.330}, {0.300, 0.600}, {0.150, 0.060}, {D65x, D65y}};
    case Gamut::P3D65: return {{0.680, 0.320}, {0.265, 0.690}, {0.150, 0.060}, {D65x, D65y}};
    case Gamut::DCIP3: return {{0.680, 0.320}, {0.265, 0.690}, {0.150, 0.060}, {0.314, 0.351}};
    case Gamut::FGamutC: return {{0.7347, 0.2653}, {0.0263, 0.9737}, {0.1173, -0.0224}, {D65x, D65y}};
    case Gamut::DWG: return {{0.8000, 0.3130}, {0.1682, 0.9877}, {0.0790, -0.1155}, {D65x, D65y}};
    case Gamut::AWG3: return {{0.6840, 0.3130}, {0.2210, 0.8480}, {0.0861, -0.1020}, {D65x, D65y}};
    case Gamut::AWG4: return {{0.7347, 0.2653}, {0.1424, 0.8576}, {0.0991, -0.0308}, {D65x, D65y}};
    case Gamut::SGamut3Cine: return {{0.766, 0.275}, {0.225, 0.800}, {0.089, -0.087}, {D65x, D65y}};
    case Gamut::VGamut: return {{0.730, 0.280}, {0.165, 0.840}, {0.100, -0.030}, {D65x, D65y}};
    case Gamut::CinemaGamut: return {{0.740, 0.270}, {0.170, 1.140}, {0.080, -0.100}, {D65x, D65y}};
    case Gamut::BMDWG5: return {{0.7177215, 0.3171181}, {0.2280410, 0.8615690}, {0.1005841, -0.0820452}, {0.3127170, 0.3290312}};
    case Gamut::AP1: return {{0.713, 0.293}, {0.165, 0.830}, {0.128, 0.044}, {0.32168, 0.33767}};
    }
    return {{0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}, {D65x, D65y}};
}

Vec3 whiteXYZ(Gamut g) { Prim p = prim(g); return xyToXYZ(p.w[0], p.w[1]); }

Mat3 npm(Gamut g) {
    Prim p = prim(g);
    Vec3 R = xyToXYZ(p.r[0], p.r[1]), G = xyToXYZ(p.g[0], p.g[1]), B = xyToXYZ(p.b[0], p.b[1]);
    Mat3 P;
    for (int i = 0; i < 3; ++i) { P.m[i][0] = R[size_t(i)]; P.m[i][1] = G[size_t(i)]; P.m[i][2] = B[size_t(i)]; }
    Vec3 S = P.inverse() * xyToXYZ(p.w[0], p.w[1]);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) P.m[i][j] *= S[size_t(j)];
    return P;
}

Mat3 catMatrix(const Vec3& src, const Vec3& dst, bool cat16) {
    Mat3 C;
    static const double kBradford[3][3] = {{0.8951, 0.2664, -0.1614}, {-0.7502, 1.7135, 0.0367}, {0.0389, -0.0685, 1.0296}};
    static const double kCat16[3][3] = {{0.401288, 0.650173, -0.051461}, {-0.250268, 1.204414, 0.045854},
                                        {-0.002079, 0.048952, 0.953127}};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) C.m[i][j] = cat16 ? kCat16[i][j] : kBradford[i][j];
    Vec3 s = C * src, d = C * dst;
    return C.inverse() * Mat3::diag(d[0] / s[0], d[1] / s[1], d[2] / s[2]) * C;
}

Mat3 rgbToRgb(Gamut src, Gamut dst) {
    Mat3 M = npm(src);
    Prim ps = prim(src), pd = prim(dst);
    if (std::fabs(ps.w[0] - pd.w[0]) > 1e-9 || std::fabs(ps.w[1] - pd.w[1]) > 1e-9)
        M = catMatrix(xyToXYZ(ps.w[0], ps.w[1]), xyToXYZ(pd.w[0], pd.w[1]), false) * M;
    return npm(dst).inverse() * M;
}

}}} // namespace cel::core::spec
