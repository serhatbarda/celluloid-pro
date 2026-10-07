// Celluloid Pro - host-side bakes of the spectral film model (double precision) and the
// process-wide bake cache. Mirrors reference/model.py. Private to core.
#pragma once
#include "Spectral.h"
#include "Stocks.h"
#include <memory>
#include <string>
#include <vector>

namespace cel { namespace core { namespace bake {

// model constants (== reference/model.py and src/kernels/ColorState.h)
constexpr int kNCurve = 1024;
constexpr double kNegX0 = -7.0, kNegX1 = 4.0;
constexpr double kPrtU0 = -5.0, kPrtU1 = 3.0;
constexpr double kScnW0 = -6.0, kScnW1 = 4.0;
constexpr double kScnXA = -6.0, kScnXB = 3.5;     // neutral ramp sampled for the scan inversion
constexpr int kScnNFwd = 1024;
constexpr int kNLut3 = 64;            // printer / scanner dye LUTs (projection is integrated per pixel)
constexpr double kLayerW[3] = {0.30, 0.50, 0.20};
constexpr double kLog10Grey = -0.7447274948966939;
constexpr double kPoint = 0.025;
constexpr double kHdrGreyNits = 15.0;
constexpr int kHoldN = 49;                         // hue-hold anchor grid (octahedral)
constexpr int kHoldNX = 512;                       // hue-hold neutral table

double dev(double y, double gamma, double k, double R, double p);
stocks::CurveDef pushCurve(const stocks::CurveDef& c, double push, bool reversal);
double curveEval(const stocks::CurveDef& c, double x, bool reversal);

spec::Spectrum baseDensity(const stocks::BaseDef& b);

struct Lut { std::vector<float> data; int n = 0; double maxv = 0.0; };

// cached blocks (immutable once built)
struct ExposureBake { Lut oct; };                       // octahedral log10 K LUT, N x N x 3
struct DyeLutBake { Lut lut; };                         // N^3 x 3 log10 exposures / XYZ
struct ProjBake { Lut lut; /* spectral PJ table K x 7, n = K, maxv = dye domain */ spec::Mat3 cat; double pmin[3]; std::vector<float> pcurve; };   // pcurve: print curves 3 x kNCurve

std::shared_ptr<const ExposureBake> exposureLut(int neg);
std::shared_ptr<const DyeLutBake> printLut(int neg, int prt);        // printer: neg dyes -> print log H
std::shared_ptr<const DyeLutBake> scanLut(int neg);                  // scanner: neg dyes -> log10 channel exposure
std::shared_ptr<const ProjBake> projectionLut(int prt, bool mono);   // print dyes -> spectral projection table
std::shared_ptr<const ProjBake> reversalLut(int neg);                // reversal dyes -> spectral projection table

double negLutMax(const stocks::NegDef& n);
double printLutMax(const stocks::PrintDef& p);
spec::Mat3 catToD65(const spec::Spectrum& lamp);

// print curves (identical for every call with the same stock) incl. the mono (B&W) variant
void printCurves(int prt, bool mono, std::vector<float>& out /* 3 x kNCurve */);

void clearCache();                                      // tests / timing

}}} // namespace cel::core::bake
