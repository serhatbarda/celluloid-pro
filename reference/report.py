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
PRINT:    printer lamp × T_neg(λ) × print sensitivities  (3D LUT)  ──► print curves ──► print dyes [+ silver]
          ──► projector lamp × T_print(λ) × CIE 1931 CMFs (3D LUT, log XYZ) ──► CAT16 → D65
LAB SCAN: Status-M-like scanner (3D LUT) ──► neutral-referenced balanced inversion ──► lab matrix ──► scanner curve
REVERSAL: positive curves ──► reversal dyes ──► projection (3D LUT)
   ▼
display XYZ (white 1.0) ──► output primaries ──► smooth gamut compression ──► SDR limiter | HDR unroll ──► EOTF⁻¹
```

All spectral work happens on the host in double precision at 380–780 nm / 5 nm (81 samples)
and is baked into LUTs; the per-pixel kernel (`src/kernels/ColorKernels.h`, identical on CPU,
OpenCL, CUDA and Metal) costs a 2D lookup, nine 1D lookups, two trilinear 3D lookups and a few
matrices. Reference implementation: `reference/model.py` (NumPy, float64); the C++ kernels agree
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
* **Printing**: `log H_j = log10 Σ lamp·T_neg(λ)·S_print,j(λ)` baked as a 48³ LUT over the
  negative dye amounts; **printer points**: +1 point = +0.025 log H on that layer (more red
  light → more cyan dye → *less* red in the image); `printExposure` +1 = 1 point less light on
  all layers (= brighter print).
* **Grey balance** (every prepare, points 0, exposure 0): per-layer printer offsets solved by
  Newton on the actual kernel so that scene 18 % grey is D65-neutral at the print's target Y.
  The lab re-times the print for push/pull/bleach, so those change contrast and colour, not
  the grey level.
* **Print curves** γ ≈ 2.55–3.75; print bleach = retained silver `≈0.6·Σw(P − Pmin)` (P5 has
  0.6 of it built in).
* **Projection**: `log XYZ = log10 Σ projector·T_print(λ)·CMF(λ)` (48³ LUT); Y normalised so
  the clear (unexposed, processed) print — base + dye fog — is display white 1.0. Chromatic
  adaptation from the projector white (lamp through a perfectly clear base) to D65 with
  **CAT16** (von Kries, D = 1): the current CIE recommendation, linear and well behaved for the
  large warm→D65 shift; print base tints (e.g. P4's yellowed base) stay visible.
* **Lab Scan**: Status-M-like narrow-band scanner (448/545/652 nm) reads the negative (48³
  LUT); *neutral-referenced balanced inversion*: each channel is mapped through the neutral
  ramp onto the green channel's density scale (per-density grey balance, like a lab scanner),
  so neutrals stay neutral at every exposure while the negative's latitude is kept; a per-stock
  3×3 lab matrix (rows sum to 1) and a gentle scanner curve (γ 1.4) give the flatter scan look.
  Printer points act as scanner balance (1 point ≈ the same display change as in print).
* **Reversal**: positive curves (`D = Dmin + dev(x0 − x)`), reversal dyes → projection LUT;
  grey balance via per-layer exposure offsets (CC filtration). Print stock and Process are
  ignored.
* **B&W**: one panchromatic layer (three identical layers internally), silver image; printed
  as a neutral silver image on the chosen print stock (its green-layer curve and its base tint),
  so B&W on P4 is warm-toned. Grain is monochrome (the colour kernel uses the G noise channel).

### 1.5 Output
Display XYZ → output RGB (P3-DCI: DCI white encoding with D65 white simulated, scaled so D65
white's largest channel is 1). **Gamut compression**: hue-preserving desaturation toward the
luminance axis — saturation ratio `s = (Y − min)/Y` mapped by
`s' = 0.85 + 0.15·x/√(1+x²)`, `x = (s − 0.85)/0.15` above 0.85 (C1, asymptote at the gamut
boundary, no clipping steps). **SDR**: per-channel C1 tanh limiter from 0.95 to 1.0 (output never
exceeds 1.0, monotonic per channel). **HDR**: mid grey → 15 cd/m² (scaled from the SDR grey of
the same look); highlight **unroll** multiplies luminance by `10^(u·G)`, `G = Gmax·t²/(1+t²)`,
`t = max(e − 0.30, 0)/0.90`, e = log10 scene exposure over grey, `Gmax = log10(peak/(Lref·Ywhite))`
— monotonic, C1, approaches the peak (per-channel limiter from 0.8·peak). PQ per SMPTE ST 2084
(`hdrPeakNits`); HLG per ITU-R BT.2100 with the 1000 cd/m² reference display (system γ 1.2,
inverse OOTF on BT.2020 luminance), peak fixed at 1000.

### 1.6 Kernel / state contract (for the engine, GPU and texture code)
* State layout: `src/kernels/ColorState.h` (128-float header + OCT, NC, PL, PC, PJ/SC blocks).
  `Pipeline::stateVersion()` comes from one process-wide counter (unique across instances).
* `color` / `display_finish`: in1 = grain field, xyz = R-, G-, B-sensitive layer, zero mean,
  unit variance; used when `inStride[1] != 0`. in2 = alpha source when `inStride[2] != 0`.
  The kernels do **not** apply the texture engine's per-format amplitude
  (`tex::grainFormatAmplitude`); fold it into the field or `LookParams.grainAmount` once.
* Texture-Only: `display_decode` gives display-linear light in the output primaries
  (SDR: 1.0 = display white; PQ/HLG: cd/m² / 100); `display_finish` adds grain in density
  (fading out toward SDR white, never pushing a legal value above 1.0) and encodes; with grain
  off the pair is an identity (≤ 1e-5).
* Cost of `prepare()`: cache hit ≈ 0.3 ms; full bake of a new stock pair ≈ 20 ms on 4 threads,
  ≈ 50 ms on one core (`CEL_BAKE_THREADS=1`).
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
    L.append(MODEL)
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
    L.append('## 5. Editing the stocks')
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
