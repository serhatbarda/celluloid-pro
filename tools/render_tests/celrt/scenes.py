"""Synthetic test scenes, encoded in F-Log2 C (F-Log2 curve, F-Gamut C primaries).

All colour values are designed here from CIELAB coordinates or simple parametric rules; no
third-party chart data is used. Images are float32 RGBA, top-down rows (the ofxhost .f32
convention), alpha = 1.
"""
import numpy as np

from . import colour as C

PATCH = 2          # pixels per patch side in the analysis chart
PER_ROW = 64       # patches per row

# 24 designed reference colours (CIELAB, D65): name, L*, a*, b*
CHART24 = [
    ("deep skin", 38, 13, 14), ("fair skin", 66, 16, 17), ("sky", 50, -3, -22), ("foliage", 43, -14, 21),
    ("violet bloom", 55, 10, -24), ("teal", 70, -30, 0), ("orange", 62, 35, 58), ("deep blue", 40, 12, -44),
    ("rose", 51, 45, 15), ("plum", 30, 22, -20), ("lime", 71, -25, 56), ("amber", 71, 18, 66),
    ("blue", 29, 18, -50), ("green", 55, -38, 30), ("red", 42, 52, 27), ("yellow", 81, 2, 78),
    ("magenta", 51, 48, -14), ("cyan", 51, -26, -26),
    ("white 95", 95, 0, 0), ("grey 80", 80, 0, 0), ("grey 65", 65, 0, 0), ("grey 50", 50, 0, 0),
    ("grey 35", 35, 0, 0), ("black 20", 20, 0, 0),
]

# designed skin tones, light to deep (CIELAB, D65)
SKIN = [("skin L78", 78, 10, 16), ("skin L70", 70, 13, 18), ("skin L62", 62, 15, 21),
        ("skin L53", 53, 16, 22), ("skin L43", 43, 15, 20), ("skin L33", 33, 12, 16)]

WEDGE_STOPS = np.round(np.arange(-10.0, 10.0 + 1e-9, 0.125), 6)   # 161 grey steps
TORTURE_STOPS = np.round(np.arange(-4.0, 8.0 + 1e-9, 0.25), 6)    # 49 steps per hue
TORTURE_HUES = list(range(0, 360, 30))


def lab_to_fgc_linear(L, a, b):
    xyz = C.lab_to_xyz(np.array([L, a, b], np.float64))
    return C.xyz_to_rgb(xyz, "fgamutc")


def hue_primary_fgc(h_deg):
    """Maximally saturated F-Gamut C colour of hue angle h (HSV-style on the camera primaries,
    includes imaginary/out-of-spectral-locus colours, unit max component)."""
    h = (h_deg % 360) / 60.0
    i = int(np.floor(h))
    f = h - i
    rgb = [(1, f, 0), (1 - f, 1, 0), (0, 1, f), (0, 1 - f, 1), (f, 0, 1), (1, 0, 1 - f)][i % 6]
    return np.array(rgb, np.float64)


class Chart:
    """The analysis chart: a list of patches laid out PATCH x PATCH pixels each."""

    def __init__(self):
        self.patches = []   # dicts: name, group, code (F-Log2 C RGB), lin (or None), extra

    def add(self, name, group, lin=None, code=None, **extra):
        if code is None:
            code = C.flog2_encode(np.asarray(lin, np.float64))
        self.patches.append(dict(name=name, group=group, code=np.asarray(code, np.float64),
                                 lin=None if lin is None else np.asarray(lin, np.float64), **extra))

    def group(self, g):
        return [i for i, p in enumerate(self.patches) if p["group"] == g]

    def image(self):
        n = len(self.patches)
        rows = (n + PER_ROW - 1) // PER_ROW
        img = np.zeros((rows * PATCH, PER_ROW * PATCH, 4), np.float32)
        img[..., :3] = C.flog2_encode(0.18)
        img[..., 3] = 1.0
        for i, p in enumerate(self.patches):
            r, c = divmod(i, PER_ROW)
            img[r * PATCH:(r + 1) * PATCH, c * PATCH:(c + 1) * PATCH, :3] = p["code"].astype(np.float32)
        return img

    def sample(self, out):
        """Mean RGBA per patch from a rendered chart image (H x W x 4)."""
        res = np.zeros((len(self.patches), 4), np.float64)
        for i in range(len(self.patches)):
            r, c = divmod(i, PER_ROW)
            res[i] = out[r * PATCH:(r + 1) * PATCH, c * PATCH:(c + 1) * PATCH].reshape(-1, 4).mean(axis=0)
        return res


def analysis_chart():
    ch = Chart()
    for s in WEDGE_STOPS:
        ch.add("grey %+.3f" % s, "wedge", lin=np.full(3, 0.18 * 2.0 ** s), stop=float(s))
    for name, L, a, b in CHART24:
        ch.add(name, "chart24", lin=lab_to_fgc_linear(L, a, b), lab=(L, a, b))
    for ev in (-2, 2):
        for name, L, a, b in CHART24:
            ch.add("%s %+d" % (name, ev), "chart24_ev%+d" % ev, lin=lab_to_fgc_linear(L, a, b) * 2.0 ** ev, lab=(L, a, b))
    for name, L, a, b in SKIN:
        ch.add(name, "skin", lin=lab_to_fgc_linear(L, a, b), lab=(L, a, b))
    for h in TORTURE_HUES:
        base = hue_primary_fgc(h)
        for s in TORTURE_STOPS:
            ch.add("hue %d %+.2f" % (h, s), "torture", lin=base * 0.18 * 2.0 ** s, hue=h, stop=float(s))
    # partially desaturated sweeps (in-gamut saturated colours) for smoothness
    for h in TORTURE_HUES[::2]:
        base = 0.35 + 0.65 * hue_primary_fgc(h)
        for s in TORTURE_STOPS:
            ch.add("sat %d %+.2f" % (h, s), "sweep", lin=base * 0.18 * 2.0 ** s, hue=h, stop=float(s))
    # out of range / pathological code values and linear values
    specials_code = [(-0.5, -0.5, -0.5), (-0.1, 0.2, 0.4), (0.0, 0.0, 0.0), (1.0, 1.0, 1.0), (1.2, 1.2, 1.2),
                     (1.5, 0.4, 0.4), (2.0, 2.0, 2.0), (4.0, 0.0, 0.0), (0.0, 4.0, 0.0), (0.0, 0.0, 4.0),
                     (10.0, 10.0, 10.0), (100.0, 0.5, -100.0), (0.39, 0.39, 1.6), (-3.0, 0.39, 0.39)]
    for i, c in enumerate(specials_code):
        ch.add("code %s" % (c,), "specials", code=np.array(c, np.float64))
    specials_lin = [(-1.0, -1.0, -1.0), (-0.05, 0.18, 0.18), (1e4, 1e4, 1e4), (1e6, 0.0, 0.0), (0.0, 1e6, 0.0),
                    (1e10, 1e10, 1e10), (1e3, -1e3, 0.0), (0.18, 0.18, 0.18)]
    for c in specials_lin:
        ch.add("lin %s" % (c,), "specials", lin=np.array(c, np.float64))
    return ch


def lattice(n):
    """n^3 lattice of input code values, r fastest (as in .cube files): image n*n wide, n tall."""
    img = np.zeros((n, n * n, 4), np.float32)
    v = np.arange(n, dtype=np.float64) / (n - 1)
    for b in range(n):
        for g in range(n):
            img[b, g * n:(g + 1) * n, 0] = v
            img[b, g * n:(g + 1) * n, 1] = v[g]
            img[b, g * n:(g + 1) * n, 2] = v[b]
    img[..., 3] = 1.0
    return img


def lattice_unpack(out, n):
    """Rendered lattice image -> (n^3, 3) array in .cube order (r fastest)."""
    res = np.zeros((n, n, n, 3), np.float64)   # b, g, r
    for b in range(n):
        for g in range(n):
            res[b, g] = out[b, g * n:(g + 1) * n, :3]
    return res.reshape(-1, 3)


def random_codes(n, seed=11):
    rng = np.random.default_rng(seed)
    img = np.zeros((1, n, 4), np.float32)
    img[0, :, :3] = rng.uniform(0.0, 1.0, (n, 3))
    img[0, :, 3] = 1.0
    return img


def _smooth_noise(h, w, scale, rng):
    gh, gw = max(2, h // scale + 2), max(2, w // scale + 2)
    g = rng.standard_normal((gh, gw))
    ys = np.linspace(0, gh - 1.001, h)
    xs = np.linspace(0, gw - 1.001, w)
    y0, x0 = np.floor(ys).astype(int), np.floor(xs).astype(int)
    fy, fx = (ys - y0)[:, None], (xs - x0)[None, :]
    fy, fx = fy * fy * (3 - 2 * fy), fx * fx * (3 - 2 * fx)
    a = g[y0][:, x0]
    b = g[y0][:, x0 + 1]
    c = g[y0 + 1][:, x0]
    d = g[y0 + 1][:, x0 + 1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def natural_linear(w, h, seed=5):
    """A synthetic 'location' frame in scene-linear F-Gamut C: sky, sun, landscape, face, grey
    card, colour swatches, practical lights on a dark wall, fine detail. Resolution independent."""
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
    u, v = xx / w, yy / h                     # 0..1, v down
    s = h / 1080.0                            # feature scale
    img = np.zeros((h, w, 3), np.float64)
    sky_top = lab_to_fgc_linear(62, -4, -30) * 2.2
    sky_hor = lab_to_fgc_linear(86, -2, -8) * 2.4
    t = np.clip(v / 0.45, 0, 1)[..., None]
    img[:] = sky_top * (1 - t) + sky_hor * t
    # sun with glow (+10 stops core) top right
    d = np.hypot(u - 0.82, (v - 0.12) * h / w * 1.0) * w / h
    img += (0.18 * 2.0 ** 10) * (d < 0.018)[..., None] + 6.0 * np.exp(-d / 0.05)[..., None] * np.array([1.0, 0.9, 0.7])
    # ground: foliage / earth with texture
    ground = v > 0.45 + 0.04 * np.sin(u * 7.0)
    tex = (0.6 * _smooth_noise(h, w, max(6, int(48 * s)), rng) + 0.3 * _smooth_noise(h, w, max(3, int(14 * s)), rng)
           + 0.15 * _smooth_noise(h, w, max(2, int(5 * s)), rng))
    foliage = lab_to_fgc_linear(40, -18, 24)
    earth = lab_to_fgc_linear(45, 8, 18)
    mix = np.clip(0.5 + 0.35 * tex, 0, 1)[..., None]
    gcol = foliage * mix + earth * (1 - mix)
    img = np.where(ground[..., None], gcol * (0.6 + 0.3 * (v[..., None] - 0.45)), img)
    # dark wall with practical lights (lower left) - halation / bloom targets
    wall = (u < 0.34) & (v > 0.55)
    img = np.where(wall[..., None], np.full(3, 0.012), img)
    for (cx, cy, r, ev, col) in [(0.06, 0.66, 0.006, 8, (1.0, 0.75, 0.45)), (0.16, 0.70, 0.010, 7, (1.0, 0.9, 0.8)),
                                 (0.26, 0.64, 0.004, 9, (0.9, 0.95, 1.0)), (0.10, 0.86, 0.016, 6, (1.0, 0.55, 0.25)),
                                 (0.24, 0.88, 0.008, 10, (1.0, 1.0, 1.0))]:
        dd = np.hypot((u - cx) * w / h, v - cy)
        spot = (dd < r)[..., None] * (0.18 * 2.0 ** ev) * np.array(col)
        img = np.where(wall[..., None], img + spot, img)
    # neon tube (saturated, very bright)
    tube = (np.abs(v - 0.60) < 0.006) & (u > 0.04) & (u < 0.30)
    img = np.where(tube[..., None], np.array([0.05, 2.5, 6.0]), img)
    # face-like ellipse with skin tone and soft shading
    fx_, fy_ = (u - 0.52) / 0.075, (v - 0.55) / 0.13
    face = fx_ ** 2 + fy_ ** 2 < 1.0
    skin = lab_to_fgc_linear(64, 15, 20)
    shade = (0.75 + 0.35 * np.clip(1 - fx_ - 0.3 * fy_, 0, 1.5))[..., None]
    img = np.where(face[..., None], skin * shade, img)
    # grey card and swatches
    card = (u > 0.66) & (u < 0.76) & (v > 0.56) & (v < 0.74)
    img = np.where(card[..., None], np.full(3, 0.18), img)
    sw = [(70, -30, 0), (62, 35, 58), (42, 52, 27), (40, 12, -44), (55, -38, 30), (81, 2, 78)]
    for i, (L, a, b) in enumerate(sw):
        m = (u > 0.79 + 0.033 * (i % 3)) & (u < 0.81 + 0.033 * (i % 3)) & (v > 0.56 + 0.1 * (i // 3)) & (v < 0.64 + 0.1 * (i // 3))
        img = np.where(m[..., None], lab_to_fgc_linear(L, a, b), img)
    # fine detail stripes (acutance) bottom right
    stripes = (u > 0.66) & (u < 0.96) & (v > 0.82) & (v < 0.94)
    freq = 2 * np.pi * (0.5 + 6.0 * (u - 0.66) / 0.30) * w / (60.0 * max(s, 0.25))
    img = np.where(stripes[..., None], (0.06 + 0.3 * (np.sin(freq * u) > 0))[..., None] * np.ones(3), img)
    # deep shadow strip at the bottom, -8 .. -4 stops
    bottom = v > 0.965
    img = np.where(bottom[..., None], (0.18 * 2.0 ** (-8 + 4 * u))[..., None] * np.ones(3), img)
    return img


def natural(w, h, seed=5):
    lin = natural_linear(w, h, seed)
    out = np.zeros((h, w, 4), np.float32)
    out[..., :3] = C.flog2_encode(lin).astype(np.float32)
    out[..., 3] = 1.0
    return out


def highlights(w, h):
    """Dark field with bright points of increasing exposure + a step edge: halation/bloom/acutance."""
    lin = np.full((h, w, 3), 0.02)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
    pts = []
    for i, ev in enumerate([4, 6, 8, 10]):
        cx, cy = w * (0.15 + 0.2 * i), h * 0.3
        r = max(1.5, h * 0.01)
        m = np.hypot(xx - cx, yy - cy) < r
        lin[m] = 0.18 * 2.0 ** ev
        pts.append((cx, cy, ev))
    lin[int(h * 0.6):, : w // 2] = 0.05
    lin[int(h * 0.6):, w // 2:] = 0.5
    out = np.zeros((h, w, 4), np.float32)
    out[..., :3] = C.flog2_encode(lin).astype(np.float32)
    out[..., 3] = 1.0
    return out, pts


def halation_daylight(w, h):
    """Halation acceptance scene (1920-px-relative sizes): a bright overcast sky (+4.2 stops, as
    in a person-against-the-sky shot) above a dark window and a skin patch that touch it, plus a
    +8-stop practical (small disc) on a dark wall in the lower half. Returns (image, regions)."""
    lin = np.full((h, w, 3), 0.03)
    s = w / 1920.0
    sky = 0.18 * 2.0 ** 4.2 * np.array([0.96, 1.0, 1.12])
    lin[: int(h * 0.4)] = sky
    y0 = int(h * 0.4)
    win = (slice(y0, y0 + int(140 * s)), slice(int(w * 0.10), int(w * 0.30)))
    lin[win] = 0.012
    skin_lin = lab_to_fgc_linear(58, 15, 21)
    sk = (slice(y0, y0 + int(200 * s)), slice(int(w * 0.45), int(w * 0.65)))
    lin[sk] = skin_lin
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
    cx, cy, r = w * 0.8, h * 0.75, 6.0 * s
    lin[np.hypot(xx - cx, yy - cy) < r] = 0.18 * 2.0 ** 8
    out = np.zeros((h, w, 4), np.float32)
    out[..., :3] = C.flog2_encode(lin).astype(np.float32)
    out[..., 3] = 1.0
    regions = dict(window=(slice(y0 + int(4 * s), win[0].stop - int(4 * s)), slice(win[1].start + int(4 * s), win[1].stop - int(4 * s))),
                   skin=(slice(y0, sk[0].stop), sk[1]), practical=(cx, cy, r, s))
    return out, regions


def grey_field(w, h, lin=0.18):
    out = np.zeros((h, w, 4), np.float32)
    out[..., :3] = np.float32(C.flog2_encode(lin))
    out[..., 3] = 1.0
    return out


def display_scene(w, h):
    """Display-referred (Rec.709 gamma 2.4 encoded) frame for Texture Only mode."""
    lin = natural_linear(w, h)
    y = lin / (lin + 0.5) * 1.3
    out = np.zeros((h, w, 4), np.float32)
    out[..., :3] = np.clip(np.power(np.clip(y, 0, 1), 1 / 2.4), 0, 1).astype(np.float32)
    out[..., 3] = 1.0
    return out


def write_f32(path, img):
    np.ascontiguousarray(img, dtype="<f4").tofile(path)


def read_f32(path, w, h):
    a = np.fromfile(path, dtype="<f4")
    if a.size != w * h * 4:
        raise ValueError("%s: %d floats, expected %dx%dx4" % (path, a.size, w, h))
    return a.reshape(h, w, 4)
