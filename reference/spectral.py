"""Celluloid Pro reference model -- spectral foundations (NumPy, float64).

Shared by the stock designer, the table generators and the golden-data generator.
Everything here has an exact C++ counterpart in src/core (see the function comments).

Wavelength grid: 380..780 nm in 5 nm steps (81 samples). Integrals are plain sums
(the constant 5 nm step cancels in every normalisation we use).
"""
import json
import math
import os

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, 'data')

WL = np.arange(380.0, 780.0 + 1e-9, 5.0)
NWL = len(WL)                      # 81
TNORM = (WL - 580.0) / 200.0       # normalised wavelength used by the upsampling polynomial


def _csv(name):
    return np.loadtxt(os.path.join(DATA, name), delimiter=',', comments='#', skiprows=2)


CMF = _csv('cie1931_2deg_5nm.csv')[:, 1:4]          # (81,3) xbar ybar zbar
D65 = _csv('cie_d65_5nm.csv')[:, 1]                 # (81,)
DAYLIGHT_BASIS = _csv('cie_daylight_basis_5nm.csv')[:, 1:4]  # S0 S1 S2

# ----------------------------------------------------------------------------------------
# chromaticities / matrices (C++: src/core/ColorMath.cpp)
# ----------------------------------------------------------------------------------------
WHITE_D65 = (0.3127, 0.3290)
WHITE_ACES = (0.32168, 0.33767)
WHITE_DCI = (0.314, 0.351)

PRIMARIES = {
    # name: ((xr,yr),(xg,yg),(xb,yb)), white
    'rec2020':   (((0.708, 0.292), (0.170, 0.797), (0.131, 0.046)), WHITE_D65),   # ITU-R BT.2020
    'rec709':    (((0.640, 0.330), (0.300, 0.600), (0.150, 0.060)), WHITE_D65),   # ITU-R BT.709
    'p3':        (((0.680, 0.320), (0.265, 0.690), (0.150, 0.060)), WHITE_D65),   # SMPTE EG 432-1 (P3-D65)
    'dcip3':     (((0.680, 0.320), (0.265, 0.690), (0.150, 0.060)), WHITE_DCI),   # SMPTE RP 431-2
    'fgamut':    (((0.708, 0.292), (0.170, 0.797), (0.131, 0.046)), WHITE_D65),   # Fujifilm F-Log/F-Log2 data sheets
    'fgamutc':   (((0.7347, 0.2653), (0.0263, 0.9737), (0.1173, -0.0224)), WHITE_D65),  # Fujifilm F-Log2 C data sheet
    'dwg':       (((0.8000, 0.3130), (0.1682, 0.9877), (0.0790, -0.1155)), WHITE_D65),  # Blackmagic DaVinci Wide Gamut
    'awg3':      (((0.6840, 0.3130), (0.2210, 0.8480), (0.0861, -0.1020)), WHITE_D65),  # ARRI Alexa LogC3 white paper
    'awg4':      (((0.7347, 0.2653), (0.1424, 0.8576), (0.0991, -0.0308)), WHITE_D65),  # ARRI LogC4 specification
    'sgamut3cine': (((0.766, 0.275), (0.225, 0.800), (0.089, -0.087)), WHITE_D65),     # Sony S-Gamut3.Cine tech summary
    'vgamut':    (((0.730, 0.280), (0.165, 0.840), (0.100, -0.030)), WHITE_D65),     # Panasonic V-Log/V-Gamut reference
    'cinemagamut': (((0.740, 0.270), (0.170, 1.140), (0.080, -0.100)), WHITE_D65),   # Canon Cinema Gamut white paper
    'bmdwg5':    (((0.7177215, 0.3171181), (0.2280410, 0.8615690), (0.1005841, -0.0820452)),
                  (0.3127170, 0.3290312)),                                           # Blackmagic Gen 5 colour science
    'ap1':       (((0.713, 0.293), (0.165, 0.830), (0.128, 0.044)), WHITE_ACES),     # SMPTE ST 2065-1 / S-2014-004
}


def xy_to_XYZ(xy, Y=1.0):
    x, y = xy
    return np.array([x / y * Y, Y, (1.0 - x - y) / y * Y])


def npm(prim, white):
    """RGB->XYZ normalised primary matrix (SMPTE RP 177)."""
    P = np.array([xy_to_XYZ(p) for p in prim]).T
    W = xy_to_XYZ(white)
    S = np.linalg.solve(P, W)
    return P * S[None, :]


BRADFORD = np.array([[0.8951, 0.2664, -0.1614],
                     [-0.7502, 1.7135, 0.0367],
                     [0.0389, -0.0685, 1.0296]])
CAT16 = np.array([[0.401288, 0.650173, -0.051461],
                  [-0.250268, 1.204414, 0.045854],
                  [-0.002079, 0.048952, 0.953127]])


def cat_matrix(XYZ_src, XYZ_dst, cone=CAT16):
    """von Kries adaptation (D = 1) in the given cone space."""
    s = cone @ XYZ_src
    d = cone @ XYZ_dst
    return np.linalg.inv(cone) @ np.diag(d / s) @ cone


def rgb_to_rgb(src, dst):
    """Matrix from linear RGB `src` primaries to linear RGB `dst`, Bradford CAT when whites differ."""
    ps, ws = PRIMARIES[src]
    pd, wd = PRIMARIES[dst]
    M = npm(ps, ws)
    if abs(ws[0] - wd[0]) > 1e-9 or abs(ws[1] - wd[1]) > 1e-9:
        M = cat_matrix(xy_to_XYZ(ws), xy_to_XYZ(wd), BRADFORD) @ M
    return np.linalg.inv(npm(pd, wd)) @ M


M2020_TO_XYZ = npm(*PRIMARIES['rec2020'])
XYZ_TO_2020 = np.linalg.inv(M2020_TO_XYZ)
LUM2020 = M2020_TO_XYZ[1].copy()

# ----------------------------------------------------------------------------------------
# illuminants (C++: src/core/Spectral.cpp)
# ----------------------------------------------------------------------------------------
C2 = 1.4388e-2  # m K (ITS-90 second radiation constant, as used by CIE 015:2018)


def planck(T):
    lam = WL * 1e-9
    return 1.0 / (lam ** 5 * (np.exp(C2 / (lam * T)) - 1.0))


def daylight(cct):
    """CIE D-series relative SPD for a correlated colour temperature (CIE 015:2018 eq. 4.x)."""
    T = float(cct)
    if T <= 7000.0:
        xd = -4.6070e9 / T ** 3 + 2.9678e6 / T ** 2 + 0.09911e3 / T + 0.244063
    else:
        xd = -2.0064e9 / T ** 3 + 1.9018e6 / T ** 2 + 0.24748e3 / T + 0.237040
    yd = -3.000 * xd * xd + 2.870 * xd - 0.275
    m = 0.0241 + 0.2562 * xd - 0.7341 * yd
    m1 = (-1.3515 - 1.7703 * xd + 5.9114 * yd) / m
    m2 = (0.0300 - 31.4424 * xd + 30.0717 * yd) / m
    # CIE recommends rounding M1, M2 to 3 decimals for the tabulated D-illuminants
    m1 = round(m1, 3)
    m2 = round(m2, 3)
    return DAYLIGHT_BASIS[:, 0] + m1 * DAYLIGHT_BASIS[:, 1] + m2 * DAYLIGHT_BASIS[:, 2]


def smooth_step_edge(lo, hi):
    """Smooth 0->1 edge over [lo,hi] nm (C1 smoothstep), vector over WL."""
    t = np.clip((WL - lo) / (hi - lo), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def illuminant(spec):
    """Designed light sources. spec = dict(type=..., ...). Normalised to max 1."""
    typ = spec['type']
    if typ == 'blackbody':
        s = planck(spec['cct'])
    elif typ == 'daylight':
        s = daylight(spec['cct'])
    elif typ == 'd65':
        s = D65.copy()
    else:
        raise ValueError(typ)
    if 'uvcut' in spec:     # long-pass edge (lamp housing / UV filter)
        lo, hi = spec['uvcut']
        s = s * smooth_step_edge(lo, hi)
    if 'ircut' in spec:     # heat-absorbing glass
        lo, hi = spec['ircut']
        s = s * (1.0 - smooth_step_edge(lo, hi) * spec.get('irdepth', 1.0))
    return s / s.max()


def lobes(params):
    """Sum of asymmetric Gaussian lobes: [[peak_nm, sigmaLeft, sigmaRight, amplitude], ...]."""
    out = np.zeros(NWL)
    for (mu, sl, sr, amp) in params:
        sig = np.where(WL < mu, sl, sr)
        out += amp * np.exp(-0.5 * ((WL - mu) / sig) ** 2)
    return out


def XYZ_of(spd):
    """XYZ (unnormalised sums) of a stimulus spectrum."""
    return spd @ CMF


# ----------------------------------------------------------------------------------------
# octahedral direction mapping of working RGB (C++/kernel: ColorKernels.h cel_oct_*)
# ----------------------------------------------------------------------------------------
E1 = np.array([1.0, -1.0, 0.0]) / math.sqrt(2.0)
E2 = np.array([1.0, 1.0, -2.0]) / math.sqrt(6.0)
E3 = np.array([1.0, 1.0, 1.0]) / math.sqrt(3.0)


def oct_encode(rgb):
    """rgb (...,3) -> (u, v, L1). Directions with L1 == 0 return u=v=0."""
    a = rgb @ E1
    b = rgb @ E2
    n = rgb @ E3
    L1 = np.abs(a) + np.abs(b) + np.abs(n)
    inv = np.where(L1 > 0, 1.0 / np.where(L1 > 0, L1, 1.0), 0.0)
    u = a * inv
    v = b * inv
    su = np.where(u >= 0, 1.0, -1.0)
    sv = np.where(v >= 0, 1.0, -1.0)
    fu = (1.0 - np.abs(v)) * su
    fv = (1.0 - np.abs(u)) * sv
    neg = n < 0
    u2 = np.where(neg, fu, u)
    v2 = np.where(neg, fv, v)
    return u2, v2, L1


def oct_decode(u, v):
    """(u,v) in [-1,1]^2 -> unit-L1 direction in working RGB."""
    n = 1.0 - np.abs(u) - np.abs(v)
    su = np.where(u >= 0, 1.0, -1.0)
    sv = np.where(v >= 0, 1.0, -1.0)
    a = np.where(n < 0, (1.0 - np.abs(v)) * su, u)
    b = np.where(n < 0, (1.0 - np.abs(u)) * sv, v)
    return a[..., None] * E1 + b[..., None] * E2 + n[..., None] * E3


def load_json(name):
    with open(os.path.join(HERE, name)) as f:
        return json.load(f)
