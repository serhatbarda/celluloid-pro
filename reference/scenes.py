"""Synthetic test scenes for designing / validating the stocks (scene-linear Rec.2020, D65).

All colours are our own definitions (no third-party chart data). The 24-patch "Reafilm
reference chart" uses CIELAB (D65) aims chosen to represent natural object colours: skin,
sky, foliage, flowers, saturated primaries/secondaries and a neutral row.
"""
import math

import numpy as np

import spectral as sp

# CIELAB (D65) aims of the Reafilm reference chart, row-major 6 x 4
CHART_LAB = [
    (38, 13, 14), (66, 17, 17), (50, -4, -22), (43, -14, 22), (55, 9, -25), (71, -33, 0),
    (62, 34, 57), (40, 10, -45), (51, 47, 16), (30, 22, -21), (72, -24, 57), (71, 19, 67),
    (29, 14, -50), (55, -38, 31), (42, 53, 28), (81, 4, 79), (51, 49, -15), (51, -28, -28),
    (95, 0, 0), (81, 0, 0), (66, 0, 0), (51, 0, 0), (36, 0, 0), (20, 0, 0),
]
CHART_NAMES = ['dark skin', 'light skin', 'sky', 'foliage', 'blue flower', 'bluish green',
               'orange', 'purplish blue', 'moderate red', 'purple', 'yellow green', 'orange yellow',
               'blue', 'green', 'red', 'yellow', 'magenta', 'cyan',
               'white', 'neutral 8', 'neutral 6.5', 'neutral 5', 'neutral 3.5', 'black']
SKIN_LAB = [(72, 13, 16), (64, 16, 20), (56, 18, 24), (47, 17, 24), (37, 14, 19), (68, 21, 11)]
SKIN_NAMES = ['fair', 'light', 'medium', 'tan', 'deep', 'rosy']

WD65 = sp.xy_to_XYZ(sp.WHITE_D65)


def lab_to_xyz(lab):
    lab = np.asarray(lab, dtype=float)
    L, a, b = lab[..., 0], lab[..., 1], lab[..., 2]
    fy = (L + 16) / 116
    fx = fy + a / 500
    fz = fy - b / 200
    d = 6 / 29

    def finv(f):
        return np.where(f > d, f ** 3, 3 * d * d * (f - 4 / 29))
    return np.stack([finv(fx) * WD65[0], finv(fy) * WD65[1], finv(fz) * WD65[2]], -1)


def xyz_to_lab(X, white=WD65):
    X = np.asarray(X, dtype=float) / white
    d = 6 / 29
    f = np.where(X > d ** 3, np.cbrt(np.maximum(X, 0)), X / (3 * d * d) + 4 / 29)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def lab_to_rgb2020(lab):
    return lab_to_xyz(lab) @ sp.XYZ_TO_2020.T


def chart_rgb():
    return lab_to_rgb2020(CHART_LAB)


def skin_rgb():
    return lab_to_rgb2020(SKIN_LAB)


def hsv_sweep(n, sat, val):
    """Hue sweep of Rec.2020 colours with given 'saturation' (can exceed 1 = out of gamut)."""
    h = np.linspace(0, 1, n, endpoint=False)
    k = np.stack([(5 + h * 6) % 6, (3 + h * 6) % 6, (1 + h * 6) % 6], -1)
    c = val - val * sat * np.clip(np.minimum(k, 4 - k), 0, 1)
    return c


def test_image(W=960):
    """Composite scene-linear test image (H x W x 3) and a dict of named regions."""
    rows = []
    regions = {}
    y = 0
    # chart 6x4
    pw = W // 6
    ph = pw * 2 // 3
    chart = chart_rgb()
    blk = np.zeros((ph * 4, W, 3))
    for i, c in enumerate(chart):
        r, q = divmod(i, 6)
        blk[r * ph:(r + 1) * ph, q * pw:(q + 1) * pw] = c
    rows.append(blk)
    regions['chart'] = (y, ph, pw)
    y += blk.shape[0]
    # skin at -2,-1,0,+1,+2 stops
    sk = skin_rgb()
    sh = ph // 2
    blk = np.zeros((sh * 5, W, 3))
    sw = W // len(sk)
    for e in range(5):
        for i, c in enumerate(sk):
            blk[e * sh:(e + 1) * sh, i * sw:(i + 1) * sw] = c * 2.0 ** (e - 2)
    rows.append(blk)
    regions['skin'] = (y, sh, sw)
    y += blk.shape[0]
    # grey ramp -10..+10 stops
    gh = 48
    st = np.linspace(-10, 10, W)
    blk = np.repeat((0.18 * 2 ** st)[None, :, None], gh, 0).repeat(3, 2)
    rows.append(blk)
    y += gh
    # stepped grey ramp in 1-stop steps (for reading values)
    blk = np.zeros((gh, W, 3))
    for i in range(21):
        x0, x1 = i * W // 21, (i + 1) * W // 21
        blk[:, x0:x1] = 0.18 * 2.0 ** (i - 10)
    rows.append(blk)
    y += gh
    # saturated sweeps: in gamut (sat 1), and beyond (sat 1.3, mixes in negative values),
    # with brightness ramp -4..+4 stops vertically
    for sat in (0.6, 1.0, 1.35):
        hh = 72
        hue = hsv_sweep(W, sat, 1.0)
        mag = np.abs(hue).sum(1) / 3.0
        hue = hue / mag[:, None] * 0.18
        ev = np.linspace(3, -4, hh)
        blk = hue[None, :, :] * (2.0 ** ev)[:, None, None]
        rows.append(blk)
        y += hh
    # sunset gradient: deep blue zenith -> orange horizon, sun disc at +5 stops, dark foreground
    hh = 200
    yy = np.linspace(0, 1, hh)[:, None]
    xx = np.linspace(-1, 1, W)[None, :]
    top = np.array([0.02, 0.05, 0.16])
    mid = np.array([0.55, 0.22, 0.10])
    hor = np.array([1.6, 0.75, 0.18])
    t = yy[..., None]
    sky = np.where(t < 0.6, top + (mid - top) * (t / 0.6), mid + (hor - mid) * ((t - 0.6) / 0.4))
    sky = sky * np.ones((1, W, 1))
    r = np.sqrt((xx * 3.0) ** 2 + ((yy - 0.82) * 6.0) ** 2)
    sun = np.exp(-(r / 0.25) ** 4)[..., None] * np.array([16.0, 11.0, 6.0])
    glow = np.exp(-r / 1.2)[..., None] * np.array([1.2, 0.6, 0.15])
    img = sky + sun + glow
    img[(yy > 0.9).repeat(W, 1)] *= 0.02
    rows.append(img)
    y += hh
    return np.concatenate(rows, 0), regions
