// Celluloid Pro - spectral foundations (host side, double precision). Mirrors reference/spectral.py.
#pragma once
#include <array>

namespace cel { namespace core { namespace spec {

constexpr int NWL = 81;                 // 380..780 nm, 5 nm
using Spectrum = std::array<double, NWL>;

struct Mat3 {
    double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    static Mat3 diag(double a, double b, double c);
    Mat3 operator*(const Mat3& o) const;
    std::array<double, 3> operator*(const std::array<double, 3>& v) const;
    Mat3 inverse() const;
};
using Vec3 = std::array<double, 3>;

double wavelength(int i);               // nm
double tnorm(int i);                    // (wl - 580) / 200
const double (*cmf())[3];               // CIE 1931 2 deg, [81][3]
const Spectrum& d65();

struct Lobe { double mu, sl, sr, amp; };
struct LobeSet { int n; Lobe l[6]; };
Spectrum lobes(const LobeSet& s);

// light sources (type 0 blackbody, 1 CIE daylight, 2 D65), optional smooth UV / IR edges
struct IllumDef { int type; double cct; double uv0, uv1; double ir0, ir1, irdepth; };
Spectrum illuminant(const IllumDef& d);   // normalised to max 1
Spectrum planck(double T);
Spectrum daylight(double cct);

Vec3 XYZof(const Spectrum& s);          // plain sums against the CMFs

// colour matrices
enum class Gamut { Rec2020, Rec709, P3D65, DCIP3, FGamut, FGamutC, DWG, AWG3, AWG4, SGamut3Cine, VGamut,
                   CinemaGamut, BMDWG5, AP1 };
Mat3 npm(Gamut g);                      // RGB -> XYZ
Vec3 whiteXYZ(Gamut g);                 // Y = 1
Vec3 xyToXYZ(double x, double y, double Y = 1.0);
Mat3 catMatrix(const Vec3& src, const Vec3& dst, bool cat16);   // von Kries, D = 1 (CAT16 or Bradford)
Mat3 rgbToRgb(Gamut src, Gamut dst);    // Bradford when whites differ

}}} // namespace cel::core::spec
