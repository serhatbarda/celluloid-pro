// Celluloid Pro - designed stock library (host side). Data generated from reference/stocks.json
// by reference/gen_tables.py into StockData.inc. All spectra are parametric designs (sums of
// asymmetric Gaussian lobes), not digitised manufacturer data (SPEC section 1).
#pragma once
#include "Spectral.h"

namespace cel { namespace core { namespace stocks {

using spec::IllumDef;
using spec::LobeSet;

struct CurveDef { double fog, gamma, k, x0, R, p; };   // D = fog + dev(+-(x - x0)); see Bake.cpp
struct DyeDef { LobeSet primary, secondary; };         // density per unit amount; secondary = maskable part
struct BaseDef { double d0, blue, red; };

enum Kind { kNegative = 0, kReversal = 1, kBW = 2 };

struct NegDef {
    const char* name;
    const char* description;
    int kind;
    IllumDef balance;                 // design illuminant (daylight / tungsten balance)
    double grainSizeScale, halationScale, bloomScale;
    LobeSet sens[3];                  // R-, G-, B-sensitive layer (B&W: identical)
    CurveDef curves[3];
    DyeDef dyes[3];                   // cyan, magenta, yellow (B&W: unused)
    double mask[3];                   // coloured-coupler masking strength per dye
    BaseDef base;
    double inter[3][3];               // interlayer (DIR) inhibition of layer i by layer j
    double gran[3];                   // granularity (sigma at amount 1)
    double silver;                    // retained-silver density factor (bleach bypass)
    int hasSilverSpectrum;
    LobeSet silverSpectrum;           // B&W image silver spectral density shape
    double greyY;                     // reversal: display Y of 18% grey
    double scanMatrix[3][3];          // Lab Scan colour matrix (identity = none)
    double scanRoot[3][3];            // Lab Scan root-polynomial terms (sqrt(rg), sqrt(gb), sqrt(rb))
    double scanCrossover;             // Lab Scan: share of the negative's own crossover kept (0 = neutral)
    double scanContrast;              // Lab Scan: stock profile contrast (1 = G-layer grey gamma)
    double hueHold, chromaHold;       // reversal: hue hold (see ColorKernels.h celc_hold)
};

struct PrintDef {
    const char* name;
    const char* description;
    LobeSet sens[3];
    DyeDef dyes[3];
    CurveDef curves[3];
    BaseDef base;
    double greyY;                     // display Y of 18% grey (points at 0)
    double silver;                    // built-in silver retention 0..1
    double silverDensity;
    double fade[3];                   // dye fading factors (archive look)
    double hueHold, chromaHold;       // hue hold strength / chroma hold (ColorKernels.h celc_hold)
    LobeSet monoTone;                 // B&W image tone on this print: flat + these lobes (unit mean)
};

struct ScannerDef { LobeSet sens[3]; double gamma, k, R, p; double greyY; double hueHold, chromaHold; };
struct LampSet { IllumDef printer, projector, scanner; };

int numNegatives();
int numPrints();
const NegDef& negative(int i);       // clamped index
const PrintDef& print(int i);
const ScannerDef& scanner();
const LampSet& lamps();
const char* libraryHash();           // hash of stocks.json the tables were generated from

}}} // namespace cel::core::stocks
