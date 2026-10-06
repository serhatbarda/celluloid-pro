"""Colour science helpers for the render tests (numpy, double precision).

Only public standards are used: F-Log2 / F-Gamut C (Fujifilm white papers, constants as in
SPEC 4.1), ITU-R BT.709 / BT.2020 / BT.2100 (PQ = SMPTE ST 2084, HLG), IEC 61966-2-1 (sRGB),
SMPTE RP 431-2 (DCI-P3), CIE 1976 L*a*b*, CIEDE2000.
"""
import numpy as np

# ---------------------------------------------------------------- F-Log2 (SPEC 4.1)
FLOG2 = dict(a=5.555556, b=0.064829, c=0.245281, d=0.384316, e=8.799461, f=0.092864,
             cut1=0.000889, cut2=0.100686685370811)


def flog2_encode(lin):
    k = FLOG2
    lin = np.asarray(lin, dtype=np.float64)
    with np.errstate(invalid="ignore", divide="ignore"):
        hi = k["c"] * np.log10(np.maximum(k["a"] * lin + k["b"], 1e-300)) + k["d"]
    lo = k["e"] * lin + k["f"]
    return np.where(lin >= k["cut1"], hi, lo)


def flog2_decode(code):
    k = FLOG2
    code = np.asarray(code, dtype=np.float64)
    hi = (np.power(10.0, (code - k["d"]) / k["c"]) - k["b"]) / k["a"]
    lo = (code - k["f"]) / k["e"]
    return np.where(code >= k["cut2"], hi, lo)


# ---------------------------------------------------------------- gamuts
D65 = (0.3127, 0.3290)
DCI_WHITE = (0.314, 0.351)
PRIMARIES = {
    "rec709": ((0.640, 0.330), (0.300, 0.600), (0.150, 0.060), D65),
    "rec2020": ((0.708, 0.292), (0.170, 0.797), (0.131, 0.046), D65),
    "p3d65": ((0.680, 0.320), (0.265, 0.690), (0.150, 0.060), D65),
    "p3dci": ((0.680, 0.320), (0.265, 0.690), (0.150, 0.060), DCI_WHITE),
    "fgamutc": ((0.7347, 0.2653), (0.0263, 0.9737), (0.1173, -0.0224), D65),
}


def xy_to_XYZ(xy):
    x, y = xy
    return np.array([x / y, 1.0, (1.0 - x - y) / y])


def rgb_to_xyz_matrix(name):
    r, g, b, w = PRIMARIES[name]
    P = np.stack([xy_to_XYZ(r), xy_to_XYZ(g), xy_to_XYZ(b)], axis=1)
    S = np.linalg.solve(P, xy_to_XYZ(w))
    return P * S[None, :]


def rgb_to_xyz(rgb, name):
    return np.asarray(rgb, np.float64) @ rgb_to_xyz_matrix(name).T


def xyz_to_rgb(xyz, name):
    return np.asarray(xyz, np.float64) @ np.linalg.inv(rgb_to_xyz_matrix(name)).T


# ---------------------------------------------------------------- display decoding
PQ = dict(m1=2610.0 / 16384.0, m2=2523.0 / 4096.0 * 128.0, c1=3424.0 / 4096.0, c2=2413.0 / 4096.0 * 32.0,
          c3=2392.0 / 4096.0 * 32.0)


def pq_to_nits(v):
    v = np.clip(np.asarray(v, np.float64), 0.0, 1.0)
    n = np.power(v, 1.0 / PQ["m2"])
    L = np.maximum(n - PQ["c1"], 0.0) / (PQ["c2"] - PQ["c3"] * n)
    return 10000.0 * np.power(L, 1.0 / PQ["m1"])


def nits_to_pq(nits):
    L = np.clip(np.asarray(nits, np.float64) / 10000.0, 0.0, 1.0)
    p = np.power(L, PQ["m1"])
    return np.power((PQ["c1"] + PQ["c2"] * p) / (1.0 + PQ["c3"] * p), PQ["m2"])


HLG = dict(a=0.17883277, b=0.28466892, c=0.55991073)


def hlg_inverse_oetf(v):
    v = np.clip(np.asarray(v, np.float64), 0.0, 1.0)
    return np.where(v <= 0.5, v * v / 3.0, (np.exp((v - HLG["c"]) / HLG["a"]) + HLG["b"]) / 12.0)


def hlg_to_nits_rgb(rgb, peak=1000.0):
    """BT.2100 HLG reference display: inverse OETF then OOTF (gamma 1.2 at 1000 nits)."""
    E = hlg_inverse_oetf(rgb)
    gamma = 1.2 + 0.42 * np.log10(peak / 1000.0)
    Ys = E @ np.array([0.2627, 0.6780, 0.0593])
    scale = peak * np.power(np.maximum(Ys, 1e-12), gamma - 1.0)
    return E * scale[..., None]


class OutputSpace:
    """How to interpret encoded plug-in output for one output-space option label."""

    def __init__(self, label):
        self.label = label
        l = label.lower()
        if "pq" in l or "2084" in l:
            self.kind, self.prim, self.hdr = "pq", "rec2020", True
        elif "hlg" in l:
            self.kind, self.prim, self.hdr = "hlg", "rec2020", True
        elif "srgb" in l:
            self.kind, self.prim, self.hdr = "srgb", "rec709", False
        elif "dci" in l:
            self.kind, self.prim, self.hdr = "g26", "p3dci", False
        elif "p3" in l:
            self.kind, self.prim, self.hdr = "g26", "p3d65", False
        elif "709" in l and ("2.2" in l):
            self.kind, self.prim, self.hdr = "g22", "rec709", False
        else:  # Rec.709 gamma 2.4 (BT.1886) and unknown -> 2.4
            self.kind, self.prim, self.hdr = "g24", "rec709", False

    def to_linear_rgb(self, v):
        """Encoded -> display-linear RGB. SDR: relative to display white (1.0); HDR: nits/100."""
        v = np.asarray(v, np.float64)
        if self.kind == "g24":
            return np.power(np.clip(v, 0, None), 2.4)
        if self.kind == "g22":
            return np.power(np.clip(v, 0, None), 2.2)
        if self.kind == "g26":
            return np.power(np.clip(v, 0, None), 2.6)
        if self.kind == "srgb":
            v = np.clip(v, 0, None)
            return np.where(v <= 0.04045, v / 12.92, np.power((v + 0.055) / 1.055, 2.4))
        if self.kind == "pq":
            return pq_to_nits(v) / 100.0
        if self.kind == "hlg":
            return hlg_to_nits_rgb(v) / 100.0
        raise ValueError(self.kind)

    def to_xyz(self, v):
        return rgb_to_xyz(self.to_linear_rgb(v), self.prim)

    def nits(self, v):
        """Luminance in nits (SDR assumes a 100 nit display)."""
        return self.to_xyz(v)[..., 1] * 100.0


# ---------------------------------------------------------------- CIELAB / CIEDE2000
WHITE_D65_XYZ = xy_to_XYZ(D65)


def xyz_to_lab(xyz, white=WHITE_D65_XYZ):
    t = np.asarray(xyz, np.float64) / white
    d = 6.0 / 29.0
    f = np.where(t > d ** 3, np.cbrt(t), t / (3 * d * d) + 4.0 / 29.0)
    L = 116.0 * f[..., 1] - 16.0
    a = 500.0 * (f[..., 0] - f[..., 1])
    b = 200.0 * (f[..., 1] - f[..., 2])
    return np.stack([L, a, b], axis=-1)


def lab_to_xyz(lab, white=WHITE_D65_XYZ):
    lab = np.asarray(lab, np.float64)
    fy = (lab[..., 0] + 16.0) / 116.0
    fx = fy + lab[..., 1] / 500.0
    fz = fy - lab[..., 2] / 200.0
    d = 6.0 / 29.0

    def finv(f):
        return np.where(f > d, f ** 3, 3 * d * d * (f - 4.0 / 29.0))
    return np.stack([finv(fx), finv(fy), finv(fz)], axis=-1) * white


def lab_to_lch(lab):
    lab = np.asarray(lab, np.float64)
    C = np.hypot(lab[..., 1], lab[..., 2])
    h = np.degrees(np.arctan2(lab[..., 2], lab[..., 1])) % 360.0
    return np.stack([lab[..., 0], C, h], axis=-1)


def hue_diff(h1, h2):
    """Signed smallest difference h2 - h1 in degrees."""
    return (np.asarray(h2) - np.asarray(h1) + 180.0) % 360.0 - 180.0


def delta_e2000(lab1, lab2):
    lab1 = np.asarray(lab1, np.float64)
    lab2 = np.asarray(lab2, np.float64)
    L1, a1, b1 = lab1[..., 0], lab1[..., 1], lab1[..., 2]
    L2, a2, b2 = lab2[..., 0], lab2[..., 1], lab2[..., 2]
    C1 = np.hypot(a1, b1)
    C2 = np.hypot(a2, b2)
    Cb = 0.5 * (C1 + C2)
    G = 0.5 * (1 - np.sqrt(Cb ** 7 / (Cb ** 7 + 25.0 ** 7)))
    a1p, a2p = (1 + G) * a1, (1 + G) * a2
    C1p, C2p = np.hypot(a1p, b1), np.hypot(a2p, b2)
    h1p = np.degrees(np.arctan2(b1, a1p)) % 360
    h2p = np.degrees(np.arctan2(b2, a2p)) % 360
    dLp = L2 - L1
    dCp = C2p - C1p
    dh = h2p - h1p
    dh = np.where(C1p * C2p == 0, 0, np.where(dh > 180, dh - 360, np.where(dh < -180, dh + 360, dh)))
    dHp = 2 * np.sqrt(C1p * C2p) * np.sin(np.radians(dh / 2))
    Lbp = 0.5 * (L1 + L2)
    Cbp = 0.5 * (C1p + C2p)
    hsum = h1p + h2p
    hbp = np.where(C1p * C2p == 0, hsum,
                   np.where(np.abs(h1p - h2p) <= 180, hsum / 2,
                            np.where(hsum < 360, (hsum + 360) / 2, (hsum - 360) / 2)))
    T = (1 - 0.17 * np.cos(np.radians(hbp - 30)) + 0.24 * np.cos(np.radians(2 * hbp))
         + 0.32 * np.cos(np.radians(3 * hbp + 6)) - 0.20 * np.cos(np.radians(4 * hbp - 63)))
    dtheta = 30 * np.exp(-(((hbp - 275) / 25) ** 2))
    Rc = 2 * np.sqrt(Cbp ** 7 / (Cbp ** 7 + 25.0 ** 7))
    Sl = 1 + 0.015 * (Lbp - 50) ** 2 / np.sqrt(20 + (Lbp - 50) ** 2)
    Sc = 1 + 0.045 * Cbp
    Sh = 1 + 0.015 * Cbp * T
    Rt = -np.sin(np.radians(2 * dtheta)) * Rc
    return np.sqrt((dLp / Sl) ** 2 + (dCp / Sc) ** 2 + (dHp / Sh) ** 2 + Rt * (dCp / Sc) * (dHp / Sh))


def scene_lab_fgamutc(lin_rgb):
    """CIELAB of scene-linear F-Gamut C values (D65 white, Y = 1 is diffuse white)."""
    return xyz_to_lab(rgb_to_xyz(lin_rgb, "fgamutc"))


def self_test():
    """Sanity checks of this module against known values (raises AssertionError)."""
    # F-Log2: 18% grey and round trip
    g = flog2_encode(0.18)
    assert abs(flog2_decode(g) - 0.18) < 1e-9
    x = np.logspace(-6, 4, 200)
    assert np.max(np.abs(flog2_decode(flog2_encode(x)) / x - 1)) < 1e-9
    # Rec.709 matrix Y row
    M = rgb_to_xyz_matrix("rec709")
    assert np.allclose(M[1], [0.2126, 0.7152, 0.0722], atol=2e-4)
    # PQ round trip and 100 nits ~ 0.508
    assert abs(nits_to_pq(100.0) - 0.5081) < 1e-3
    assert abs(pq_to_nits(nits_to_pq(203.0)) - 203.0) < 1e-6
    # CIEDE2000 reference pair (Sharma et al. 2005, pair 1): 2.0425
    de = delta_e2000([50.0, 2.6772, -79.7751], [50.0, 0.0, -82.7485])
    assert abs(de - 2.0425) < 1e-4, de
    # HLG: 0.75 code with 1000 nit display on a neutral = 203 nits (BT.2408 reference white)
    n = hlg_to_nits_rgb(np.array([0.75, 0.75, 0.75]))[1]
    assert abs(n - 203.0) < 1.5, n
    return True


# ---------------------------------------------------------------- camera log encodings (for decode cross-checks)
# Formulas and constants from the manufacturers' published white papers / specifications
# (scene-linear reflectance in, code value 0..1 out). Used to verify that 18% grey and a grey
# wedge encoded for each supported camera map to the same film rendering as F-Log2 C.
def _flog(x):
    a, b, c, d, e, f, cut1 = 0.555556, 0.009468, 0.344676, 0.790453, 8.735631, 0.092864, 0.00089
    return np.where(x >= cut1, c * np.log10(np.maximum(a * x + b, 1e-30)) + d, e * x + f)


def _logc3(x):   # ARRI LogC3, EI 800
    cut, a, b, c, d, e, f = 0.010591, 5.555556, 0.052272, 0.247190, 0.385537, 5.367655, 0.092809
    return np.where(x > cut, c * np.log10(np.maximum(a * x + b, 1e-30)) + d, e * x + f)


def _logc4(x):   # ARRI LogC4
    a = (2.0 ** 18 - 16.0) / 117.45
    b = (1023.0 - 95.0) / 1023.0
    c = 95.0 / 1023.0
    s = (7.0 * np.log(2.0) * 2.0 ** (7.0 - 14.0 * c / b)) / (a * b)
    t = (2.0 ** (14.0 * (-c / b) + 6.0) - 64.0) / a
    return np.where(x >= t, (np.log2(np.maximum(a * x + 64.0, 1e-30)) - 6.0) / 14.0 * b + c, (x - t) / s)


def _slog3(x):   # Sony S-Log3
    return np.where(x >= 0.01125, (420.0 + np.log10(np.maximum((x + 0.01) / 0.19, 1e-30)) * 261.5) / 1023.0,
                    (x * (171.2102946929 - 95.0) / 0.01125 + 95.0) / 1023.0)


def _vlog(x):    # Panasonic V-Log
    return np.where(x < 0.01, 5.6 * x + 0.125, 0.241514 * np.log10(np.maximum(x + 0.00873, 1e-30)) + 0.598206)


def _clog3(x):   # Canon Log 3 (reflectance in, /0.9 as in Canon's specification)
    x = x / 0.9
    return np.where(x < -0.014, -0.36726845 * np.log10(np.maximum(-x * 14.98325 + 1, 1e-30)) + 0.12783901,
                    np.where(x <= 0.014, 1.9754798 * x + 0.12512219,
                             0.36726845 * np.log10(np.maximum(x * 14.98325 + 1, 1e-30)) + 0.12240537))


def _dvi(x):     # DaVinci Intermediate
    A, B, C_, M, cut = 0.0075, 7.0, 0.07329248, 10.44426855, 0.00262409
    return np.where(x > cut, (np.log2(np.maximum(x + A, 1e-30)) + B) * C_, x * M)


def _bmdgen5(x):  # Blackmagic Film Generation 5
    A, B, C_, D, E, cut = 0.08692876065491224, 0.005494072432257808, 0.5300133392291939, 8.283605932402494, 0.09246575342465753, 0.005
    return np.where(x < cut, D * x + E, A * np.log(np.maximum(x + B, 1e-30)) + C_)


def _acescct(x):
    return np.where(x <= 0.0078125, 10.5402377416545 * x + 0.0729055341958355, (np.log2(np.maximum(x, 1e-30)) + 9.72) / 17.52)


LOG_ENCODERS = [
    (r"F-?Log2", flog2_encode), (r"F-?Log\b", _flog), (r"LogC\s*3", _logc3), (r"LogC\s*4", _logc4),
    (r"S-?Log\s*3", _slog3), (r"V-?Log", _vlog), (r"Canon\s*Log\s*3|C-?Log\s*3", _clog3),
    (r"DaVinci\s*Intermediate", _dvi), (r"Film\s*Gen(eration)?\s*5", _bmdgen5), (r"ACEScct", _acescct),
    (r"linear", lambda x: np.asarray(x, np.float64)),
]


def log_encoder_for(label):
    import re
    for pat, fn in LOG_ENCODERS:
        if re.search(pat, label, re.I):
            return fn
    return None
