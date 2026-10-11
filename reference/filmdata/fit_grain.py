"""Fit Reafilm's dual-emulsion dye-cloud grain model to the published KODAK VISION3 diffuse RMS
granularity curves (vision3_texture.json, see digitise_kodak.py).

Model (== src/kernels/GrainKernels.h, celg_*):
  every colour layer holds a FAST (coarse-grained) and a SLOW (fine-grained) emulsion. The developed
  fraction of emulsion k at layer exposure s (stops relative to 18 % grey) is a probit curve (log-normal
  grain sensitivity), the fast one with a fog floor:
      p_fast(s) = Phi(smax((s - s_fast) / b_fast, z_fog)),     p_slow(s) = Phi((s - s_slow) / b_slow)
  Its developed dye clouds are an excursion set of a band-limited Gaussian "sensitivity" field G
  (unit variance, the emulsion's cloud size): soft indicator I = Phi((G - t) / w) with the level t set so
  that E[I] = p exactly (t = -sqrt(1 + w^2) Phi^-1(p)). The density fluctuation is
      dD = A_fast (I_fast - p_fast) + A_slow (I_slow - p_slow)
  with Var(I) = Phi2(a, a; 1 / (1 + w^2)) - p^2, a = Phi^-1(p). Sparse isolated clouds in the toe
  (positive skew), merging clouds in the mid tones, sparse holes near saturation; the coarse fast
  emulsion dominates the shadows and lower mids, the fine slow emulsion the highlights.

The Kodak curves show exactly this two-hump shape (a granularity peak in the toe where the fast
emulsion starts to develop, a second broad hump 2..4 stops over grey, falling toward the shoulder).
Grey (0 stops) is located on the granularity chart's log-exposure axis through the density of the
green layer at 0 camera stops on the same sheet's sensitometric chart (logE ~ 2.50 on all sheets).

Usage: python3 fit_grain.py [w]      prints the fitted parameters (copied into GrainKernels.h /
                                     TextureDesign.cpp, see kGrain* constants) and writes grain_fit.json
"""
import json
import os
import sys

import numpy as np
from scipy.optimize import least_squares
from scipy.stats import multivariate_normal, norm

HERE = os.path.dirname(os.path.abspath(__file__))
GREY_LOGE = 2.50
KAPPA = 0.6          # softness of the fog floor (smooth maximum, in probit units)
SETS = [('VISION3 250D 5207', 'g'), ('VISION3 250D 5207', 'r'), ('VISION3 250D 5207', 'b'),
        ('VISION3 500T 5219', 'g'), ('VISION3 500T 5219', 'b'),
        ('VISION3 50D 5203', 'r'), ('VISION3 50D 5203', 'g'), ('VISION3 50D 5203', 'b')]


def var_table(w, n=801):
    r = 1.0 / (1.0 + w * w)
    p = np.linspace(1e-4, 1 - 1e-4, n)
    mvn = multivariate_normal(mean=[0, 0], cov=[[1, r], [r, 1]])
    v = np.array([mvn.cdf([a, a]) for a in norm.ppf(p)]) - p * p
    return p, np.maximum(v, 0.0)


def main():
    w = float(sys.argv[1]) if len(sys.argv) > 1 else 0.6
    d = json.load(open(os.path.join(HERE, 'vision3_texture.json')))
    pt, vt = var_table(w)
    V = lambda p: np.interp(p, pt, vt)
    data = []
    for st, k in SETS:
        g = d[st]['granularity'][k]
        sx = (np.array(g['logE']) - GREY_LOGE) / np.log10(2.0)
        s = np.array(g['sigmaD'])
        m = (sx > -7.5) & (sx < 8.0)
        data.append((st, k, sx[m], s[m]))

    def model(par, sx, amp):
        sf, bf, ss, bs, zfog = par[:5]
        z = (sx - sf) / bf
        z = 0.5 * (z + zfog + np.sqrt((z - zfog) ** 2 + KAPPA ** 2))     # smooth max: fog floor
        p1 = norm.cdf(z)
        p2 = norm.cdf((sx - ss) / bs)
        return np.sqrt(amp[0] ** 2 * V(p1) + amp[1] ** 2 * V(p2))

    def res(par):
        return np.concatenate([np.log(model(par, sx, par[5 + 2 * i:7 + 2 * i]) + 1e-7) - np.log(s)
                               for i, (_, _, sx, s) in enumerate(data)])

    p0 = [-3.6, 0.8, 2.3, 2.5, -1.4] + [0.03, 0.03] * len(data)
    lo = [-6, 0.2, -1, 0.5, -4.0] + [0, 0] * len(data)
    hi = [0, 3, 6, 6, 0.0] + [1, 1] * len(data)
    f = least_squares(res, p0, bounds=(lo, hi))
    par = f.x
    out = {'w': w, 'rms_log_error': float(np.sqrt(np.mean(f.fun ** 2))),
           'fast_centre_stops': par[0], 'fast_width_stops': par[1], 'z_fog': par[4], 'kappa': KAPPA,
           'fog_fraction': float(norm.cdf(par[4])),
           'slow_centre_stops': par[2], 'slow_width_stops': par[3], 'layers': {}}
    print('w %.2f  rms log error %.3f' % (w, out['rms_log_error']))
    print('fast: centre %.2f stops, width %.2f, z_fog %.3f (fog %.3f) ; slow: centre %.2f, width %.2f'
          % (par[0], par[1], par[4], norm.cdf(par[4]), par[2], par[3]))
    for i, (st, k, sx, s) in enumerate(data):
        amp = par[5 + 2 * i:7 + 2 * i]
        g0 = model(par, np.array([0.0]), amp)[0]
        out['layers'][f'{st} {k}'] = {'A_fast': amp[0], 'A_slow': amp[1], 'sigmaD_grey': g0}
        print('%s %s  A_fast %.4f  A_slow %.4f  sigmaD(grey) %.4f  fast/slow %.2f' % (st, k, amp[0], amp[1], g0, amp[0] / amp[1]))
        print('      ' + '  '.join('%+.1f:%.4f/%.4f' % (q, np.interp(q, sx, s), model(par, np.array([q]), amp)[0])
                                   for q in (-7, -5, -3.6, -2, 0, 2, 4, 6, 7.5)))
    json.dump(out, open(os.path.join(HERE, 'grain_fit.json'), 'w'), indent=1)


if __name__ == '__main__':
    main()
