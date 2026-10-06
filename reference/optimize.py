"""Stock design optimiser (design-time tool; needs SciPy).

    python3 reference/optimize.py neg  <index> [--print P] [--iters N] [--write]
    python3 reference/optimize.py print <index> [--neg N] [--write]

Tunes a stock's physically meaningful parameters (sensitivity lobe positions/widths, interlayer
matrix, per-layer curve offsets) so that, through the default print, the rendering meets the
stock's design brief in reference/briefs.json: hue fidelity on natural colours (skin weighted),
target saturation, neutral tracking (crossover), with bounds that keep every curve plausible.
"""
import copy
import json
import math
import os
import sys

import numpy as np
from scipy.optimize import least_squares

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import direct  # noqa: E402
import model as m  # noqa: E402
import scenes  # noqa: E402
import spectral as sp  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))

CHART = scenes.chart_rgb()
SKIN = scenes.skin_rgb()
_P709 = np.array([[1, 0, 0], [0, 1, 0], [0, 0, 1], [1, 1, 0], [0, 1, 1], [1, 0, 1],
                  [1, 0.5, 0], [0.5, 1, 0], [0, 1, 0.5], [0, 0.5, 1], [0.5, 0, 1], [1, 0, 0.5]], float)
SAT = (_P709 * 0.85 + 0.03) @ sp.rgb_to_rgb('rec709', 'rec2020').T
SAT = SAT / (SAT @ sp.LUM2020)[:, None] * 0.12
TEST = np.concatenate([CHART[:18], SKIN, SKIN * 0.5, SKIN * 2.0, SAT], 0)
IS_SAT = np.zeros(len(TEST), bool)
IS_SAT[-len(SAT):] = True
LAB_IN = scenes.xyz_to_lab(TEST @ sp.M2020_TO_XYZ.T)
IS_SKIN = np.zeros(len(TEST), bool)
IS_SKIN[[0, 1]] = True
IS_SKIN[18:] = True
RAMP = np.repeat((0.18 * 2.0 ** np.arange(-6, 7, 1.0))[:, None], 3, 1)


def lab_of(lin709, ygrey=None):
    """CIELAB of display-linear Rec.709, exposure-normalised so that the rendered 18% grey has
    the scene's grey luminance (compares colour rendition, not the print's overall density)."""
    X = lin709 @ sp.npm(*sp.PRIMARIES['rec709']).T
    if ygrey is not None:
        X = X * (0.18 / ygrey)
    return scenes.xyz_to_lab(np.maximum(X, 1e-9))


def neg_vector_spec(neg):
    """(getter/setter list, bounds) for the free parameters of a colour negative."""
    spec = []
    bands = [(615, 660), (535, 560), (440, 465)]
    for li in range(3):
        lo, hi = bands[li]
        spec.append((('sensitivity', li, 0, 0), lo, hi))     # peak
        # widths bounded to photographically plausible layers (red layer cut off by ~700 nm)
        spec.append((('sensitivity', li, 0, 1), (20, 18, 16)[li], (42, 32, 30)[li]))     # sigma left
        spec.append((('sensitivity', li, 0, 2), (12, 16, 22)[li], (24, 32, 34)[li]))     # sigma right
    for i in range(3):
        for j in range(3):
            if i != j:
                spec.append((('interlayer', i, j), 0.0, 0.9))
    return spec


def neutral_vector_spec(neg):
    """R- and B-layer curve shape relative to the G layer (neutral tracking / designed crossover)."""
    c = neg['curves'][1]
    spec = []
    for li in (0, 2):
        spec.append((('curves', li, 'gamma'), c['gamma'] * 0.85, c['gamma'] * 1.15))
        spec.append((('curves', li, 'x0'), c['x0'] - 0.25, c['x0'] + 0.25))
        spec.append((('curves', li, 'k'), c['k'] * 0.6, c['k'] * 1.6))
        spec.append((('curves', li, 'R'), c['R'] * 0.85, c['R'] * 1.15))
    return spec


def get(d, path):
    for k in path:
        d = d[k]
    return d


def put(d, path, v):
    for k in path[:-1]:
        d = d[k]
    d[path[-1]] = float(v)


def residuals(x, lib, neg, prt, spec, brief, x0, mode='all'):
    for (path, lo, hi), v in zip(spec, x):
        put(neg, path, v)
    try:
        D = direct.Direct(lib, neg, prt)
        out = D.output709(TEST)
        ramp = D.output709(RAMP)
    except (np.linalg.LinAlgError, FloatingPointError, ValueError):
        return np.full(200, 1e3)
    yg = ramp[6] @ sp.npm(*sp.PRIMARIES['rec709'])[1]
    lab = lab_of(out, yg)
    Cin = np.hypot(LAB_IN[:, 1], LAB_IN[:, 2])
    Cout = np.hypot(lab[:, 1], lab[:, 2])
    hin = np.arctan2(LAB_IN[:, 2], LAB_IN[:, 1])
    hout = np.arctan2(lab[:, 2], lab[:, 1])
    dh = np.degrees((hout - hin + np.pi) % (2 * np.pi) - np.pi)
    hue_t = np.zeros(len(TEST))
    hdeg = np.degrees(hin)
    for (hc, wd, sh) in brief.get('hue_rules', []):      # [input hue centre deg, width deg, shift deg]
        dd = (hdeg - hc + 180.0) % 360.0 - 180.0
        hue_t = hue_t + sh * np.exp(-0.5 * (dd / wd) ** 2)
    skin_hue = brief.get('skin_hue', 2.0)
    hue_t = np.where(IS_SKIN, skin_hue, hue_t)
    wh = np.clip(Cin / 25.0, 0.2, 1.5) * np.where(IS_SKIN, 2.5, 1.0) * np.where(IS_SAT, 0.35, 1.0)
    r_h = (dh - hue_t) / 4.0 * wh
    sat = brief.get('saturation', 1.05)
    skin_sat = brief.get('skin_saturation', sat * 0.97)
    st = np.where(IS_SKIN, skin_sat, np.where(IS_SAT, brief.get('sat_saturated', 0.8), sat))
    r_c = np.log(np.maximum(Cout, 0.5) / (Cin * st)) / 0.05 * np.where(IS_SKIN, 1.5, np.where(IS_SAT, 0.15, 0.7))
    # neutral tracking: chroma of the ramp in Lab and per-channel log spread
    lr = lab_of(ramp, yg)
    if 'ramp_anchors' in brief:
        an = np.array(brief['ramp_anchors'], float)
        st = np.arange(-6, 7, 1.0)
        tint = np.stack([np.interp(st, an[:, 0], an[:, 1]), np.interp(st, an[:, 0], an[:, 2])], 1)
    else:
        tint = np.array(brief.get('ramp_tint_ab', [[0, 0]] * len(RAMP)))
    r_n = ((lr[:, 1:] - tint) / 0.6).ravel() * brief.get('neutral_weight', 1.0)
    # regularisation toward the starting design (keeps the stock character / plausibility)
    reg = (x - x0) / np.array([max(1e-3, (hi - lo) * 0.25) for (_, lo, hi) in spec]) * brief.get('reg', 0.3)
    if mode == 'colour':
        return np.concatenate([r_h, r_c, reg])
    if mode == 'neutral':
        return np.concatenate([r_n * 3.0, reg * 0.3])
    return np.concatenate([r_h, r_c, r_n, reg])


def report(lib, neg, prt):
    D = direct.Direct(lib, neg, prt)
    out = D.output709(TEST)
    yg = D.output709(RAMP)[6] @ sp.npm(*sp.PRIMARIES['rec709'])[1]
    lab = lab_of(out, yg)
    Cin = np.hypot(LAB_IN[:, 1], LAB_IN[:, 2])
    Cout = np.hypot(lab[:, 1], lab[:, 2])
    dh = np.degrees((np.arctan2(lab[:, 2], lab[:, 1]) - np.arctan2(LAB_IN[:, 2], LAB_IN[:, 1]) + np.pi) % (2 * np.pi) - np.pi)
    print('  dh  ', np.round(dh).astype(int).tolist())
    print('  (last 12 = saturated 709 primaries/secondaries)')
    print('  Crat', np.round(Cout / Cin, 2).tolist())
    lr = lab_of(D.output709(RAMP), yg)
    print('  ramp ab', np.round(lr[:, 1:], 1).tolist())


def optimise_neg(idx, prt_idx=0, iters=60, write=False):
    lib = m.load()
    briefs = json.load(open(os.path.join(HERE, 'briefs.json')))
    neg = copy.deepcopy(lib.negs[idx])
    prt = lib.prints[prt_idx]
    brief = briefs['negatives'].get(neg['name'], {})
    print('before:')
    report(lib, neg, prt)
    total0 = None
    for rnd in range(2):
        for mode, specf in (('colour', neg_vector_spec), ('neutral', neutral_vector_spec)):
            if mode == 'colour' and brief.get('no_colour_opt'):
                continue
            spec = specf(neg)
            x0 = np.array([get(neg, p) for (p, lo, hi) in spec])
            lo = np.array([s[1] for s in spec])
            hi = np.array([s[2] for s in spec])
            x0 = np.clip(x0, lo, hi)
            if total0 is None:
                total0 = 0.5 * np.sum(residuals(x0, lib, copy.deepcopy(neg), prt, spec, brief, x0) ** 2)
            res = least_squares(residuals, x0, bounds=(lo, hi), args=(lib, neg, prt, spec, brief, x0.copy(), mode),
                                max_nfev=iters * (len(x0) + 1), diff_step=1e-3, x_scale='jac')
            for (path, a, b), v in zip(spec, res.x):
                put(neg, path, round(float(v), 4))
            print('  round %d %-8s cost %.1f' % (rnd, mode, res.cost))
    allspec = neg_vector_spec(neg) + neutral_vector_spec(neg)
    xa = np.array([get(neg, p) for (p, a, b) in allspec])
    print('after (total cost %.1f -> %.1f):' % (total0, 0.5 * np.sum(residuals(xa, lib, copy.deepcopy(neg), prt, allspec, brief, xa) ** 2)))
    report(lib, neg, prt)
    for (path, a, b), v in zip(allspec, xa):
        flag = ' <bound' if abs(v - a) < 1e-3 * max(1, abs(a)) or abs(v - b) < 1e-3 * max(1, abs(b)) else ''
        print('   ', path, round(float(v), 4), flag)
    if write:
        j = json.load(open(os.path.join(HERE, 'stocks.json')))
        j['negatives'][idx] = neg
        with open(os.path.join(HERE, 'stocks.json'), 'w') as f:
            json.dump(j, f, indent=1)
    return neg


def main():
    args = sys.argv[1:]
    kind, idx = args[0], int(args[1])
    write = '--write' in args
    iters = int(args[args.index('--iters') + 1]) if '--iters' in args else 40
    if kind == 'neg':
        prt = int(args[args.index('--print') + 1]) if '--print' in args else 0
        optimise_neg(idx, prt, iters, write)
    elif kind == 'scan':
        optimise_scan(idx, write)



def optimise_scan(idx, write=False, sat=0.95):
    """Lab Scan colour matrix per negative (the lab's calibration): 3x3 with rows summing to 1
    (neutrals untouched), fitted for hue fidelity and a moderate, 'flat scan' saturation."""
    lib = m.load()
    neg = lib.negs[idx]
    if neg['kind'] != 'negative':
        return None
    P = m.Pipeline(lib).prepare(m.Params(neg=idx, process='scan'))
    Cin = np.hypot(LAB_IN[:, 1], LAB_IN[:, 2])
    hin = np.arctan2(LAB_IN[:, 2], LAB_IN[:, 1])
    def mat(x):
        M = np.zeros((3, 3))
        M[0, 1], M[0, 2], M[1, 0], M[1, 2], M[2, 0], M[2, 1] = x
        for i in range(3):
            M[i, i] = 1.0 - M[i].sum()
        return M
    M709 = np.linalg.inv(sp.npm(*sp.PRIMARIES['rec709']))
    def res(x):
        P.Mscan = mat(x)
        X, _ = P.film_xyz(TEST)
        Xg, _ = P.film_xyz(np.full((1, 3), 0.18))
        lab = lab_of(X @ M709.T, Xg[0, 1])
        Cout = np.hypot(lab[:, 1], lab[:, 2])
        dh = np.degrees((np.arctan2(lab[:, 2], lab[:, 1]) - hin + np.pi) % (2 * np.pi) - np.pi)
        wh = np.clip(Cin / 25.0, 0.2, 1.5) * np.where(IS_SKIN, 2.5, 1.0) * np.where(IS_SAT, 0.35, 1.0)
        st = np.where(IS_SKIN, sat, np.where(IS_SAT, sat * 0.8, sat))
        rc = np.log(np.maximum(Cout, 0.5) / (Cin * st)) / 0.05 * np.where(IS_SAT, 0.2, 1.0)
        return np.concatenate([dh / 4.0 * wh, rc, x * 2.0])
    r = least_squares(res, np.zeros(6), bounds=(-0.6, 0.6), diff_step=1e-3)
    M = mat(r.x)
    print(neg['name'], 'scan matrix', np.round(M, 4).tolist(), 'cost %.1f -> %.1f' % (0.5 * np.sum(res(np.zeros(6)) ** 2), r.cost))
    if write:
        j = json.load(open(os.path.join(HERE, 'stocks.json')))
        j['negatives'][idx]['scanMatrix'] = np.round(M, 5).tolist()
        with open(os.path.join(HERE, 'stocks.json'), 'w') as f:
            json.dump(j, f, indent=1)
    return M


if __name__ == '__main__':
    main()
