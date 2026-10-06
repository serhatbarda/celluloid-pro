"""Generate the stock-independent spectral upsampling table (octahedral direction grid).

    python3 reference/gen_upsampling.py            # writes reference/data/upsample_oct.npz
                                                   #    and src/core/UpsampleTable.inc

Method (justification in docs/STOCKS.md, "Spectral upsampling"):
  For every direction u of scene-linear Rec.2020 RGB (unit L1 norm in an opponent basis,
  octahedral parameterisation -> covers ALL of RGB space incl. negative / out-of-gamut values):
    1. XYZ_u = M_2020 * rgb_u.  S = X+Y+Z, q = S / (|X|+|Y|+|Z|) (q = 1 for every real colour).
       Fade phi(q) = smoothstep(0, 0.5, q); S <= 0 -> black (non-physical directions).
    2. Chromaticity soft-clip toward the D65 white along the dominant-wavelength line:
       relative purity r = |xy - w| / |locus(h) - w| -> r' = r (r <= 0.70), C1 tanh roll-off to
       an asymptote of 0.95 (never on/over the spectral locus).
    3. Spectral shape: Jakob-Hanika style sigmoid-polynomial reflectance
           R(lambda) = sigmoid(c0 + c1 t + c2 t^2),  t = (lambda - 580) / 200
       fitted EXACTLY to the clipped chromaticity at half the optimal-colour (MacAdam) luminance
       Y = 0.5 * Ymax(h, r') under D65: a bounded, smooth, "typical surface" spectrum (reds become
       smooth long-pass plateaus, greens/blues band-pass bumps, purples band-stops).
    4. Scale a_u = Yk * phi / Y(R): the clipped colour keeps the input's luminance Y (smooth
       floor 0.03 * (X+ + Y+ + Z+) for imaginary directions with tiny/negative Y); exposure is
       exactly homogeneous: E(k rgb) = k E(rgb).
  The table stores (c0, c1, c2, a_u) per texel. The per-stock bake (C++ and model.py) integrates
  a_u * R_u(lambda) against each layer's balanced sensitivity.
"""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import spectral as sp  # noqa: E402

N_OCT = int(os.environ.get('CEL_N_OCT', '97'))   # odd: texel lines on the u=0 / v=0 kinks of the octahedral map
R0, RMAX = 0.65, 0.92
FBRIGHT = 0.5
KAPPA = 0.03

W = sp.D65[:, None] * sp.CMF
W = W / W[:, 1].sum()                 # XYZ of reflectance R under D65: R @ W  (white -> Y=1)
WHITE = W.sum(0)
WXY = WHITE[:2] / WHITE.sum()
T = sp.TNORM


def locus_xy():
    loc = W / W.sum(1, keepdims=True)  # monochromatic stimuli (D65 weighting does not change chroma)
    return sp.CMF[:, :2] / sp.CMF.sum(1, keepdims=True)


LOCUS = locus_xy()


def locus_distance(h):
    """Distance from D65 white to the spectral locus polygon (incl. purple line) along angle h."""
    h = np.atleast_1d(h)
    d = np.stack([np.cos(h), np.sin(h)], -1)
    P = np.vstack([LOCUS, LOCUS[:1]]) - WXY
    best = np.full(h.shape, np.inf)
    for i in range(len(P) - 1):
        p, q = P[i], P[i + 1]
        e = q - p
        det = d[:, 0] * (-e[1]) - d[:, 1] * (-e[0])
        ok = np.abs(det) > 1e-15
        s = (p[0] * (-e[1]) - p[1] * (-e[0])) / np.where(ok, det, 1)
        u = (d[:, 0] * p[1] - d[:, 1] * p[0]) / np.where(ok, det, 1)
        hit = ok & (s > 0) & (u >= -1e-12) & (u <= 1 + 1e-12)
        best = np.where(hit, np.minimum(best, s), best)
    return best


def optimal_colour_ymax(nh=720, nr=200):
    """Ymax(h, r): max luminance of any optimal (0/1 block) reflectance with relative purity >= r."""
    wl1 = np.arange(380.0, 780.0 + 1e-9, 1.0)
    W1 = np.stack([np.interp(wl1, sp.WL, W[:, k]) for k in range(3)], 1) / 5.0
    C = np.vstack([np.zeros(3), np.cumsum(W1, 0)])
    n = len(wl1)
    a, b = np.meshgrid(np.arange(n + 1), np.arange(n + 1), indexing='ij')
    m = a < b
    band = C[b[m]] - C[a[m]]
    stop = C[-1] - band
    opt = np.vstack([band, stop])
    S = opt.sum(1)
    good = S > 1e-9
    opt = opt[good]
    S = S[good]
    xy = opt[:, :2] / S[:, None]
    Y = opt[:, 1]
    hue = np.arctan2(xy[:, 1] - WXY[1], xy[:, 0] - WXY[0])
    hb = (np.arange(nh) + 0.5) / nh * 2 * np.pi - np.pi
    Ld = locus_distance(hb)
    hi = np.clip(((hue + np.pi) / (2 * np.pi) * nh).astype(int), 0, nh - 1)
    r = np.hypot(xy[:, 0] - WXY[0], xy[:, 1] - WXY[1]) / Ld[hi]
    ri = np.clip(np.round(r * nr).astype(int), 0, nr)
    tab = np.zeros((nh, nr + 1))
    np.maximum.at(tab, (hi, ri), Y)
    # max over purity >= r (monotone non-increasing in r)
    tab = np.maximum.accumulate(tab[:, ::-1], axis=1)[:, ::-1]
    tab[:, 0] = 1.0
    # smooth (circular in hue, then along r) in log domain, keep monotone
    lt = np.log(np.maximum(tab, 1e-6))
    k = np.exp(-0.5 * (np.arange(-12, 13) / 4.0) ** 2)
    k /= k.sum()
    pad = np.concatenate([lt[-12:], lt, lt[:12]], 0)
    lt = np.stack([np.convolve(pad[:, j], k, mode='valid') for j in range(nr + 1)], 1)
    padr = np.concatenate([np.repeat(lt[:, :1], 6, 1), lt, np.repeat(lt[:, -1:], 6, 1)], 1)
    kr = np.exp(-0.5 * (np.arange(-6, 7) / 2.0) ** 2)
    kr /= kr.sum()
    lt = np.stack([np.convolve(padr[i], kr, mode='valid') for i in range(nh)], 0)
    lt = np.minimum.accumulate(lt, axis=1)
    tab = np.exp(lt)
    tab[:, 0] = 1.0
    return hb, tab, Ld


HB, YMAX, LD = optimal_colour_ymax()


def ymax_lookup(h, r):
    nh, nr1 = YMAX.shape
    fh = (h + np.pi) / (2 * np.pi) * nh - 0.5
    i0 = np.floor(fh).astype(int)
    th = fh - i0
    i0 %= nh
    i1 = (i0 + 1) % nh
    fr = np.clip(r, 0, 1) * (nr1 - 1)
    j0 = np.clip(np.floor(fr).astype(int), 0, nr1 - 2)
    tr = fr - j0
    def g(i, j):
        return YMAX[i, j]
    a = g(i0, j0) * (1 - tr) + g(i0, j0 + 1) * tr
    b = g(i1, j0) * (1 - tr) + g(i1, j0 + 1) * tr
    return a * (1 - th) + b * th


def softclip_purity(r):
    x = np.maximum(r - R0, 0.0)
    return np.where(r <= R0, r, R0 + (RMAX - R0) * np.tanh(x / (RMAX - R0)))


def sigmoid(z):
    return 0.5 + 0.5 * np.tanh(0.5 * z)


def fit_batch(target_dir, rp, Ld, hue, nsteps=24, iters=40):
    """Continuation from flat 0.5 reflectance along purity for a batch of targets.
    target_dir: unit chroma direction (n,2); rp: clipped relative purity (n,)."""
    n = len(rp)
    c = np.zeros((n, 3))
    ok = np.ones(n, bool)
    for s in range(1, nsteps + 1):
        rs = rp * (s / nsteps)
        xy = WXY + (rs * Ld)[:, None] * target_dir
        Yt = FBRIGHT * ymax_lookup(hue, rs)
        tgt = np.stack([xy[:, 0] / xy[:, 1] * Yt, Yt, (1 - xy[:, 0] - xy[:, 1]) / xy[:, 1] * Yt], 1)
        for it in range(iters):
            z = c[:, 0:1] + c[:, 1:2] * T + c[:, 2:3] * T * T
            R = sigmoid(z)
            f = R @ W - tgt
            err = np.abs(f).max(1) / np.maximum(Yt, 1e-6)
            if s < nsteps and err.max() < 1e-7:
                break
            if s == nsteps and err.max() < 1e-12:
                break
            dR = R * (1 - R)
            J = np.stack([dR @ W, (dR * T) @ W, (dR * T * T) @ W], 2)   # (n,3,3)  d f_i / d c_k
            try:
                dc = np.linalg.solve(J, -f[..., None])[..., 0]
            except np.linalg.LinAlgError:
                dc = np.zeros_like(c)
            mx = np.abs(dc).max(1, keepdims=True)
            dc = dc * np.minimum(1.0, 8.0 / np.maximum(mx, 1e-30))
            c = c + dc
        ok &= np.isfinite(c).all(1)
    z = c[:, 0:1] + c[:, 1:2] * T + c[:, 2:3] * T * T
    R = sigmoid(z)
    xyz = R @ W
    xy = xyz[:, :2] / xyz.sum(1, keepdims=True)
    want = WXY + (rp * Ld)[:, None] * target_dir
    resid = np.abs(xy - want).max(1)
    return c, resid


def direction_targets(rgb):
    """For unit-L1 directions rgb (n,3): returns (valid, dir2, rp, Ld, hue, Spos*phi).
    Magnitude kept: Spos = X+ + Y+ + Z+ (sum of the positive tristimulus parts; = X+Y+Z for every
    real colour).  Fade to black only for directions dominated by negative tristimulus values:
    phi = smoothstep(0, 0.5, Spos / (|X|+|Y|+|Z|)).  Hue direction from (X - xw S, Y - yw S),
    continuous through S = 0; S <= 0 (non-physical) maps to the maximum clipped purity."""
    XYZ = rgb @ sp.M2020_TO_XYZ.T
    S = XYZ.sum(1)
    Spos = np.maximum(XYZ, 0.0).sum(1)
    A = np.abs(XYZ).sum(1)
    q = Spos / np.maximum(A, 1e-30)
    t = np.clip(q / 0.5, 0, 1)
    phi = t * t * (3 - 2 * t)
    # luminance kept: Y for every colour with a healthy luminance, a floor of KAPPA * Spos for
    # (imaginary) directions whose Y is tiny or negative; homogeneous smooth max
    k = KAPPA * Spos
    Yk = 0.5 * (XYZ[:, 1] + k + np.sqrt((XYZ[:, 1] - k) ** 2 + (0.1 * k) ** 2))
    valid = (Spos > 1e-12) & (phi > 0)
    d = XYZ[:, :2] - WXY[None, :] * S[:, None]
    dist = np.hypot(d[:, 0], d[:, 1])
    small = dist < 1e-14
    dir2 = np.where(small[:, None], np.array([1.0, 0.0]), d / np.maximum(dist, 1e-30)[:, None])
    hue = np.where(small, 0.0, np.arctan2(dir2[:, 1], dir2[:, 0]))
    Ld = locus_distance(hue)
    r = np.where(S > 1e-12, dist / np.maximum(S, 1e-30) / Ld, 1e9)
    rp = softclip_purity(r)
    return valid, dir2, rp, Ld, hue, np.where(valid, Yk * phi, 0.0)


def build_table(N):
    g = np.linspace(-1.0, 1.0, N)
    u, v = np.meshgrid(g, g, indexing='xy')      # row = v index, col = u index
    rgb = sp.oct_decode(u.ravel(), v.ravel())
    valid, dir2, rp, Ld, hue, Sphi = direction_targets(rgb)
    coef = np.zeros((N * N, 3))
    resid = np.zeros(N * N)
    idx = np.nonzero(valid)[0]
    c, rs = fit_batch(dir2[idx], rp[idx], Ld[idx], hue[idx])
    coef[idx] = c
    resid[idx] = rs
    R = sigmoid(coef[:, 0:1] + coef[:, 1:2] * T + coef[:, 2:3] * T * T)
    YR = (R @ W)[:, 1]
    a = np.where(valid, Sphi / np.maximum(YR, 1e-30), 0.0)
    coef[~valid] = 0.0
    return coef.reshape(N, N, 3), a.reshape(N, N), resid.reshape(N, N), valid.reshape(N, N)


def write_inc(path, coef, a):
    N = coef.shape[0]
    with open(path, 'w') as f:
        f.write('// GENERATED by reference/gen_upsampling.py -- do not edit.\n')
        f.write('// Spectral upsampling table on the octahedral direction grid (see that script for the method).\n')
        f.write('// Per texel (row v, column u): sigmoid-polynomial coefficients c0,c1,c2 and scale a.\n')
        f.write(f'#define CEL_UPSAMPLE_N {N}\n')
        f.write('static const float kUpsampleTable[CEL_UPSAMPLE_N * CEL_UPSAMPLE_N * 4] = {\n')
        flat = np.concatenate([coef.reshape(-1, 3), a.reshape(-1, 1)], 1).astype(np.float32)
        for row in flat:
            f.write(','.join(f'{x:.9g}f' if x != 0 else '0' for x in row) + ',\n')
        f.write('};\n')


def main():
    coef, a, resid, valid = build_table(N_OCT)
    print(f'N={N_OCT} valid texels {valid.sum()} / {valid.size}; max chroma residual {resid[valid].max():.3e}')
    # store the float32-rounded values so that model.py and C++ use bit-identical inputs
    c32 = coef.astype(np.float32).astype(np.float64)
    a32 = a.astype(np.float32).astype(np.float64)
    np.savez(os.path.join(sp.DATA, 'upsample_oct.npz'), coef=c32, a=a32, N=N_OCT, r0=R0, rmax=RMAX, fbright=FBRIGHT)
    root = os.path.dirname(sp.HERE)
    write_inc(os.path.join(root, 'src', 'core', 'UpsampleTable.inc'), coef, a)


if __name__ == '__main__':
    main()
