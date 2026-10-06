"""Celluloid Pro -- NumPy reference implementation of the spectral film model (float64).

This mirrors src/core (host bakes) + src/kernels/ColorKernels.h (per-pixel evaluation)
function by function, with the same LUT grids and interpolation rules, so that the C++
implementation can be checked against it (tests/core, reference/golden.py). It is also the
tool the stocks in reference/stocks.json were designed with (reference/design.py).

Pipeline (SPEC section 4):
  decode(input transfer + gamut -> scene-linear Rec.2020) x 2^exposure
  -> spectral layer exposures (octahedral direction LUT x L1 magnitude)
  -> interlayer (DIR) inhibition -> negative characteristic curves (push/pull, neutralize)
  -> grain in dye density -> [neg bleach: retained silver]
  -> PRINT:   printer-light LUT (neg dyes -> print log exposure) -> print curves -> [print silver]
              -> projection LUT (print dyes -> log XYZ under the projector) -> CAT16 to D65
  -> SCAN:    scanner LUT (neg dyes -> Status-M-like densities) -> balanced inversion
              -> colour matrix -> gentle scanner curve
  -> REVERSAL: positive curves -> projection LUT of the reversal dyes
  -> output (display primaries, smooth gamut compression, SDR limiter / HDR unroll, EOTF^-1)
"""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import spectral as sp  # noqa: E402

LOG10_018 = math.log10(0.18)

# ---------------------------------------------------------------- constants (== ColorState.h)
N_CURVE = 1024
NEG_X0, NEG_X1 = -7.0, 4.0          # log10 exposure domain of negative / reversal curves
PRT_U0, PRT_U1 = -5.0, 3.0          # print curve domain (log10 print exposure incl. offsets)
SCN_W0, SCN_W1 = -6.0, 4.0          # scanner tone curve domain (log10 relative to grey)
SCN_XA, SCN_XB = -6.0, 3.5          # neutral ramp sampled for the scan inversion (log10 E)
SCN_NFWD = 1024
N_LUT3 = 48
LAYER_W = np.array([0.30, 0.50, 0.20])   # luminance-like weights of the R,G,B-sensitive layers
GRAIN_EPS = 0.02
POINT = 0.025                        # log exposure per printer point
HDR_GREY_NITS = 15.0
HDR_E0, HDR_ER = 0.30, 0.90          # unroll starts 1 stop over grey, log10 scale of the roll
GC_THR, GC_P = 0.85, 2.0             # gamut compression threshold / power (on saturation ratio)
SDR_KNEE = 0.95                      # SDR max-channel limiter knee (display linear)
HDR_KNEE = 0.80                      # HDR peak limiter knee (fraction of peak)

INPUTS = ['FLog2C', 'FLog2', 'FLog', 'DaVinciIntermediate', 'ArriLogC3', 'ArriLogC4',
          'SLog3SGamut3Cine', 'VLog', 'CanonLog3', 'BMDFilmGen5', 'ACEScct', 'LinearRec2020']
INPUT_GAMUT = {'FLog2C': 'fgamutc', 'FLog2': 'fgamut', 'FLog': 'fgamut', 'DaVinciIntermediate': 'dwg',
               'ArriLogC3': 'awg3', 'ArriLogC4': 'awg4', 'SLog3SGamut3Cine': 'sgamut3cine',
               'VLog': 'vgamut', 'CanonLog3': 'cinemagamut', 'BMDFilmGen5': 'bmdwg5',
               'ACEScct': 'ap1', 'LinearRec2020': 'rec2020'}
OUTPUTS = ['Rec709_G24', 'Rec709_sRGB', 'P3D65_G26', 'P3DCI_G26', 'Rec2100_PQ', 'Rec2100_HLG']
OUTPUT_GAMUT = {'Rec709_G24': 'rec709', 'Rec709_sRGB': 'rec709', 'P3D65_G26': 'p3', 'P3DCI_G26': 'dcip3',
                'Rec2100_PQ': 'rec2020', 'Rec2100_HLG': 'rec2020'}

# =========================================================================== transfer functions
# Every constant cited from the camera maker's published document (same citations in
# src/kernels/ColorTransfer.h).
FLOG2 = dict(a=5.555556, b=0.064829, c=0.245281, d=0.384316, e=8.799461, f=0.092864,
             cut1=0.000889, cut2=0.100686685370811)   # Fujifilm "F-Log2 Data Sheet" Ver.1.0
FLOG = dict(a=0.555556, b=0.009468, c=0.344676, d=0.790453, e=8.735631, f=0.092864,
            cut1=0.00089, cut2=0.100537775223865)      # Fujifilm "F-Log Data Sheet" Ver.1.1
LOGC3 = dict(cut=0.010591, a=5.555556, b=0.052272, c=0.247190, d=0.385537, e=5.367655, f=0.092809)  # ARRI EI800
LOGC4_A = (2.0 ** 18 - 16.0) / 117.45
LOGC4_B = (1023.0 - 95.0) / 1023.0
LOGC4_C = 95.0 / 1023.0
LOGC4_S = (7.0 * math.log(2.0) * 2.0 ** (7.0 - 14.0 * LOGC4_C / LOGC4_B)) / (LOGC4_A * LOGC4_B)
LOGC4_T = (2.0 ** (14.0 * (-LOGC4_C / LOGC4_B) + 6.0) - 64.0) / LOGC4_A
DI = dict(A=0.0075, B=7.0, C=0.07329248, M=10.44426855, LIN_CUT=0.00262409, LOG_CUT=0.02740668)
BMD5 = dict(A=0.08692876065491224, B=0.005494072432257808, C=0.5300133392291939,
            D=8.283605932402494, E=0.09246575342465753, LIN_CUT=0.005)
VLOG = dict(cut1=0.01, cut2=0.181, b=0.00873, c=0.241514, d=0.598206)
ACESCCT = dict(XB=0.0078125, YB=0.155251141552511, A=10.5402377416545, B=0.0729055341958355)


def decode_transfer(name, v):
    v = np.asarray(v, dtype=np.float64)
    if name in ('FLog2C', 'FLog2', 'FLog'):
        k = FLOG if name == 'FLog' else FLOG2
        return np.where(v >= k['cut2'], (10.0 ** ((v - k['d']) / k['c']) - k['b']) / k['a'], (v - k['f']) / k['e'])
    if name == 'DaVinciIntermediate':
        return np.where(v <= DI['LOG_CUT'], v / DI['M'], 2.0 ** (v / DI['C'] - DI['B']) - DI['A'])
    if name == 'ArriLogC3':
        k = LOGC3
        return np.where(v > k['e'] * k['cut'] + k['f'], (10.0 ** ((v - k['d']) / k['c']) - k['b']) / k['a'],
                        (v - k['f']) / k['e'])
    if name == 'ArriLogC4':
        return np.where(v >= 0.0, (2.0 ** (14.0 * (v - LOGC4_C) / LOGC4_B + 6.0) - 64.0) / LOGC4_A,
                        v * LOGC4_S + LOGC4_T)
    if name == 'SLog3SGamut3Cine':
        return np.where(v >= 171.2102946929 / 1023.0, 10.0 ** ((v * 1023.0 - 420.0) / 261.5) * (0.18 + 0.01) - 0.01,
                        (v * 1023.0 - 95.0) * 0.01125000 / (171.2102946929 - 95.0))
    if name == 'VLog':
        k = VLOG
        return np.where(v < k['cut2'], (v - 0.125) / 5.6, 10.0 ** ((v - k['d']) / k['c']) - k['b'])
    if name == 'CanonLog3':
        x = np.where(v < 0.097465473, -(10.0 ** ((0.12783901 - v) / 0.36726845) - 1.0) / 14.98325,
                     np.where(v <= 0.15277891, (v - 0.12512219) / 1.9754798,
                              (10.0 ** ((v - 0.12240537) / 0.36726845) - 1.0) / 14.98325))
        return x * 0.9
    if name == 'BMDFilmGen5':
        k = BMD5
        log_cut = k['D'] * k['LIN_CUT'] + k['E']
        return np.where(v < log_cut, (v - k['E']) / k['D'], np.exp((v - k['C']) / k['A']) - k['B'])
    if name == 'ACEScct':
        k = ACESCCT
        hi = (math.log2(65504.0) + 9.72) / 17.52
        return np.where(v <= k['YB'], (v - k['B']) / k['A'], np.where(v < hi, 2.0 ** (v * 17.52 - 9.72), 65504.0))
    if name == 'LinearRec2020':
        return v
    raise ValueError(name)


def encode_transfer(name, x):
    x = np.asarray(x, dtype=np.float64)
    if name in ('FLog2C', 'FLog2', 'FLog'):
        k = FLOG if name == 'FLog' else FLOG2
        return np.where(x >= k['cut1'], k['c'] * np.log10(np.maximum(k['a'] * x + k['b'], 1e-30)) + k['d'], k['e'] * x + k['f'])
    if name == 'DaVinciIntermediate':
        return np.where(x <= DI['LIN_CUT'], x * DI['M'], (np.log2(np.maximum(x + DI['A'], 1e-30)) + DI['B']) * DI['C'])
    if name == 'ArriLogC3':
        k = LOGC3
        return np.where(x > k['cut'], k['c'] * np.log10(np.maximum(k['a'] * x + k['b'], 1e-30)) + k['d'], k['e'] * x + k['f'])
    if name == 'ArriLogC4':
        return np.where(x >= LOGC4_T, (np.log2(np.maximum(LOGC4_A * x + 64.0, 1e-30)) - 6.0) / 14.0 * LOGC4_B + LOGC4_C,
                        (x - LOGC4_T) / LOGC4_S)
    if name == 'SLog3SGamut3Cine':
        return np.where(x >= 0.01125000,
                        (420.0 + np.log10(np.maximum((x + 0.01) / (0.18 + 0.01), 1e-30)) * 261.5) / 1023.0,
                        (x * (171.2102946929 - 95.0) / 0.01125000 + 95.0) / 1023.0)
    if name == 'VLog':
        k = VLOG
        return np.where(x < k['cut1'], 5.6 * x + 0.125, k['c'] * np.log10(np.maximum(x + k['b'], 1e-30)) + k['d'])
    if name == 'CanonLog3':
        y = x / 0.9
        return np.where(y < -0.014, -0.36726845 * np.log10(np.maximum(1.0 - 14.98325 * y, 1e-30)) + 0.12783901,
                        np.where(y <= 0.014, 1.9754798 * y + 0.12512219,
                                 0.36726845 * np.log10(np.maximum(14.98325 * y + 1.0, 1e-30)) + 0.12240537))
    if name == 'BMDFilmGen5':
        k = BMD5
        return np.where(x < k['LIN_CUT'], k['D'] * x + k['E'], k['A'] * np.log(np.maximum(x + k['B'], 1e-30)) + k['C'])
    if name == 'ACEScct':
        k = ACESCCT
        return np.where(x <= k['XB'], k['A'] * x + k['B'], (np.log2(np.maximum(x, 1e-30)) + 9.72) / 17.52)
    if name == 'LinearRec2020':
        return x
    raise ValueError(name)


def input_matrix(name):
    return sp.rgb_to_rgb(INPUT_GAMUT[name], 'rec2020')


# PQ (SMPTE ST 2084) and HLG (ITU-R BT.2100)
PQ_M1, PQ_M2 = 2610.0 / 16384.0, 2523.0 / 4096.0 * 128.0
PQ_C1, PQ_C2, PQ_C3 = 3424.0 / 4096.0, 2413.0 / 4096.0 * 32.0, 2392.0 / 4096.0 * 32.0
HLG_A, HLG_B, HLG_C = 0.17883277, 0.28466892, 0.55991073


def pq_encode(nits):
    y = np.clip(np.asarray(nits) / 10000.0, 0.0, 1.0)
    p = y ** PQ_M1
    return ((PQ_C1 + PQ_C2 * p) / (1.0 + PQ_C3 * p)) ** PQ_M2


def pq_decode(v):
    v = np.clip(np.asarray(v, dtype=np.float64), 0.0, 1.0)
    p = v ** (1.0 / PQ_M2)
    return 10000.0 * (np.maximum(p - PQ_C1, 0.0) / (PQ_C2 - PQ_C3 * p)) ** (1.0 / PQ_M1)


def hlg_oetf(e):
    e = np.maximum(np.asarray(e, dtype=np.float64), 0.0)
    return np.where(e <= 1.0 / 12.0, np.sqrt(3.0 * e), HLG_A * np.log(np.maximum(12.0 * e - HLG_B, 1e-30)) + HLG_C)


def hlg_oetf_inv(v):
    v = np.maximum(np.asarray(v, dtype=np.float64), 0.0)
    return np.where(v <= 0.5, v * v / 3.0, (np.exp((v - HLG_C) / HLG_A) + HLG_B) / 12.0)


def gamma_enc(x, g):
    return np.sign(x) * np.abs(x) ** (1.0 / g)


def srgb_enc(x):
    a = np.abs(x)
    return np.sign(x) * np.where(a <= 0.0031308, 12.92 * a, 1.055 * a ** (1.0 / 2.4) - 0.055)


def srgb_dec(v):
    a = np.abs(v)
    return np.sign(v) * np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)


def output_matrix(name):
    """XYZ (D65-adapted, Y relative) -> linear output RGB. DCI: D65 white simulated, scaled so
    that the D65 white's largest channel is 1.0 (neutrals stay D65)."""
    prim, white = sp.PRIMARIES[OUTPUT_GAMUT[name]]
    M = np.linalg.inv(sp.npm(prim, white))
    if name == 'P3DCI_G26':
        w = M @ sp.xy_to_XYZ(sp.WHITE_D65)
        M = M / w.max()
    return M


# =========================================================================== stock construction
class StockLib:
    def __init__(self, path='stocks.json'):
        self.j = sp.load_json(path)
        self.negs = self.j['negatives']
        self.prints = self.j['prints']
        self.lamps = {k: sp.illuminant(v) for k, v in self.j['lamps'].items()}
        self.scanner_sens = np.stack([sp.lobes(l) for l in self.j['scanner']['sensitivity']])
        up = np.load(os.path.join(sp.DATA, 'upsample_oct.npz'))
        self.up_coef = up['coef']
        self.up_a = up['a']
        self.N_OCT = int(up['N'])

    def neg_index(self, name):
        return [n['name'] for n in self.negs].index(name)

    def print_index(self, name):
        return [p['name'] for p in self.prints].index(name)


def neg_kind(neg):
    return neg['kind']  # 'negative' | 'reversal' | 'bw'


def neg_sensitivities(neg):
    if neg['kind'] == 'bw':
        s = sp.lobes(neg['sensitivity'][0])
        return np.stack([s, s, s])
    return np.stack([sp.lobes(l) for l in neg['sensitivity']])


def dye_spectra(dyes):
    """dyes: list of 3 dicts {primary: lobes, secondary: lobes} -> (eps_total (3,81), secondary (3,81))."""
    tot, sec = [], []
    for d in dyes:
        p = sp.lobes(d['primary'])
        s = sp.lobes(d.get('secondary', []))
        tot.append(p + s)
        sec.append(s)
    return np.stack(tot), np.stack(sec)


def base_density(b):
    return b['d0'] + b.get('blue', 0.0) * np.exp(-0.5 * ((sp.WL - 400.0) / 35.0) ** 2) \
        + b.get('red', 0.0) * np.exp(-0.5 * ((sp.WL - 700.0) / 60.0) ** 2)


def neg_dyes(neg):
    if neg['kind'] == 'bw':
        sil = sp.lobes(neg['silverSpectrum']) if 'silverSpectrum' in neg else np.ones(sp.NWL)
        e = np.stack([sil, sil, sil]) / 3.0
        return e, np.zeros_like(e)
    return dye_spectra(neg['dyes'])


def print_mono_variant(prt):
    """Black & white negatives print to a neutral silver-like image on the chosen print stock:
    identical layers (sum of the print sensitivities), green-layer curve, flat neutral dye."""
    q = dict(prt)
    q['_mono'] = True
    return q


def print_sens(prt):
    s = np.stack([sp.lobes(l) for l in prt['sensitivity']])
    if prt.get('_mono'):
        t = s.sum(0)
        s = np.stack([t, t, t])
    return s


def print_dyes(prt):
    if prt.get('_mono'):
        e = np.ones((3, sp.NWL)) / 3.0
        return e
    tot, _ = dye_spectra(prt['dyes'])
    return tot


def print_fade(prt):
    """Faded archive: dye loss per layer AFTER printing (applied on top of the END normalisation,
    so the lab balanced the print with fresh dyes and the cast appears in the shadows)."""
    if prt.get('_mono'):
        return np.ones(3)
    return np.array(prt.get('fade', [1.0, 1.0, 1.0]), float)


def print_curve_params(prt):
    c = prt['curves']
    if prt.get('_mono'):
        return [c[1], c[1], c[1]]
    return c


# =========================================================================== parametric curves
def softplus(z):
    return np.where(z > 30.0, z, np.log1p(np.exp(np.minimum(z, 30.0))))


def dev(y, gamma, k, R, p):
    r = gamma * softplus(k * y) / k
    return r / (1.0 + (r / R) ** p) ** (1.0 / p)


def push_params(c, push, reversal=False):
    c = dict(c)
    g = 1.0 + (0.12 if push > 0 else 0.10) * push
    c['gamma'] = c['gamma'] * g
    if reversal:
        c['x0'] = c['x0'] + 0.10 * push     # shoulder/toe move toward more exposure = faster
        c['R'] = c['R'] * (1.0 - 0.03 * push)
        c['fog'] = c['fog'] + 0.01 * push
    else:
        c['x0'] = c['x0'] - 0.10 * push
        c['fog'] = c['fog'] + (0.035 if push > 0 else 0.01) * push
    return c


def neg_curve_eval(c, x, reversal=False):
    if reversal:
        return c['fog'] + dev(c['x0'] - x, c['gamma'], c['k'], c['R'], c['p'])
    return c['fog'] + dev(x - c['x0'], c['gamma'], c['k'], c['R'], c['p'])


def build_neg_curves(neg, push, neutralize):
    rev = neg['kind'] == 'reversal'
    cs = neg['curves'] if neg['kind'] != 'bw' else [neg['curves'][0]] * 3
    cs = [push_params(c, push, rev) for c in cs]
    x = np.linspace(NEG_X0, NEG_X1, N_CURVE)
    d = np.stack([neg_curve_eval(c, x, rev) for c in cs])
    if neutralize > 0:
        fog = np.array([c['fog'] for c in cs])[:, None]
        dg = np.stack([neg_curve_eval(c, np.array([LOG10_018]), rev) for c in cs])
        if rev:   # normalise around grey toward a common shape
            dmx = fog + np.array([c['R'] for c in cs])[:, None]
            cn = (d - fog) / (dg - fog)
        else:
            cn = (d - fog) / (dg - fog)
        cm = cn.mean(0, keepdims=True)
        d = fog + (dg - fog) * ((1 - neutralize) * cn + neutralize * cm)
    gam = np.array([c['gamma'] for c in cs])
    return d, gam


def build_print_curves(prt):
    cs = print_curve_params(prt)
    u = np.linspace(PRT_U0, PRT_U1, N_CURVE)
    return np.stack([c['fog'] + dev(u - c['x0'], c['gamma'], c['k'], c['R'], c['p']) for c in cs])


# =========================================================================== LUT helpers (== kernel)
def f32(a):
    return np.asarray(a, dtype=np.float32).astype(np.float64)


def lut1d(table, x0, x1, x):
    """table (N,) over [x0,x1], corner aligned, clamp."""
    N = table.shape[-1]
    t = np.clip((x - x0) / (x1 - x0) * (N - 1), 0.0, N - 1)
    i = np.minimum(np.floor(t).astype(int), N - 2)
    f = t - i
    return table[..., i] * (1 - f) + table[..., i + 1] * f if table.ndim == 1 else None


def interp_uniform(vals, x0, x1, x):
    """linear interpolation of samples on a uniform grid over [x0,x1], clamped (== kernel lut1)."""
    return lut1d(vals, x0, x1, np.asarray(x, dtype=np.float64))


def invert_monotone(xs, D, t):
    """x(t) for a non-decreasing sampled function D(xs): first segment with D[k] >= t."""
    t = np.atleast_1d(t)
    k = np.searchsorted(D, t, side='left')
    k = np.clip(k, 1, len(D) - 1)
    d0, d1 = D[k - 1], D[k]
    f = np.where(d1 > d0, (t - d0) / np.where(d1 > d0, d1 - d0, 1.0), 0.0)
    f = np.clip(f, 0.0, 1.0)
    return xs[k - 1] + f * (xs[k] - xs[k - 1])


def lut1d_rows(tables, x0, x1, X):
    """tables (3,N), X (n,3) -> (n,3)"""
    out = np.empty_like(X)
    for c in range(3):
        out[:, c] = lut1d(tables[c], x0, x1, X[:, c])
    return out


def lut2d(tab, P, Q):
    """tab (N,N,C) indexed [row=v][col=u]; P=u, Q=v in [-1,1]."""
    N = tab.shape[0]
    s = np.clip((P + 1.0) * 0.5 * (N - 1), 0, N - 1)
    t = np.clip((Q + 1.0) * 0.5 * (N - 1), 0, N - 1)
    i = np.minimum(np.floor(s).astype(int), N - 2)
    j = np.minimum(np.floor(t).astype(int), N - 2)
    fs = (s - i)[:, None]
    ft = (t - j)[:, None]
    a = tab[j, i] * (1 - fs) + tab[j, i + 1] * fs
    b = tab[j + 1, i] * (1 - fs) + tab[j + 1, i + 1] * fs
    return a * (1 - ft) + b * ft


def lut3d(tab, lo, hi, X):
    """tab (N,N,N,3) indexed [x0][x1][x2]; X (n,3); per-axis domain lo..hi (scalars)."""
    N = tab.shape[0]
    t = np.clip((X - lo) / (hi - lo) * (N - 1), 0, N - 1)
    i = np.minimum(np.floor(t).astype(int), N - 2)
    f = t - i
    out = 0
    for dx in (0, 1):
        wx = f[:, 0] if dx else 1 - f[:, 0]
        for dy in (0, 1):
            wy = f[:, 1] if dy else 1 - f[:, 1]
            for dz in (0, 1):
                wz = f[:, 2] if dz else 1 - f[:, 2]
                out = out + (wx * wy * wz)[:, None] * tab[i[:, 0] + dx, i[:, 1] + dy, i[:, 2] + dz]
    return out


# =========================================================================== bakes
def balanced_weights(sens, illum):
    w = sens * illum[None, :]
    return w / w.sum(1, keepdims=True)


def bake_exposure_lut(lib, neg):
    """K_i(u) for every octahedral texel: a_u * sum_l R_u(l) * w_i(l)."""
    sens = neg_sensitivities(neg)
    w = balanced_weights(sens, sp.illuminant(neg['balance']))
    c = lib.up_coef.reshape(-1, 3)
    z = c[:, 0:1] + c[:, 1:2] * sp.TNORM + c[:, 2:3] * sp.TNORM ** 2
    R = 0.5 + 0.5 * np.tanh(0.5 * z)
    K = (R @ w.T) * lib.up_a.reshape(-1, 1)
    N = lib.N_OCT
    return K.reshape(N, N, 3)


def dmax_neg(neg):
    cs = neg['curves'] if neg['kind'] != 'bw' else [neg['curves'][0]]
    return max(c['fog'] + c['R'] for c in cs) * 1.15 + 0.35


def visual_end_scale(eps, base, lamp, target=1.0):
    """Scale factors s_j so that equal dye amounts (s_j * eps_j, amount = target) give a neutral
    of visual density `target` relative to the clear film under `lamp` (equivalent neutral density
    units). Newton on log10 XYZ with the analytic Jacobian."""
    w = sp.CMF.T * (lamp * 10.0 ** (-base))[None, :]          # (3,L)
    ref = np.log10(w.sum(1)) - target
    s = np.ones(3)
    for _ in range(50):
        T = 10.0 ** (-(s[:, None] * eps).sum(0) * target)
        X = w @ T
        r = np.log10(X) - ref
        if np.abs(r).max() < 1e-13:
            break
        J = -(w[:, None, :] * eps[None, :, :] * T[None, None, :]).sum(2) * target / X[:, None]
        s = s - np.linalg.solve(J, r)
    return s


def printing_end_scale(lib, eps, sec, masks, dsat, base, target=1.0):
    """Scale factors for negative dyes so that equal amounts give equal *printing density*
    (reference print sensitivities = P1 under the printer lamp), masks included."""
    S = np.stack([sp.lobes(l) for l in lib.prints[0]['sensitivity']])
    w = S * lib.lamps['printer'][None, :] * 10.0 ** (-base)[None, :]
    def logH(sv, d):
        dens = (d * sv)[:, None] * eps + (masks * np.maximum(dsat - d, 0.0) * sv)[:, None] * sec
        T = 10.0 ** (-dens.sum(0))
        return np.log10(w @ T)
    s = np.ones(3)
    zero = logH(s, np.zeros(3))      # independent of s? (mask at d=0 scales with s) -> recompute
    for _ in range(60):
        r = logH(s, np.full(3, target)) - logH(s, np.zeros(3)) + target
        if np.abs(r).max() < 1e-12:
            break
        J = np.zeros((3, 3))
        h = 1e-6
        for k in range(3):
            e = s.copy()
            e[k] += h
            J[:, k] = ((logH(e, np.full(3, target)) - logH(e, np.zeros(3)) + target) - r) / h
        s = s - np.linalg.solve(J, r)
    return s


def neg_dyes_scaled(lib, neg):
    eps, sec = neg_dyes(neg)
    if neg['kind'] == 'bw':
        return eps, sec
    if neg['kind'] == 'reversal':
        base = base_density(neg['base'])
        s = visual_end_scale(eps, base, lib.lamps['projector'])
        return eps * s[:, None], sec * s[:, None]
    masks = np.array(neg.get('mask', [0, 0, 0]), dtype=float)
    dsat = np.array([c['fog'] + c['R'] for c in neg['curves']])
    base = base_density(neg['base'])
    s = printing_end_scale(lib, eps, sec, masks, dsat, base)
    return eps * s[:, None], sec * s[:, None]


def neg_transmittance_factors(lib, neg, grid):
    """Per-layer spectral transmittance factors F_i(d, lambda) incl. coloured masking couplers."""
    eps, sec = neg_dyes_scaled(lib, neg)
    masks = np.array(neg.get('mask', [0, 0, 0]), dtype=float)
    cs = neg['curves'] if neg['kind'] != 'bw' else [neg['curves'][0]] * 3
    dsat = np.array([c['fog'] + c['R'] for c in cs])
    F = []
    for i in range(3):
        md = masks[i] * np.maximum(dsat[i] - grid, 0.0)           # remaining coloured coupler
        dens = grid[:, None] * eps[i][None, :] + md[:, None] * sec[i][None, :]
        F.append(10.0 ** (-dens))
    return F


def bake_dye_lut(Fs, base, weights):
    """Generic separable 3D bake: out[i,j,k,c] = log10( sum_l base(l) F0[i,l] F1[j,l] F2[k,l] w_c(l) )."""
    F0, F1, F2 = Fs
    N = F0.shape[0]
    tb = 10.0 ** (-base)
    out = np.empty((N, N, N, 3))
    for i in range(N):
        B = (F0[i] * tb)[None, :] * F1                      # (N,L)
        for c in range(3):
            # sum over l of B[j,l] * F2[k,l] * w_c[l]
            out[i, :, :, c] = (B * weights[c][None, :]) @ F2.T
    return np.log10(np.maximum(out, 1e-30))


def bake_print_lut(lib, neg, prt):
    N = N_LUT3
    dmax = dmax_neg(neg)
    g = np.linspace(0.0, dmax, N)
    Fs = neg_transmittance_factors(lib, neg, g)
    base = base_density(neg['base']) if 'base' in neg else np.zeros(sp.NWL)
    L = lib.lamps['printer']
    S = print_sens(prt)
    w = S * L[None, :]
    w = w / w.sum(1, keepdims=True)            # clear (T=1) film -> H = 1
    return bake_dye_lut(Fs, base, w), dmax


def dmax_print(prt):
    return max(c['fog'] + c['R'] for c in prt['curves']) * 1.05 + 0.3


def projection_white(lib, base, lamp):
    X = sp.XYZ_of(lamp * 10.0 ** (-base))
    return X


def bake_projection_lut(lib, eps, base, dmax, lamp, fog):
    """log10 XYZ of dye densities under the projector lamp; Y normalised so that the clear
    (unexposed, processed) film -- support base + dye fog -- is display white 1.0."""
    N = N_LUT3
    g = np.linspace(0.0, dmax, N)
    Fs = [10.0 ** (-(g[:, None] * eps[i][None, :])) for i in range(3)]
    dclear = base + fog @ eps
    Yb = sp.XYZ_of(lamp * 10.0 ** (-dclear))[1]
    w = (sp.CMF.T * lamp[None, :]) / Yb
    return bake_dye_lut(Fs, base, w)


def cat_to_d65(lamp):
    """CAT16 from the projector lamp's white (perfectly clear base) to D65 (both Y = 1)."""
    Xw = sp.XYZ_of(lamp)
    Xw = Xw / Xw[1]
    return sp.cat_matrix(Xw, sp.xy_to_XYZ(sp.WHITE_D65), sp.CAT16)


def bake_scan_lut(lib, neg):
    N = N_LUT3
    dmax = dmax_neg(neg)
    g = np.linspace(0.0, dmax, N)
    Fs = neg_transmittance_factors(lib, neg, g)
    base = base_density(neg['base']) if 'base' in neg else np.zeros(sp.NWL)
    L = lib.lamps['scanner']
    w = lib.scanner_sens * L[None, :]
    w = w / w.sum(1, keepdims=True)
    return bake_dye_lut(Fs, base, w), dmax


# =========================================================================== pipeline
class Params:
    def __init__(self, **kw):
        self.input = 'FLog2C'
        self.output = 'Rec709_G24'
        self.exposure = 0.0
        self.neg = 0
        self.push = 0.0
        self.interlayer = 1.0
        self.negBleach = 0.0
        self.neutralize = 0.0
        self.process = 'print'      # 'print' | 'scan'
        self.prt = 0
        self.printer = (0.0, 0.0, 0.0)
        self.printExposure = 0.0
        self.printBleach = 0.0
        self.hdrPeak = 1000.0
        self.hdrUnroll = 1.0
        self.grain = 0.0
        for k, v in kw.items():
            if not hasattr(self, k):
                raise KeyError(k)
            setattr(self, k, v)


def smooth_floor(e, d):
    return 0.5 * (e + np.sqrt(e * e + d * d))


class Pipeline:
    """prepare() bakes everything (float32-rounded tables like the C++ state buffer)."""

    def __init__(self, lib):
        self.lib = lib

    def prepare(self, p):
        lib = self.lib
        self.p = p
        neg = lib.negs[p.neg]
        self.negd = neg
        kind = neg['kind']
        self.kind = 'reversal' if kind == 'reversal' else ('scan' if p.process == 'scan' else 'print')
        self.mono = kind == 'bw'
        prt = lib.prints[p.prt]
        if self.mono:
            prt = print_mono_variant(prt)
        self.prtd = prt
        # input
        self.Min = f32(input_matrix(p.input))
        self.gain = 2.0 ** p.exposure
        # exposure LUT
        self.K = f32(bake_exposure_lut(lib, neg))
        # negative / reversal curves
        nc, gam = build_neg_curves(neg, p.push, p.neutralize)
        self.ncurve = f32(nc)
        self.ngamma = gam
        self.dmin = np.minimum(self.ncurve[:, 0], self.ncurve[:, -1]).copy()   # D-min (clear end)
        self.inter = np.array(neg.get('interlayer', np.zeros((3, 3)).tolist()), dtype=float) * p.interlayer
        np.fill_diagonal(self.inter, 0.0)
        self.inter = f32(self.inter)
        pushf = 1.25 ** p.push
        gr = np.array(neg['granularity'] if not self.mono else [neg['granularity'][0]] * 3)
        self.gsig = f32(gr * p.grain * pushf)
        self.nb = f32(p.negBleach * neg.get('silver', 0.9))
        self.txg = f32(2.5 * self.gsig.mean())
        lamp_proj = lib.lamps['projector']
        if self.kind == 'print':
            lut, dmax = bake_print_lut(lib, neg, prt)
            self.plut = f32(lut)
            self.plut_max = dmax
            self.pcurve = f32(build_print_curves(prt))
            self.pmin = self.pcurve[:, 0].copy()
            eps = print_dyes(prt)
            base = base_density(prt['base'])
            if not self.mono:
                eps = eps * visual_end_scale(eps, base, lamp_proj)[:, None]
            eps = eps * print_fade(prt)[:, None]
            self.pdmax = dmax_print(prt)
            self.jlut = f32(bake_projection_lut(lib, eps, base, self.pdmax, lamp_proj, self.pmin))
            self.Mcat = cat_to_d65(lamp_proj)
            s_stock = prt.get('silver', 0.0)
            self.pb = f32((1.0 - (1.0 - p.printBleach) * (1.0 - s_stock)) * prt.get('silverDensity', 0.9))
            self.off = np.zeros(3)
        elif self.kind == 'reversal':
            eps, _ = neg_dyes_scaled(lib, neg)
            base = base_density(neg['base'])
            self.pdmax = dmax_neg(neg)
            fog_nom = np.array([neg_curve_eval(c, np.array(NEG_X1), True) for c in neg['curves']])  # nominal D-min
            self.jlut = f32(bake_projection_lut(lib, eps, base, self.pdmax, lamp_proj, fog_nom))
            self.Mcat = cat_to_d65(lamp_proj)
            self.off = np.zeros(3)
        else:  # scan
            lut, dmax = bake_scan_lut(lib, neg)
            self.slut = f32(lut)
            self.slut_max = dmax
            self.scan_prepare(neg)
        self.out = p.output
        self.Mout = output_matrix(p.output)
        self.hdr = p.output in ('Rec2100_PQ', 'Rec2100_HLG')
        self.peak = p.hdrPeak if p.output == 'Rec2100_PQ' else 1000.0
        # grey balance (points 0, exposure 0), then user offsets
        self.solve_balance()
        self.calibrate_hdr()
        if self.kind == 'print':
            pts = np.array(p.printer) - p.printExposure
            self.off_user = self.off + POINT * pts
        elif self.kind == 'scan':
            pts = np.array(p.printer) - p.printExposure
            self.off_user = -0.075 / self.scan_gamma * pts
        else:
            self.off_user = self.off.copy()
        return self

    # ------------------------------------------------------------------ scan calibration
    def scan_prepare(self, neg):
        lib = self.lib
        sc = lib.j['scanner']
        # Neutral-referenced balanced inversion (what a lab scanner's per-density grey balance does):
        # each channel's scanner density is mapped through the neutral ramp onto the G channel's
        # density scale, then inverted with the G channel's grey gamma. Neutrals stay neutral at every
        # exposure; the negative's toe / shoulder shape (latitude) is kept.
        K = SCN_NFWD
        xs = SCN_XA + (SCN_XB - SCN_XA) * np.arange(K) / (K - 1)
        d = lut1d_rows(self.ncurve, NEG_X0, NEG_X1, np.repeat(xs[:, None], 3, 1))
        D = -lut3d(self.slut, 0.0, self.slut_max, d)            # (K,3) forward scanner densities
        D = np.maximum.accumulate(D, axis=0)                      # enforce monotone (float flat tails)
        gx = LOG10_018
        g_grey = interp_uniform(D[:, 1], SCN_XA, SCN_XB, gx)
        h = 0.15
        gam = (interp_uniform(D[:, 1], SCN_XA, SCN_XB, gx + h) - interp_uniform(D[:, 1], SCN_XA, SCN_XB, gx - h)) / (2 * h)
        self.sd0 = f32(D[0].copy())
        self.sd1 = f32(D[-1].copy())
        tab = np.zeros((3, N_CURVE))
        for j in range(3):
            t = self.sd0[j] + (self.sd1[j] - self.sd0[j]) * np.arange(N_CURVE) / (N_CURVE - 1)
            xj = invert_monotone(xs, D[:, j], t)
            tab[j] = (interp_uniform(D[:, 1], SCN_XA, SCN_XB, xj) - g_grey) / gam
        self.sinv = f32(tab)
        self.Mscan = np.eye(3) if self.mono else np.array(neg.get('scanMatrix', np.eye(3).tolist()))
        cv = sc['curve']
        self.scan_curve_c = cv
        self.scan_gamma = cv['gamma']
        # solve ws so that w=0 -> grey target
        tgt = -math.log10(sc['greyY'])
        lo, hi = -5.0, 5.0
        for _ in range(80):
            mid = 0.5 * (lo + hi)
            v = float(dev(np.array(mid), cv['gamma'], cv['k'], cv['R'], cv['p']))
            if v < tgt:
                lo = mid
            else:
                hi = mid
        self.scan_ws = 0.5 * (lo + hi)
        w = np.linspace(SCN_W0, SCN_W1, N_CURVE)
        self.scurve = f32(dev(self.scan_ws - w, cv['gamma'], cv['k'], cv['R'], cv['p']))

    # ------------------------------------------------------------------ per-pixel model
    def decode(self, enc):
        enc = np.clip(np.where(np.isfinite(enc), enc, 0.0), -65504.0, 65504.0)
        with np.errstate(over='ignore', invalid='ignore'):
            lin = decode_transfer(self.p.input, enc)
            out = (lin @ self.Min.T) * self.gain
        return np.clip(np.where(np.isfinite(out), out, 0.0), -65504.0, 65504.0)

    def exposures(self, rgb):
        u, v, L1 = sp.oct_encode(rgb)
        K = lut2d(self.K, u, v)
        E = L1[:, None] * K
        return np.log10(np.maximum(E, 1e-10))

    def interlayer(self, x):
        xref = x @ LAYER_W
        cur = lambda X: lut1d_rows(self.ncurve, NEG_X0, NEG_X1, X)
        ga = self.ngamma[None, :]
        sgn = -1.0 if self.kind == 'reversal' else 1.0
        c = sgn * (cur(x) - cur(np.repeat(xref[:, None], 3, 1))) / ga
        delta = -(c @ self.inter.T)
        delta = delta - (delta @ LAYER_W)[:, None]
        return x + delta, xref

    def neg_densities(self, rgb, noise):
        x = self.exposures(rgb)
        x, xref = self.interlayer(x)
        if self.kind == 'reversal':
            x = x + self.off_user[None, :]
        d = lut1d_rows(self.ncurve, NEG_X0, NEG_X1, x)
        if noise is not None and self.gsig.max() > 0:
            nz = np.repeat(noise[:, 1:2], 3, 1) if self.mono else noise
            d = d + self.gsig[None, :] * np.sqrt(np.maximum(d - self.dmin[None, :], 0.0) + GRAIN_EPS) * nz
        sil = self.nb * (np.maximum(d - self.dmin[None, :], 0.0) @ LAYER_W)
        return d, sil, xref

    def film_xyz(self, rgb, noise=None, off=None):
        """scene-linear Rec.2020 -> display XYZ (D65 adapted, 1 = white) + HDR driver."""
        d, sil, xref = self.neg_densities(rgb, noise)
        if self.kind == 'print':
            o = self.off_user if off is None else off
            h = lut3d(self.plut, 0.0, self.plut_max, d) + o[None, :] - sil[:, None]
            P = lut1d_rows(self.pcurve, PRT_U0, PRT_U1, h)
            ps = self.pb * (np.maximum(P - self.pmin[None, :], 0.0) @ LAYER_W)
            lx = lut3d(self.jlut, 0.0, self.pdmax, P)
            XYZ = 10.0 ** (lx - ps[:, None])
            XYZ = XYZ @ self.Mcat.T
        elif self.kind == 'reversal':
            lx = lut3d(self.jlut, 0.0, self.pdmax, d)
            XYZ = 10.0 ** (lx - sil[:, None])
            XYZ = XYZ @ self.Mcat.T
        else:
            ds = -lut3d(self.slut, 0.0, self.slut_max, d) + sil[:, None]
            o = self.off_user if off is None else off
            v = np.stack([lut1d(self.sinv[j], self.sd0[j], self.sd1[j], ds[:, j]) for j in range(3)], 1) + o[None, :]
            e = (10.0 ** v) @ self.Mscan.T
            e = smooth_floor(e, 1e-3)
            w = np.log10(e)
            Dd = lut1d_rows(np.stack([self.scurve] * 3), SCN_W0, SCN_W1, w)
            rgb2020 = 10.0 ** (-Dd)
            XYZ = rgb2020 @ sp.M2020_TO_XYZ.T
        return XYZ, xref

    # ------------------------------------------------------------------ balance & HDR calibration
    def solve_balance(self):
        if self.kind == 'scan':
            self.off = np.zeros(3)
            return
        neg = self.negd
        tY = (self.prtd if self.kind == 'print' else neg)['greyY']
        grey = np.full((1, 3), 0.18)
        target = np.log10(sp.xy_to_XYZ(sp.WHITE_D65, tY))
        if self.mono and self.kind == 'print':
            # neutral silver image: tint = base tint; only the density is solved
            pass
        self.off_user = np.zeros(3)
        off = self.balance_initial_guess(tY)
        self.off_user = off
        for it in range(40):
            def f(o):
                self.off_user = o
                X, _ = self.film_xyz(grey, None, o)
                return np.log10(np.maximum(X[0], 1e-12))
            y0 = f(off)
            hstep = 1e-3
            if self.mono:
                r = y0[1] - target[1]
                if abs(r) < 1e-10:
                    break
                dy = (f(off + hstep)[1] - y0[1]) / hstep
                step = -r / dy if abs(dy) > 1e-9 else 0.0
                off = off + np.clip(step, -0.5, 0.5)
                continue
            r = y0 - target
            if np.abs(r).max() < 1e-10:
                break
            J = np.zeros((3, 3))
            for k in range(3):
                e = np.zeros(3)
                e[k] = hstep
                J[:, k] = (f(off + e) - y0) / hstep
            step = np.linalg.solve(J, -r)
            off = off + np.clip(step, -0.5, 0.5)
        self.off = off
        self.off_user = off

    def balance_initial_guess(self, tY):
        """Per-layer bisection: put the grey's density of each layer at the visual target."""
        grey = np.full((1, 3), 0.18)
        d, sil, _ = self.neg_densities(grey, None)
        dt = -math.log10(tY)
        off = np.zeros(3)
        if self.kind == 'print':
            h = lut3d(self.plut, 0.0, self.plut_max, d)[0] - sil[0]
            for j in range(3):
                lo, hi = PRT_U0, PRT_U1
                for _ in range(60):
                    mid = 0.5 * (lo + hi)
                    if lut1d(self.pcurve[j], PRT_U0, PRT_U1, np.array(mid)) - self.pmin[j] < dt:
                        lo = mid
                    else:
                        hi = mid
                off[j] = 0.5 * (lo + hi) - h[j]
        else:  # reversal: offsets in log exposure
            x = np.full(3, LOG10_018)
            for j in range(3):
                lo, hi = -4.0, 4.0
                for _ in range(60):
                    mid = 0.5 * (lo + hi)
                    D = lut1d(self.ncurve[j], NEG_X0, NEG_X1, np.array(x[j] + mid)) - self.dmin[j]
                    if D > dt:
                        lo = mid
                    else:
                        hi = mid
                off[j] = 0.5 * (lo + hi)
        return off

    def calibrate_hdr(self):
        grey = np.full((1, 3), 0.18)
        X, _ = self.film_xyz(grey, None, self.off)
        self.Ygrey = X[0, 1]
        Xw, _ = self.film_xyz(np.full((1, 3), 0.18 * 2.0 ** 14), None, self.off)
        self.Ywhite = Xw[0, 1]
        self.Lref = HDR_GREY_NITS / self.Ygrey
        self.Gmax = max(0.0, math.log10(self.peak / (self.Lref * self.Ywhite)))

    # ------------------------------------------------------------------ output stage
    def gamut_compress(self, rgb, lum):
        Y = rgb @ lum
        mn = rgb.min(1)
        Ys = np.maximum(Y, 1e-10)
        s = np.where(Y > 1e-10, (Y - mn) / Ys, 0.0)
        x = np.maximum(s - GC_THR, 0.0) / (1.0 - GC_THR)
        sc = np.where(s > GC_THR, GC_THR + (1.0 - GC_THR) * x / (1.0 + x ** GC_P) ** (1.0 / GC_P), s)
        k = np.where(s > 1e-12, sc / np.maximum(s, 1e-12), 1.0)
        out = Y[:, None] + (rgb - Y[:, None]) * k[:, None]
        return np.where(Y[:, None] > 1e-10, out, 0.0)

    def limit(self, rgb, knee, top):
        x = np.maximum(rgb - knee, 0.0) / (top - knee)
        return np.where(rgb > knee, knee + (top - knee) * np.tanh(x), rgb)

    def output(self, XYZ, xref):
        lum = self.Mout_lum()
        if not self.hdr:
            rgb = XYZ @ self.Mout.T
            rgb = self.gamut_compress(rgb, lum)
            rgb = self.limit(rgb, SDR_KNEE, 1.0)
            return self.encode_sdr(rgb)
        e = xref - LOG10_018
        t = np.maximum(e - HDR_E0, 0.0) / HDR_ER
        G = self.Gmax * t * t / (1.0 + t * t)
        nits = XYZ * (self.Lref * 10.0 ** (self.p.hdrUnroll * G))[:, None]
        rgb = nits @ self.Mout.T
        rgb = self.gamut_compress(rgb, lum)
        rgb = self.limit(rgb, HDR_KNEE * self.peak, self.peak)
        if self.out == 'Rec2100_PQ':
            return pq_encode(rgb)
        return self.hlg_encode(rgb)

    def Mout_lum(self):
        return np.linalg.inv(self.Mout)[1]

    def encode_sdr(self, rgb):
        o = self.out
        if o == 'Rec709_G24':
            return gamma_enc(rgb, 2.4)
        if o == 'Rec709_sRGB':
            return srgb_enc(rgb)
        return gamma_enc(rgb, 2.6)

    def hlg_encode(self, nits):
        # BT.2100 HLG inverse EOTF with nominal peak Lw = 1000 cd/m2, system gamma 1.2, black 0
        Fd = np.maximum(nits, 0.0) / 1000.0
        Yd = Fd @ sp.LUM2020
        Ys = np.maximum(Yd, 1e-12) ** (1.0 / 1.2)
        E = Fd / np.maximum(Ys ** 0.2, 1e-12)[:, None]   # E = Fd / (alpha * Ys^(gamma-1)), alpha = 1
        E = np.where(Yd[:, None] > 1e-12, E, 0.0)
        return hlg_oetf(E)

    # ------------------------------------------------------------------ full pixel
    def process(self, enc, noise=None):
        enc = np.atleast_2d(np.asarray(enc, dtype=np.float64))
        rgb = self.decode(enc)
        return self.scene_to_output(rgb, noise)

    def scene_to_output(self, rgb, noise=None):
        rgb = np.atleast_2d(np.asarray(rgb, dtype=np.float64))
        rgb = np.where(np.isfinite(rgb), rgb, 0.0)
        rgb = np.clip(rgb, -65504.0, 65504.0)
        if noise is not None:
            noise = np.clip(np.where(np.isfinite(noise), noise, 0.0), -8.0, 8.0)
        XYZ, xref = self.film_xyz(rgb, noise)
        out = self.output(XYZ, xref)
        return np.where(np.isfinite(out), out, 0.0)

    # ------------------------------------------------------------------ Texture-Only
    def display_decode(self, v):
        v = np.clip(np.where(np.isfinite(v), v, 0.0), -65504.0, 65504.0)
        o = self.out
        if o == 'Rec709_sRGB':
            return srgb_dec(v)
        if o == 'Rec709_G24':
            return np.sign(v) * np.abs(v) ** 2.4
        if o == 'Rec2100_PQ':
            return pq_decode(v) * 0.01
        if o == 'Rec2100_HLG':
            E = hlg_oetf_inv(np.clip(v, 0.0, 1.5))
            Ys = E @ self.Mout_lum()
            k = np.where(Ys > 0, 1000.0 * np.maximum(Ys, 1e-30) ** 0.2, 0.0)
            return E * (k * 0.01)[:, None]
        return np.sign(v) * np.abs(v) ** 2.6

    def display_finish(self, lin, noise=None):
        lin = np.clip(np.where(np.isfinite(lin), lin, 0.0), -65504.0, 65504.0)
        if noise is not None and self.txg > 0:
            nz = np.clip(np.where(np.isfinite(noise), noise, 0.0), -8.0, 8.0)
            if self.mono:
                nz = np.repeat(nz[:, 1:2], 3, 1)
            Y = lin @ self.Mout_lum()
            l = np.log10(np.maximum(Y, 1e-6)) + 0.95
            sig = self.txg * np.exp(-0.5 * l * l / 0.49)
            if not self.hdr:
                t = np.clip((Y - 0.6) / 0.4, 0.0, 1.0)
                sig = sig * (1.0 - t * t * (3.0 - 2.0 * t))
                g = lin * 10.0 ** (-sig[:, None] * nz)
                lin = np.minimum(g, np.maximum(lin, 1.0))
            else:
                lin = lin * 10.0 ** (-sig[:, None] * nz)
        o = self.out
        if o == 'Rec2100_PQ':
            out = pq_encode(lin * 100.0)
        elif o == 'Rec2100_HLG':
            out = self.hlg_encode(lin * 100.0)
        else:
            out = self.encode_sdr(lin)
        return np.where(np.isfinite(out), out, 0.0)


def load(path='stocks.json'):
    return StockLib(path)
