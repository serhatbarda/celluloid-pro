"""Write docs/STOCKS.md (model documentation + stock catalogue + measured numbers).

    python3 reference/report.py

All numbers in the generated file are measured by the NumPy reference model (which tests/core
checks against the C++ implementation to < 1e-5).
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design  # noqa: E402
import gen_tables  # noqa: E402
import model as m  # noqa: E402
import scenes  # noqa: E402
import spectral as sp  # noqa: E402

ROOT = os.path.dirname(sp.HERE)
OUT = os.path.join(ROOT, 'docs', 'STOCKS.md')

MODEL = r'''
## 1. The model in one page

```
camera code values ──decode──► scene-linear Rec.2020 (× 2^exposure)
   │  (texture passes: weave, acutance, halation, bloom, vignette, grain field)
   ▼
spectral upsampling (octahedral direction LUT, magnitude = L1 norm)  ──►  layer exposures E_R,E_G,E_B
   ▼
interlayer / DIR inhibition (luminance-neutral)  ──►  negative curves (push / pull / neutralize)
   ▼                                                         ──►  dye densities  + grain  + [bleach: silver]
PRINT:    printer lamp × T_neg(λ) × print sensitivities  (64³ LUT)  ──► print curves ──► print dyes [+ silver]
          ──► projector lamp × T_print(λ) × CIE 1931 CMFs (per-pixel 41-sample integral) ──► CAT16 → D65
LAB SCAN: Status-M-like scanner (64³ LUT) ──► neutral-referenced balanced inversion (crossover, contrast)
          ──► lab matrix + root-polynomial terms ──► scanner curve
REVERSAL: positive curves ──► reversal dyes ──► projection (per-pixel integral)
   ▼
display XYZ (white 1.0) ──► hue hold ──► output primaries ──► smooth gamut compression ──► SDR limiter | HDR unroll ──► EOTF⁻¹
```

All spectral design work happens on the host in double precision at 380–780 nm / 5 nm
(81 samples). The printer and scanner steps are baked into 64³ LUTs; the projection (print or
reversal original → display XYZ) is integrated **per pixel** over 41 samples (10 nm, trapezoid
weights) so smooth gradients stay smooth after any later grade (no LUT ripple). The per-pixel
kernel (`src/kernels/ColorKernels.h`, identical on CPU, OpenCL, CUDA and Metal) costs a 2D
lookup, ~a dozen 1D lookups, one trilinear 3D lookup, the 41-term projection sum, the hue hold
and a few matrices (≈ 2× the cost of the former two-LUT kernel). Reference implementation: `reference/model.py` (NumPy, float64); the C++ kernels agree
with it to 5e-6 on encoded output (tests/core).

### 1.1 Input
Every input space is decoded with the maker's published transfer function (constants and
document citations in `src/kernels/ColorTransfer.h`, cross-checked against colour-science),
then converted to Rec.2020 with a matrix derived in double precision from the published
primaries/white (Bradford adaptation only for ACES AP1's D60-ish white). F-Gamut C uses the
primaries R(0.7347, 0.2653) G(0.0263, 0.9737) B(0.1173, −0.0224), D65.

### 1.2 Spectral upsampling (scene RGB → stimulus)
*Method.* A Jakob–Hanika-style sigmoid-polynomial reflectance
`R(λ) = sigmoid(c0 + c1·t + c2·t²)`, `t = (λ − 580)/200`, is fitted **exactly** to every
chromaticity, at half the optimal-colour (MacAdam) luminance of that chromaticity under D65
— i.e. the smooth, bounded "typical surface" spectrum of that colour: reds become long-pass
plateaus, greens/blues band-pass bumps, purples band-stops. The stimulus is `a·R(λ)·D65(λ)`.
*Red sector.* At half the MacAdam luminance a saturated red can only be fitted by a U-shaped
quadratic that reflects strongly again below 420 nm (the chart red had R(400 nm) = 0.27,
R(380 nm) = 0.52). Real red surfaces and red lights have almost no violet; the fake violet
exposed the blue-sensitive layer, so bright reds slid toward pink/magenta on every stock. The
brightness fraction is therefore lowered smoothly around dominant red
(`0.5 − 0.32·exp(−½(Δh/8°)²)`, xy hue angle about D65 centred at −5°): the same chromaticity
gets a clean long-pass spectrum (chart red R(400 nm) 0.07). Skin (+22°), orange (+31°) and
magenta (−55°) are outside the dip and keep their spectra.
*Why this and not a linear basis.* A linear basis makes the whole camera→film step a 3×3
matrix (no spectral behaviour at all) and produces negative spectra outside its triangle; an
unbounded exp-quadratic family (tested first) pushes saturated reds' energy beyond 700 nm where
film is blind and renders them too dark. The bounded sigmoid family gives plausible spectra
for every real colour and stays smooth.
*Domain.* The table is indexed by the **direction** of the RGB vector (octahedral map of an
opponent basis, 97×97, odd so that texel lines lie on the map's two kink lines through the
neutral axis): it covers all of RGB space incl. negative and out-of-gamut values. Exposure is
`L1(rgb) × K(direction)`, hence exactly homogeneous (`E(k·rgb) = k·E(rgb)`, tested to 1e-7).
*Out of gamut.* Chromaticity is soft-clipped toward D65 along the dominant-wavelength line
(relative purity identity up to 0.65, C1 tanh roll-off to 0.92 of the spectral locus), luminance
kept (with a smooth floor 0.03·(X⁺+Y⁺+Z⁺) for imaginary colours whose Y is tiny/negative);
directions dominated by negative tristimulus values fade smoothly to black. No NaN, no flips.
*Film balance.* The reflectance is re-lit by the stock's design illuminant (D55 daylight or
3200 K tungsten) and integrated against each layer's sensitivity, normalised so that 18 % grey
exposes every layer equally (camera white balance → film balance).
The table is stock-independent and generated offline (`reference/gen_upsampling.py`).

### 1.3 Negative development
* **Curves** per layer, in log10 exposure x: `D = fog + dev(x − x0)`,
  `dev(y) = r / (1 + (r/R)^p)^(1/p)`, `r = γ·softplus(k·y)/k` — toe softness k, straight-line
  γ, shoulder sharpness p, maximum R (monotonic, C∞). Densities are in **equivalent neutral
  density (END)** units: the dye spectra are scaled (Newton solve at bake) so that equal amounts
  give equal printing density (negatives, P1 sensitivities under the printer lamp) or a visual
  neutral (prints, reversal). Identical curves therefore print neutral; per-layer differences
  are deliberate crossover character (designed with `reference/optimize.py`).
* **Push/pull** (−2…+3): γ × (1 + 0.12·push) (0.10 when pulling), speed +0.10 log/stop,
  fog +0.035/stop (pull: −0.01/stop); grain × 1.25^push. Reversal: speed and D-max shift instead.
* **Interlayer / DIR**: `Δx_i = −s·Σ_j K_ij·c_j`, `c_j = (D_j(x_j) − D_j(x_ref))/γ_j` with
  `x_ref = 0.3·x_R + 0.5·x_G + 0.2·x_B`; then the weighted mean of Δ is removed (luminance
  neutral). Neutrals are unchanged exactly; the effect saturates naturally in toe/shoulder.
  s = user `interlayer` (0…2).
* **Neutralize** blends each layer's grey-normalised curve shape toward the mean shape.
* **Masking couplers**: the unwanted (secondary) absorption of the cyan and magenta dyes is
  compensated by coloured couplers that are consumed as dye forms — the orange mask emerges
  from the model (mask strength per stock).
* **Grain** (amount from `LookParams.grainAmount`, spatial field from the texture engine):
  `D_i += σ_i·amount·1.25^push·sqrt(max(D_i − Dmin_i, 0) + 0.02)·n_i`; blue layer coarsest.
* **Bleach bypass (negative)**: retained silver = neutral density `0.45·Σw(D − Dmin)` — exactly
  a log-exposure offset for the printer, so it needs no extra LUT dimension.

### 1.4 Print, projection, scan, reversal, B&W
* **Printing**: `log H_j = log10 Σ lamp·T_neg(λ)·S_print,j(λ)` baked as a 64³ LUT over the
  negative dye amounts; **printer points**: +1 point = +0.025 log H on that layer (more red
  light → more cyan dye → *less* red in the image); `printExposure` +1 = 1 point less light on
  all layers (= brighter print).
* **Grey balance** (every prepare, points 0, exposure 0): per-layer printer offsets solved by
  Newton on the actual kernel so that scene 18 % grey is D65-neutral at the print's target Y.
  The lab re-times the print for push/pull/bleach, so those change contrast and colour, not
  the grey level.
* **Print curves** γ ≈ 2.55–3.75; print bleach = retained silver `≈0.6·Σw(P − Pmin)` (P5 has
  0.6 of it built in).
* **Projection**: `XYZ = Σ_k w(k)·10^−(base(k) + Σ_i D_i·ε_i(k) + silver)` over 41 samples, the
  weights w = projector·CMF·Δλ (computed per pixel, no LUT); Y normalised so
  the clear (unexposed, processed) print — base + dye fog — is display white 1.0. Chromatic
  adaptation from the projector white (lamp through a perfectly clear base) to D65 with
  **CAT16** (von Kries, D = 1): the current CIE recommendation, linear and well behaved for the
  large warm→D65 shift; print base tints (e.g. P4's yellowed base) stay visible.
* **Lab Scan**: Status-M-like narrow-band scanner (448/545/652 nm) reads the negative (64³
  LUT); *neutral-referenced balanced inversion*: each channel is mapped through the neutral
  ramp onto the green channel's density scale (per-density grey balance, like a lab scanner),
  so neutrals stay neutral at every exposure while the negative's latitude is kept. A per-stock
  share of the negative's own crossover is kept (`scanCrossover`, scanner default 0.3) and a
  per-stock `scanContrast` scales the inversion, so each negative scans with its own tonal
  character. Colour correction is a root-polynomial profile: a 3×3 matrix plus a 3×3 on
  (√(r·g), √(g·b), √(r·b)), every row summing to 1 (neutrals and exposure homogeneity kept),
  root terms bounded to ±0.3. It is fitted per stock (`reference/optimize.py scan`) to that
  stock's own P1 print rendition (saturation and hue bias from its brief), with skies and cyans
  held on the input hue and yellows weighted 2–4×, so the scans differ by stock (smallest
  ΔE2000 between two scans ≥ 2.1) instead of all looking alike. A gentle scanner curve (γ @SG@,
  shoulder p @SP@) gives the flatter scan look: open, but not milky blacks (−10 stops ≈ 0.08
  encoded, a print ≈ 0.05).
  Printer points act as scanner balance (1 point ≈ the same display change as in print).
* **Reversal**: positive curves (`D = Dmin + dev(x0 − x)`), reversal dyes → projection LUT;
  grey balance via per-layer exposure offsets (CC filtration). Print stock and Process are
  ignored.
* **B&W**: one panchromatic layer (three identical layers internally), silver image; printed
  as a neutral silver image on the chosen print stock (its green-layer curve and its base tint),
  so B&W on P4 is warm-toned; P4 additionally has a two-lobe `monoTone` absorption (470 nm and
  540 nm, scaled by density) that gives its B&W prints a sepia tone (a* +1.6, b* +5.3 at mid grey). Grain is monochrome (the colour kernel uses the G noise channel).

### 1.5 Hue hold
Every per-layer log-domain tone scale bends a colour's path toward white in the highlights and
toward black in the shadows (orange slides to yellow, red to pink, sky blue to cyan, skin to
yellow-orange). The hold keeps the film's *mid-grey rendering* of each scene chromaticity as the
hue anchor at every exposure: a 49² octahedral table stores the CIELAB (a, b) of the film's
rendering of each chromaticity at 18 % grey luminance relative to the grey neutral, and a 512-entry
table the film's neutral (incl. printer lights and user offsets) over the reference exposure. Per
pixel, the colour's (a, b) relative to its own neutral is turned toward the anchor hue by
`t = hold·w·(1 − exp(−1.3·stops²))` (zero at mid grey, so the film's own mid-tone colour is
untouched), faded out for near-neutral colours (|anchor| < ~2 ΔE) and for nearly opposite hues
(90°–160°, keeps the map continuous); chroma is raised by `1 + chromaHold·t` (30 % of that in the
shadows, none for colours already near the gamut boundary). Y is unchanged, so tone scale and
monotonicity are exactly those of the film. `hueHold`/`chromaHold` belong to the stock that is viewed:
the print stock (1.0/0.8), the reversal stock (1.0/0.8) or the Lab Scan scanner (1.0/1.0). Exact CIELAB f with a* weighted 2.5× b*, so the
angles are CIELAB hue angles.

### 1.6 Output
Display XYZ → output RGB (P3-DCI: DCI white encoding with D65 white simulated, scaled so D65
white's largest channel is 1). **Gamut compression**: hue-preserving desaturation toward the
luminance axis — saturation ratio `s = (Y − min)/Y` (min = smooth minimum of the three channels,
ε = 0.02·Y, so there is no slope kink where the smallest channel changes) mapped by
`s' = 0.80 + 0.20·x/√(1+x²)`, `x = (s − 0.80)/0.20` above 0.80 (C1, asymptote at the gamut
boundary, no clipping steps). **SDR**: per-channel C1 tanh limiter from 0.95 to 1.0 (output never
exceeds 1.0, monotonic per channel). **HDR**: mid grey → 15 cd/m² (scaled from the SDR grey of
the same look); highlight **unroll** multiplies luminance by `10^(u·G)`, `G = Gmax·t²/(1+t²)`,
`t = max(e − 0.60, 0)/0.60`, e = log10 scene exposure over grey, Gmax calibrated so that +10 stops
reaches the peak — monotonic, C1 (per-channel limiter from 0.9·peak). Highlights open up
earlier and brighter than in v1.0 (+6 stops 534 → 680 cd/m², +10 stops 807 → 976 cd/m² at a
1000 cd/m² peak). PQ per SMPTE ST 2084
(`hdrPeakNits`); HLG per ITU-R BT.2100 with the 1000 cd/m² reference display (system γ 1.2,
inverse OOTF on BT.2020 luminance), peak fixed at 1000, signal clipped to 1. Decoding replaces NaN by 0 and clamps ±Inf
to ±65504 (an absurdly large code value renders white, never black).

### 1.7 Kernel / state contract (for the engine, GPU and texture code)
* State layout: `src/kernels/ColorState.h` (layout 3.0: 128-float header + OCT, NC, PL, PC, PJ/SC
  and hold HA/HN blocks; PJ holds K×7 floats [base, ε0..2, wX, wY, wZ]).
  `Pipeline::stateVersion()` comes from one process-wide counter (unique across instances).
* `color` / `display_finish`: in1 = grain field, xyz = R-, G-, B-sensitive layer, zero mean,
  unit variance; used when `inStride[1] != 0`. in2 = alpha source when `inStride[2] != 0`.
  The kernels do **not** apply the texture engine's per-format amplitude
  (`tex::grainFormatAmplitude`); fold it into the field or `LookParams.grainAmount` once.
* Texture-Only: `display_decode` gives display-linear light in the output primaries
  (SDR: 1.0 = display white; PQ/HLG: cd/m² / 100); `display_finish` adds grain in density
  (fading out toward SDR white, never pushing a legal value above 1.0) and encodes; with grain
  off the pair is an identity (≤ 1e-5).
* Cost of `prepare()`: cache hit ≈ 0.3 ms; full bake of a new stock pair ≈ 35 ms on one core
  (`CEL_BAKE_THREADS=1`); the hold anchor is re-baked only when the film changes (not for
  printer lights, exposure or output changes).
'''


REVISION = r'''## 5. Look-phase revision (v1.0)

Reviewed by rendering every pair through the real plug-in on real-world-like scenes (skin under
tungsten and daylight at −2/0/+2 stops, golden hour, night city with neon and sodium light,
foliage, overcast exterior, high-key and low-key interiors). Changes:

* **Spectral upsampling, red sector** (section 1.2): bright reds no longer slide to pink. Chart
  red at +3 stops on CN 200T / P1: hue 10° → 21° (input 28°); a Rec.709-red surface now goes
  slightly orange in its highlights (36° → 49° at +5 stops) as print film does.
* **Highlight colour** of CN 500T, CN 800T, CN Vintage 85, C41 Portrait 400, C41 Consumer 200:
  the warm highlights were pink/mauve (a* ≈ +1.2…+1.5 at +4 stops); the curve crossovers were
  re-fitted so they are golden/cream (a* ≈ 0…+0.6, b* +2…+5 at +4…+6 stops). Portrait and
  Consumer shadows are less magenta (a* +1.0 → +0.6 at −4…−6 stops; Portrait b* +0.1 → +0.4).
* **Distinctness**: CN 500T got a teal/orange hue signature and a little more contrast
  (γ × 1.04), C41 Portrait 400 softer contrast (γ × 0.92 overall) with richer colour, C41
  Consumer 200 warmer, CN 250D a more pastel saturation (0.74), P3 Rich Contrast more contrast
  and a sharper toe (γ 3.75 → 4.15). Smallest mean ΔE2000 between negatives 2.0 → 2.2, between
  prints 2.1 → 2.3 (P1 ↔ P3 2.1 → 3.0).
* **Lab Scan**: scanner curve γ 1.4 → 1.45, shoulder p 2 → 4: blacks 0.132 → 0.089 at −6 stops
  and ≈ 0.08 at −10 (open, not milky); lab matrices re-fitted for every negative.
* **P4 Faded Archive**: magenta-dye fog 0.08 → 0.135, yellow-dye fog 0.08 → 0.09: highlights are
  cream (a* +0.7, b* +3.9 at +6 stops) instead of yellow-green (a* −2.6).
* **Halation scale** of the anti-halation-backed stocks ≈ −40 % (CN 200T 1.0 → 0.6),
  CN 800T No-Backing 3.0 → 2.0 (still 3.3× CN 200T). *(This alone did not stop a bright
  overcast sky from painting red fringes on dark edges — the critique of v1.0 measured
  d(R−G) 0.12 on a window frame; the actual fix is the halation energy model in section 5.1.)*
* **CN 800T grain** σ 0.020/0.024/0.032 → 0.017/0.020/0.026, size 1.40 → 1.30: still the
  coarsest colour stock, without the harsh blue-layer sparkle in skies.


### 5.1 Refinement round (v1.1)

Every point of the v1.0 critique was re-measured through the real plug-in (ofxhost, dev licence)
before it was changed; numbers are before → after.

* **Halation flooding (critical).** The energy that feeds the halation blur was the linear
  excess over the threshold, capped at 32× — a large bright overcast sky therefore fed as much
  light into the red layer as a lamp. It is now a soft knee in stops above the threshold,
  `e = (y − T)·s²/(s² + k²)`, `s = log2(y/T)`, k = 2 stops (a sky just above threshold feeds
  almost nothing, a +8-stop source still most of its excess), the per-amount gain is 0.60 → 0.24,
  the scale ladder concentrates energy near the source (relative radii 0.2…3.2, weights
  0.16/0.24/0.24/0.18/0.12/0.06) and the default threshold is 2.5 → 3.5 stops. On the overcast
  "person against a window" scene at default settings: window frame d(R−G) 0.120 → 0.005, face
  edge dR 0.334 → 0.024; a +8-stop point source keeps a visible ring (dR 0.28 at 4 px). All
  presets: window d(R−G) ≤ 0.021. Automated in `tools/render_tests` (`halation_daylight`).
  The per-stock halation scales were *not* lowered further (the energy model is the cause).
* **Print-path slope ripple.** Trilinear 48³ printer + projection LUTs made the slope of smooth
  gradients ripple periodically. The projection is now integrated per pixel (41 samples), the
  printer LUT is 64³, gamut compression uses a smooth channel minimum and starts at 0.80. Worst
  print pair, relative slope ripple (Savitzky–Golay fit, normalised by the fastest channel):
  neutral 0.084 → 0.032, sky 0.144 → 0.038, blue 0.132 → 0.034 (limit 0.05, tested for every
  print in `tests/core` `smoothness`). Cost: CPU colour kernel 1080p ≈ 187 → 400 ms (one
  thread per row, all cores), full bake 35 ms on one core.
* **Lab Scan distinctness.** Scans are fitted to each stock's own P1 rendition with per-stock
  saturation/hue bias (`briefs.json` `scan_sat`, `scan_hue_bias`), keep part of the negative's
  crossover (`scanCrossover`) and have a per-stock `scanContrast`: smallest ΔE2000 between two
  stocks under Lab Scan 0.37 → 2.17 (gate ≥ 2.0 in `reference/design.py gates`, ctest
  `look_gates_distinct`).
* **Lab Scan yellows.** Yellows are weighted 4× (below 100°) / 2× in the scan fit and the
  root-polynomial terms bend saturated hues without moving neutrals: golden-hour field under
  Lab Scan h 81–89° (input 82°, P1 75–93°), no longer spring-green.
* **Skies and cyan, reds, skin and orange in highlights.** Root cause: every log-domain per-layer
  tone scale bends hue toward the dye primaries/secondaries as exposure moves away from grey; no
  curve fit removes it for all hues. Fixed structurally by the hue hold (section 1.5) plus print
  sensitivities 30 % narrower and re-optimised negatives. CN 200T / P1: daylight sky
  L68 C16 h241 → L69 C32 h258 (input L85 C35 h259); chart red +2…+4 stops 31 → 21° became
  29 → 27°; F-Gamut red 21 → 0° became 14 → 9°; skin at +3 stops 68° → 58°, at +6 77° → 63°
  (input 57°); orange at +2 stops 78–82° → 61° (input 59°). Aims (sky ±6°, chroma ≥ 0.7;
  skin ±5° at +1…+3; orange ±10° at +2; red/rose/magenta ±10° at +2…+4; deep skin ±12° at −2,
  chroma ≥ 0.4) are gated for the core stocks (50D, 200T, 250D, 500T, Portrait 400 on P1 and
  200T Lab Scan): 0 misses (`reference/aims.py`, ctest `look_gates_aims`). Character stocks
  and special prints are reported, not gated. The chart cyan patch lies outside Rec.709, so its
  chroma is judged on the film's XYZ, not on clipped display RGB.
* **Grain colour.** Inter-layer grain correlation 0.40 → 0.90, shared clump scale 1.6 → 1.2,
  colour-stock granularity × 0.84 so luminance grain is unchanged: CN 200T chroma/L* noise
  1.68/0.94 → 0.83/0.98 (ratio 1.8 → 0.85); Tungsten Night Drama 0.73, Archive 1978 0.84.
  Render test checks the layer correlation (0.72–0.97).
* **RV 100 Slide.** D-max and shoulder sharpness up (R × 1.44, p 2.6 → 4.5), γ × 1.18, toe
  k × 1.4, granularity 0.006/0.007/0.010 → 0.0101/0.0118/0.0168 (the critic's 0.012/0.014/0.020
  × the 0.84 chroma-grain rescale): −6 stops 0.083 → 0.032 (aim ≤ 0.035), −10 stops 0.015, near clip by +4.5.
* **CN 800T No-Backing.** Shoulder lifted (R × 1.3, p 2 → 2.8), re-neutralised: P1 +6 stops
  0.857 → 0.956, +8 0.899 → 0.976 (the low "white ceiling" of the neon look is gone).
* **P2 Low Contrast.** Toe softer (k 4 → 6.5), γ 2.55 → 2.4, dye secondaries × 2.5 so the low
  contrast comes from the toe and mids: CN 200T / P2 +6 stops 0.902 → 0.971, Soft Daylight
  Pastel flat field +6 stops 0.934.
* **P4 Faded Archive B&W.** `monoTone` sepia absorption: B&W mid grey a*/b* −1.7/+3.6 (olive)
  → +1.6/+5.3 (warm).
* **CN Vintage 85.** Cyan-layer toe re-fitted: shadow a* −1.7 → −0.1 (−6…−2 stops), highlights
  keep b* +4…+5.
* **HDR headroom.** Unroll starts later and steeper and is calibrated at +10 stops: at a
  1000 cd/m² peak +10 stops 807 → 976 cd/m²; +6 stops 534 → 680 cd/m² (brighter than asked to
  keep — accepted, highlights match the SDR look better).
* **Rejected / partial.** Widening the red brightness dip of the upsampling (σ 8° → 14°) did not
  change the F-Gamut red/rose sweeps (the drift was log-domain desaturation, fixed by the hue
  hold) — reverted. Capping P3's low-chroma gain at 1.35 (measured 1.48 on CN 200T, 1.13–1.96
  across negatives) needs dye impurities that turn skies 10–20° cyan — rejected; the gain is
  reported by `reference/aims.py` but not gated. The suggested 0.15 logE yellow-layer shoulder
  delay was not applied: the hue hold fixes orange and skin in the highlights without changing
  the grey scale. Silver Retention Thriller −4 stops is 0.031 → 0.036 (aim 0.045, partial: P5's
  deep D-max is its character; the suggested printExposure 0.5 → 0 would darken the shadows
  further, so pushPull 1 → 0.5 and negBleach 0.25 → 0.15 were used instead).
'''


def fmt(v, n=3):
    return ('%.' + str(n) + 'f') % v


def main():
    lib = m.load()
    j = json.load(open(os.path.join(sp.HERE, 'stocks.json')))
    L = []
    L.append('# Celluloid Pro — film stocks and colour model')
    L.append('')
    L.append('Generated by `python3 reference/report.py` from `reference/stocks.json` (stock library hash '
             '`%s`). All stocks are our own parametric designs (sums of asymmetric Gaussian lobes for '
             'sensitivities and dyes, analytic characteristic curves) — no manufacturer data, names or '
             'presets. Design plots: `reference/plots/`.' % gen_tables.stock_hash(j))
    cv = j['scanner']['curve']
    L.append(MODEL.replace('@SG@', '%g' % cv['gamma']).replace('@SP@', '%g' % cv['p']))
    L.append('## 2. Negative and reversal stocks')
    L.append('')
    L.append('| # | Stock | Character | Balance | γ (G) | grain σ (R/G/B) | grain size | halation | DIR max |')
    L.append('|---|---|---|---|---|---|---|---|---|')
    for i, n in enumerate(lib.negs):
        bal = n['balance']
        bs = ('%dK tungsten' % bal['cct']) if bal['type'] == 'blackbody' else ('D%d daylight' % (bal['cct'] // 100))
        g = n['curves'][1 if len(n['curves']) > 1 else 0]['gamma']
        gr = '/'.join(fmt(v, 3) for v in (n['granularity'] if n['kind'] != 'bw' else n['granularity'][:1]))
        dm = max(max(r) for r in n.get('interlayer', [[0]])) if n['kind'] != 'bw' else 0
        L.append('| %d | **%s** | %s | %s | %s | %s | %s | %s | %s |' % (i, n['name'], n['description'], bs, fmt(g, 2),
                 gr, fmt(n['grainSizeScale'], 2), fmt(n['halationScale'], 1), fmt(dm, 2)))
    L.append('')
    L.append('## 3. Print stocks')
    L.append('')
    L.append('| # | Print | Character | γ | D-max (END) | grey Y | built-in silver | dye fade (C/M/Y) |')
    L.append('|---|---|---|---|---|---|---|---|')
    for i, p in enumerate(lib.prints):
        c = p['curves'][1]
        L.append('| %d | **%s** | %s | %s | %s | %s | %s | %s |' % (i, p['name'], p['description'], fmt(c['gamma'], 2),
                 fmt(c['fog'] + c['R'], 2), fmt(p['greyY'], 3), fmt(p.get('silver', 0), 2),
                 '/'.join(fmt(v, 2) for v in p.get('fade', [1, 1, 1]))))
    L.append('')
    # ---------------- measurements
    L.append('## 4. Measured look targets (SPEC §6)')
    L.append('')
    L.append('Rec.709 gamma 2.4 output, printer points 0, F-Log2 C input. Values are encoded code values.')
    L.append('')
    L.append('| Negative | Print / process | 18% grey | −6 stops | +6 stops | +8 stops | monotonic | skin hue shift (°, 6 tones) |')
    L.append('|---|---|---|---|---|---|---|---|')
    rows = [(i, 0, 'print') for i in range(len(lib.negs))] + [(1, j, 'print') for j in range(1, len(lib.prints))] + \
           [(1, 0, 'scan'), (6, 0, 'scan')]
    for (i, jj, proc) in rows:
        r = design.metrics(lib, i, jj, proc, verbose=False)
        pn = lib.prints[jj]['name'] if proc == 'print' and lib.negs[i]['kind'] != 'reversal' else ('Lab Scan' if proc == 'scan' else '—')
        L.append('| %s | %s | %s | %s | %s | %s | %s | %s |' % (
            lib.negs[i]['name'], pn, fmt(r['grey'][1], 4), fmt(np.mean(r['m6']), 3), fmt(min(r['p6']), 3),
            fmt(min(r['p8']), 3), 'yes' if r['monotonic'] else 'NO',
            'n/a (monochrome)' if lib.negs[i]['kind'] == 'bw' else ' '.join('%+.1f' % v for v in r['skin_dh'])))
    L.append('')
    names, T, T2 = design.deltae(lib, verbose=False)
    L.append('### 4.1 Distinctness: mean CIEDE2000 over the 24-patch reference chart')
    L.append('')
    short = [n.replace('Panchromatic', 'Pan') for n in names]
    L.append('| (on P1) | ' + ' | '.join(short) + ' |')
    L.append('|---|' + '---|' * len(names))
    for a in range(len(names)):
        L.append('| %s | ' % short[a] + ' | '.join(fmt(v, 1) for v in T[a]) + ' |')
    off = T[~np.eye(len(names), dtype=bool)]
    L.append('')
    L.append('Smallest difference between two different negatives: **%.1f**.' % off.min())
    L.append('')
    pn = [p['name'] for p in lib.prints]
    L.append('| (with CN 200T) | ' + ' | '.join(pn) + ' |')
    L.append('|---|' + '---|' * len(pn))
    for a in range(len(pn)):
        L.append('| %s | ' % pn[a] + ' | '.join(fmt(v, 1) for v in T2[a]) + ' |')
    off2 = T2[~np.eye(len(pn), dtype=bool)]
    L.append('')
    L.append('Smallest difference between two different prints: **%.1f**.' % off2.min())
    L.append('')
    TS = design.scan_deltae(lib)
    sn = sorted(set([k[0] for k in TS] + [k[1] for k in TS]))
    L.append('| (Lab Scan) | ' + ' | '.join(lib.negs[i]['name'] for i in sn) + ' |')
    L.append('|---|' + '---|' * len(sn))
    for a in sn:
        L.append('| %s | ' % lib.negs[a]['name'] + ' | '.join(
            fmt(0.0 if a == b else TS[(min(a, b), max(a, b))], 1) for b in sn) + ' |')
    L.append('')
    L.append('Smallest difference between two different negatives through the Lab Scan (chart + skin): **%.2f**.'
             % min(TS.values()))
    L.append('')
    L.append(REVISION)
    L.append('')
    L.append('## 6. Editing the stocks')
    L.append('')
    L.append('''The single source of truth is `reference/stocks.json`. After editing it:

```
python3 reference/gen_tables.py      # -> src/core/StockData.inc (C++ table)
python3 reference/golden.py          # -> reference/golden/core_golden.txt (C++ vs NumPy test data)
python3 reference/report.py          # -> this file (needs numpy)
python3 reference/plots.py           # -> reference/plots/*.png (needs matplotlib)
tests/core/build.sh                  # C++ unit tests incl. agreement with the NumPy model
```

Design tools: `reference/design.py` (metrics, renders, contact sheets, ΔE tables),
`reference/optimize.py` (fits sensitivities, interlayer and crossover curves of a negative to
its brief in `reference/briefs.json`, and the Lab Scan matrices; needs SciPy),
`reference/gen_upsampling.py` (the stock-independent spectral upsampling table).
Choice-parameter order is permanent (projects store indices): append new stocks at the end.''')
    with open(OUT, 'w') as f:
        f.write('\n'.join(L) + '\n')
    print('wrote', OUT)


if __name__ == '__main__':
    main()
