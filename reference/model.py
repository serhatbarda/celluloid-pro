"""Reafilm Pro -- NumPy reference implementation of the spectral film model (float64).

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
N_LUT3 = 64                          # printer / scanner dye LUTs (projection: per-pixel spectral sum)
LAYER_W = np.array([0.30, 0.50, 0.20])   # luminance-like weights of the R,G,B-sensitive layers
GRAIN_EPS = 0.02
POINT = 0.025                        # log exposure per printer point
HDR_GREY_NITS = 15.0
HDR_E0, HDR_ER = 0.60, 0.60          # unroll starts 2 stops over grey, log10 scale of the roll
HDR_PEAK_STOPS = 10.0                # the unroll reaches the peak at +10 stops
GC_THR, GC_P = 0.80, 2.0             # gamut compression threshold / power (on saturation ratio)
SDR_KNEE = 0.95                      # SDR max-channel limiter knee (display linear)
HDR_KNEE = 0.90                      # HDR peak limiter knee (fraction of peak)
HOLD_N = 49                          # hue-hold anchor grid (octahedral directions)
HOLD_NX = 512                        # hue-hold neutral table over the reference log exposure
HOLD_X0, HOLD_X1 = -6.0, 4.0
HOLD_W = (0.3127 / 0.3290, 1.0, (1.0 - 0.3127 - 0.3290) / 0.3290)   # D65 white XYZ
HOLD_C0 = 0.20                      # chroma-hold roll-off (CIELAB C* / 200)
HOLD_K = 1.3                        # hold weight 1 - exp(-HOLD_K stops^2) away from grey
BLACK_ON = 1e-4                     # v1.3 monotone black: applied only when the neutral ramp drops by more than this (relative)

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
        # image tone of the black & white print: flat (neutral silver) plus the print's designed
        # 'monoTone' lobes (warm-toned papers / prints absorb a little more blue-green), unit mean
        t = 1.0 + sp.lobes(prt.get('monoTone', []))
        e = np.repeat((t / t.mean())[None, :], 3, 0) / 3.0
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


def print_aim_curve(lab, c, layer, devu):
    """v1.3 process aim / print contrast (== Bake.cpp printAimCurve): print development changed by devu units"""
    if devu == 0.0:
        return c
    w = lab['aimLayer'][layer] * devu
    c = dict(c)
    c['gamma'] = c['gamma'] * max(0.5, 1.0 + lab['aimGamma'] * w)
    c['R'] = c['R'] * max(0.5, 1.0 + lab['aimDmax'] * w)
    c['fog'] = c['fog'] + lab['aimFog'] * max(w, 0.0)
    return c


def telecine_curves(pc, black):
    """v1.3 telecine / scan of the print (== Bake.cpp telecineCurves): every layer balanced between D-min and
    D-max onto the green layer's, black set just above D-max (float32 table in, float32 out)"""
    lo1, hi1 = float(pc[1, 0]), float(pc[1, -1])
    Tb = 10.0 ** (-(hi1 - lo1))
    out = np.empty_like(pc)
    for j in range(3):
        t = pc[j].astype(np.float64)
        lo, hi = float(t[0]), float(t[-1])
        d1 = lo1 + (t - lo) * (hi1 - lo1) / (hi - lo) if hi - lo > 1e-9 else t
        T = 10.0 ** (-(d1 - lo1))
        out[j] = lo1 - np.log10((T - black * Tb) / (1.0 - black * Tb))
    return f32(out)


def build_print_curves(prt, lab=None, aim=0, contrast=0.0, render=0):
    cs = print_curve_params(prt)
    if lab is not None:
        devu = lab['aimStep'][aim] + float(np.float32(contrast))
        layers = [1, 1, 1] if prt.get('_mono') else [0, 1, 2]
        cs = [print_aim_curve(lab, c, l, devu) for c, l in zip(cs, layers)]
    u = np.linspace(PRT_U0, PRT_U1, N_CURVE)
    pc = np.stack([c['fog'] + dev(u - c['x0'], c['gamma'], c['k'], c['R'], c['p']) for c in cs])
    if lab is not None and render == 2:
        pc = telecine_curves(f32(pc), lab['telecineBlack'])
    return pc


def dupe_x0(c, aim):
    lo, hi = -8.0, 8.0
    for _ in range(80):
        mid = 0.5 * (lo + hi)
        if float(neg_curve_eval(c, np.array(mid))) < aim:
            lo = mid
        else:
            hi = mid
    return 0.5 * (lo + hi)


def dupe_log_h(lab, ch, logH, ref):
    """v1.3 release print (== Bake.cpp dupeLogH): negative -> master positive -> duplicate negative -> print,
    each generation on the VISION Color Intermediate 2242 curve with the grey on its LAD aim"""
    c = lab['dupe'][ch]
    xmp, xdn = dupe_x0(c, lab['mpAim'][ch]), dupe_x0(c, lab['dnAim'][ch])
    dmp = neg_curve_eval(c, xmp + (logH - ref))
    ddn = neg_curve_eval(c, xdn - (dmp - lab['mpAim'][ch]))
    return ref - (ddn - lab['dnAim'][ch])


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


LOGK_FLOOR = -10.0      # log10 exposure floor (== kernel)


def log_exposure_table(K, valid):
    """log10 K for the device table (interpolated in the log domain: the minority-layer K of a
    saturated direction is small and curves strongly across texels, linear interpolation of K was
    off by up to 0.2 stops). Directions without a spectrum (a_u = 0, non-physical) get no -inf:
    they are filled outward from the valid region, one decade darker per texel step (8-neighbour
    dilation, deterministic), then floored at LOGK_FLOOR (== src/core/Bake.cpp exposureLut)."""
    L = np.where(valid[..., None], np.log10(np.maximum(K, 10.0 ** LOGK_FLOOR)), -np.inf)
    filled = valid.copy()
    while not filled.all():
        P = np.pad(L, ((1, 1), (1, 1), (0, 0)), constant_values=-np.inf)
        F = np.pad(filled, 1, constant_values=False)
        best = np.full(L.shape, -np.inf)
        hit = np.zeros(filled.shape, bool)
        N0, N1 = filled.shape
        for dj in (-1, 0, 1):
            for di in (-1, 0, 1):
                if dj or di:
                    best = np.maximum(best, P[1 + dj:1 + dj + N0, 1 + di:1 + di + N1])
                    hit |= F[1 + dj:1 + dj + N0, 1 + di:1 + di + N1]
        grow = hit & ~filled
        if not grow.any():                    # nothing valid at all
            L[~filled] = LOGK_FLOOR
            break
        L = np.where(grow[..., None], np.maximum(best - 1.0, LOGK_FLOOR), L)
        filled = filled | grow
    return L


def exposures_from_log_table(LK, rgb):
    """layer log10 exposures of scene RGB: log10 L1 + bilinear log10 K (== kernel celc_exposures)"""
    u, v, L1 = sp.oct_encode(rgb)
    with np.errstate(divide='ignore'):
        l1 = np.log10(np.maximum(L1, 1e-30))
    return np.maximum(l1[:, None] + lut2d(LK, u, v), LOGK_FLOOR)


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


def bake_print_lut(lib, neg, prt, render=0):
    N = N_LUT3
    dmax = dmax_neg(neg)
    g = np.linspace(0.0, dmax, N)
    Fs = neg_transmittance_factors(lib, neg, g)
    base = base_density(neg['base']) if 'base' in neg else np.zeros(sp.NWL)
    L = lib.lamps['printer']
    S = print_sens(prt)
    w = S * L[None, :]
    w = w / w.sum(1, keepdims=True)            # clear (T=1) film -> H = 1
    lut = bake_dye_lut(Fs, base, w)
    if render == 1:
        # v1.3 release print: reference = the negative's 18 % grey at normal development (== Bake.cpp printLut)
        eps, sec = neg_dyes_scaled(lib, neg)
        masks = np.array(neg.get('mask', [0, 0, 0]), dtype=float) if neg['kind'] != 'bw' else np.zeros(3)
        cs = neg['curves'] if neg['kind'] != 'bw' else [neg['curves'][0]] * 3
        dsat = np.array([c['fog'] + c['R'] for c in cs])
        dg = np.array([float(neg_curve_eval(c, np.array(LOG10_018))) for c in cs])
        dens = base + dg @ eps + (masks * np.maximum(dsat - dg, 0.0)) @ sec
        ref = np.log10(w @ (10.0 ** (-dens)))
        lut = f32(lut)
        out = np.empty_like(lut)
        for j in range(3):
            out[..., j] = dupe_log_h(lib.j['lab'], 1 if prt.get('_mono') else j, lut[..., j], ref[j])
        lut = out
    return lut, dmax


def dmax_print(prt):
    return max(c['fog'] + c['R'] for c in prt['curves']) * 1.05 + 0.3


def projection_white(lib, base, lamp):
    X = sp.XYZ_of(lamp * 10.0 ** (-base))
    return X


def bake_projection_lut(lib, eps, base, dmax, lamp, fog):
    """Spectral projection table (== ColorState.h PJ block, K = 41 samples x 7): every other sample
    of the 5 nm grid (10 nm, trapezoid weights): base density, the three dye absorptions and
    CMF x lamp weights, Y normalised so that the clear (unexposed, processed) film -- support
    base + dye fog -- is display white 1.0 under the same quadrature. XYZ is integrated per pixel
    (project()), so there is no 3D-LUT interpolation on this stage."""
    sel = np.arange(0, sp.NWL, 2)
    q = np.full(len(sel), 2.0)
    q[0] = q[-1] = 1.0
    w = sp.CMF[sel] * (lamp[sel] * q)[:, None]               # (K,3)
    dclear = base[sel] + fog @ eps[:, sel]
    Yb = (10.0 ** (-dclear)) @ w[:, 1]
    return np.concatenate([base[sel, None], eps[:, sel].T, w / Yb], 1)     # (K,7)


def project(tab, dmax, D, nd):
    """== celc_project: XYZ = sum_k w(k) 10^-(base(k) + D . eps(k) + nd), D clamped to [0, dmax]."""
    D = np.clip(D, 0.0, dmax)
    dens = tab[None, :, 0] + nd[:, None] + D @ tab[:, 1:4].T  # (n,L)
    return (10.0 ** (-dens)) @ tab[:, 4:7]


def cat_to_d65(lamp):
    """CAT16 from the projector lamp's white (perfectly clear base) to D65 (both Y = 1)."""
    Xw = sp.XYZ_of(lamp)
    Xw = Xw / Xw[1]
    return sp.cat_matrix(Xw, sp.xy_to_XYZ(sp.WHITE_D65), sp.CAT16)


def projection_white_cat(lib, lamp, white):
    """v1.3 projection white (== Bake.cpp projectionWhite): (matrix, target white XYZ). 0 = CAT16 to D65 (v1.2),
    1 = unadapted lamp white, 2 = CAT16 to ACES D60; non-D65 whites are fitted to a D65 display (max Rec.709 = 1)"""
    if white == 0:
        return cat_to_d65(lamp), sp.xy_to_XYZ(sp.WHITE_D65)
    w = lib.j['lab']['whites'][white]
    Xw = sp.XYZ_of(lamp)
    Xw = Xw / Xw[1]
    Wt = sp.xy_to_XYZ((w['x'], w['y'])) if w['adapt'] else Xw
    M = sp.cat_matrix(Xw, Wt, sp.CAT16) if w['adapt'] else np.eye(3)
    rgb = np.linalg.inv(sp.npm(*sp.PRIMARIES['rec709'])) @ Wt
    sc = 1.0 / rgb.max() if rgb.max() > 1.0 else 1.0
    return M * sc, Wt * sc


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
        self.cameraEmulation = 0          # v1.2: 0 off, 1 Fujifilm X-Trans -> ARRI ALEXA
        self.emulationStrength = 1.0
        self.printCrossover = 0.0
        self.projectionWhite = 0          # v1.3 print lab: 0 D65, 1 Theatre Xenon, 2 D60 Sim
        self.printAim = 0                 # 0 nominal, 1 rich, 2 soft
        self.printRender = 0              # 0 contact print, 1 release print (IP/IN), 2 telecine
        self.printContrast = 0.0          # print development in aim units, -1 .. 1
        for k, v in kw.items():
            if not hasattr(self, k):
                raise KeyError(k)
            setattr(self, k, v)


def flush(a):
    """== kernel celc_sanitize: magnitudes below 1e-20 (denormals) -> 0"""
    return np.where(np.abs(a) < 1e-20, 0.0, a)


def smooth_floor(e, d):
    return 0.5 * (e + np.sqrt(e * e + d * d))


# v1.2 (== src/core/Bake.h / Core.cpp)
HOLD_ONSET = 1.0
PRINT_XOVER_GAMMA, PRINT_XOVER_DM = 0.08, 1.0


def print_crossover(pc, c):
    """== Core.cpp printCrossover: per-layer contrast g_j = 1 + c a_j k about Dm, a = (+1, 0, -1), with the
    layer's fog and D-max kept (endpoint-preserving rational pivot, see Core.cpp)"""
    a = np.array([1.0, 0.0, -1.0])
    out = pc.copy()
    for j in range(3):
        g = 1.0 + float(c) * a[j] * PRINT_XOVER_GAMMA
        if g == 1.0:
            continue
        D = pc[j].astype(np.float64)
        lo, hi, dm = float(D[0]), float(D[-1]), PRINT_XOVER_DM
        r = D.copy()
        for side, end in ((D < dm, lo), (D > dm, hi)):
            span = dm - end
            if abs(span) > 1e-9:
                x = (dm - D[side]) / span
                r[side] = dm - span * (g * x / (1.0 + (g - 1.0) * x))
        out[j] = f32(np.clip(r, min(lo, hi), max(lo, hi)))
    return f32(out)


def emulation_matrices(p):
    """== Core.cpp camera emulation state: (on, A, B) with the strength blended in"""
    import json as _json
    fuji = p.input in ('FLog2C', 'FLog2', 'FLog')
    on = p.cameraEmulation == 1 and fuji and p.emulationStrength > 0
    if not on:
        return False, np.eye(3), np.zeros((3, 3))
    e = _json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'cameras', 'emulation.json')))
    k = float(np.float32(p.emulationStrength))
    return True, f32(np.eye(3) + k * (np.array(e['A']) - np.eye(3))), f32(k * np.array(e['B']))


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
        self.emu_on, self.emuA, self.emuB = emulation_matrices(p)
        # exposure LUT
        self.LK = f32(log_exposure_table(bake_exposure_lut(lib, neg), lib.up_a.reshape(lib.N_OCT, lib.N_OCT) > 0))
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
            lut, dmax = bake_print_lut(lib, neg, prt, p.printRender)
            self.plut = f32(lut)
            self.plut_max = dmax
            self.pcurve = f32(build_print_curves(prt, lib.j['lab'], p.printAim, p.printContrast, p.printRender))
            if p.printCrossover != 0 and not self.mono:
                self.pcurve = print_crossover(self.pcurve, float(np.float32(p.printCrossover)))
            self.pmin = self.pcurve[:, 0].copy()
            eps = print_dyes(prt)
            base = base_density(prt['base'])
            if not self.mono:
                eps = eps * visual_end_scale(eps, base, lamp_proj)[:, None]
            eps = eps * print_fade(prt)[:, None]
            self.pdmax = dmax_print(prt)
            if p.printAim != 0 or p.printContrast != 0 or p.printRender == 2:   # rich aim / telecine black reach higher
                pc0 = f32(build_print_curves(prt, lib.j['lab'], p.printAim, p.printContrast, p.printRender))
                self.pdmax = max(self.pdmax, float(pc0.max()) + 0.05)
            self.jlut = f32(bake_projection_lut(lib, eps, base, self.pdmax, lamp_proj, self.pmin))
            self.Mcat, self.white = projection_white_cat(lib, lamp_proj, p.projectionWhite)
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
        self.black_d = None
        self.hold = 0.0 if self.mono else float(self.hold_strength())
        self.hold_chroma = 0.0 if self.mono else float(self.hold_strength('chromaHold'))
        if self.hold > 0:
            self.bake_hold_anchor()
        if self.kind == 'print':
            pts = np.array(p.printer) - p.printExposure
            self.off_user = self.off + POINT * pts
        elif self.kind == 'scan':
            pts = np.array(p.printer) - p.printExposure
            self.off_user = -0.075 / self.scan_gamma * pts
        else:
            self.off_user = self.off.copy()
        if self.hold > 0:
            self.bake_hold_neutral()
        return self

    # ------------------------------------------------------------------ hue hold
    # Every per-layer log-domain tone scale bends the path of a colour toward white (highlights)
    # or black (shadows): orange slides to yellow, red to pink, sky blue to cyan, deep skin to red.
    # The hue hold keeps a colour on the hue the stock gives it at mid-grey exposure: per scene
    # chromaticity the film rendering at 18 % grey luminance (points at 0) is the anchor; away
    # from grey the colour's chroma vector (relative to the film's own neutral at that exposure,
    # so crossover tints and printer lights stay) is turned toward the anchor's, magnitude and
    # luminance unchanged, by hold * (1 - exp(-(stops / 2)^2)).
    def hold_strength(self, key='hueHold'):
        if self.kind == 'print':
            return self.prtd.get(key, 0.0)
        if self.kind == 'reversal':
            return self.negd.get(key, 0.0)
        return self.lib.j['scanner'].get(key, 0.0)

    def bake_hold_anchor(self):
        N = HOLD_N
        g = np.linspace(-1.0, 1.0, N)
        u, v = np.meshgrid(g, g, indexing='xy')
        d = sp.oct_decode(u.ravel(), v.ravel())
        Y = d @ sp.LUM2020
        L1 = np.abs(d).sum(1)
        ok = Y > 0.02 * L1
        rgb = d * np.where(ok, 0.18 / np.where(ok, Y, 1.0), 0.0)[:, None]
        X, _ = self.film_xyz(np.where(ok[:, None], rgb, 0.18), None, self.off)
        a = f32(X / np.maximum(X[:, 1:2], 1e-12))
        w = np.where(ok, np.clip((Y / L1 - 0.02) / 0.08, 0.0, 1.0), 0.0)
        Xg, _ = self.film_xyz(np.full((1, 3), 0.18), None, self.off)
        n0 = f32(Xg[0] / Xg[0, 1])
        self.hold_n0 = n0
        W = np.array(HOLD_W)
        lf = lambda x: np.where(x > 0.008856451679, np.cbrt(np.maximum(x, 0.0)), 7.787037037 * x + 0.137931034)
        q = lf(a / W)
        q[:, 1] = lf(1.0)
        g = lf(n0 / W)
        g[1] = lf(1.0)
        d = np.stack([2.5 * ((q[:, 0] - q[:, 1]) - (g[0] - g[1])), (q[:, 1] - q[:, 2]) - (g[1] - g[2]),
                      np.zeros(len(q)), w], 1)
        self.hold_a = f32(d.reshape(N, N, 4))

    def bake_hold_neutral(self):
        xs = np.linspace(HOLD_X0, HOLD_X1, HOLD_NX)
        # a neutral of scene value g has xref = log10(g) + (log10 K(grey dir) * L1 terms): sample by
        # bisection-free construction: xref is affine in log10 g for neutrals
        g0 = np.full((1, 3), 0.18)
        _, xr0 = self.film_xyz(g0, None, self.off_user)
        gs = 0.18 * 10.0 ** (xs - xr0[0])
        X, _ = self.film_xyz(np.repeat(gs[:, None], 3, 1), None, self.off_user)
        self.hold_n = f32((X / np.maximum(X[:, 1:2], 1e-12)).T)          # (3, NX)
        self.bake_black_monotone(xs, X, float(xr0[0]))

    def bake_black_monotone(self, xs, X, xr0):
        """v1.3 (== Core.cpp, ColorState.h NT block): below grey, a print's layers approach their D-max at
        different rates, so the projected neutral turns bluer / greener in the last stops before black while
        its luminance hardly rises; a display channel with a negative matrix weight (Rec.709 R = 3.24 X -
        1.54 Y - 0.50 Z) can then fall by up to 3e-5 as the scene gets brighter. The display-linear neutral
        ramp v(x) = Mout XYZ is made rising from black to grey (see the loop below) and the
        difference per unit luminance, (v_mono - v) / Y, is added to every pixel at its reference exposure.
        Zero (bit-identical output) wherever the ramp is already monotonic."""
        v = X @ self.Mout.T
        kg = int(np.searchsorted(xs, xr0, side='right')) - 1
        # off unless the ramp really turns down (relative drop > BLACK_ON; float noise of ~1e-9 stays untouched)
        run = v[kg].copy()
        worst = 0.0
        for k in range(kg - 1, -1, -1):
            run = np.minimum(run, v[k + 1])
            worst = max(worst, float(np.max((v[k] - run) / np.maximum(np.abs(v[k]), 1e-12))))
        if worst <= BLACK_ON:
            self.black_d = None
            return
        vm = v.copy()
        for k in range(min(kg, len(xs) - 2), -1, -1):
            # fold the ramp: it falls by |v[k+1] - v[k]| per sample towards black, its own fall where it falls
            # and the mirrored step where it turns up. The offset vm - v stays twice the reversal at most and
            # float noise in a flat D-max only moves it by noise
            vm[k] = vm[k + 1] - np.abs(v[k + 1] - v[k])
        d = (vm - v) / np.maximum(X[:, 1:2], 1e-12)
        self.black_d = f32(d.T) if np.any(d != 0) else None              # (3, NX) or off

    def apply_hold(self, XYZ, xref, rgb_scene):
        """Hue hold on display XYZ (D65 adapted, 1 = white), in CIELAB-like coordinates
        f = cbrt(XYZ / W_D65), a = f_x - f_y, b = f_y - f_z (exactly CIELAB a*, b* up to the
        factors 500 / 200): the colour's (a, b) relative to the film's neutral of the same
        exposure and luminance is turned toward the anchor's (a, b) relative to the grey neutral,
        its length scaled by 1 + chromaHold * t; luminance unchanged."""
        if self.hold <= 0:
            return XYZ
        u, v, L1 = sp.oct_encode(rgb_scene)
        A = lut2d(self.hold_a, u, v)
        Ys = rgb_scene @ sp.LUM2020
        st = np.log2(np.maximum(Ys, 1e-10) / 0.18)
        # g: hold strength without the anchor validity weight (the chroma hold below uses it: the
        # weight falls along a saturation ramp toward the gamut edge and would fold the chroma)
        so = np.maximum(np.abs(st) - HOLD_ONSET, 0.0)        # v1.2: no hold within +-onset stops of grey
        g = self.hold * (1.0 - np.exp(-HOLD_K * so * so))
        g = np.where(Ys > 1e-10, g, 0.0)
        n = np.stack([lut1d(self.hold_n[c], HOLD_X0, HOLD_X1, xref) for c in range(3)], 1)
        W = np.array(HOLD_W)

        def ab(f):      # scaled like CIELAB (a* = 500 (fx - fy), b* = 200 (fy - fz)) / 200
            return np.stack([2.5 * (f[:, 0] - f[:, 1]), f[:, 1] - f[:, 2]], 1)

        def lf(x):      # CIELAB f
            return np.where(x > 0.008856451679, np.cbrt(np.maximum(x, 0.0)), 7.787037037 * x + 0.137931034)

        def lfinv(f):
            return np.where(f > 0.206896552, f ** 3, (f - 0.137931034) * 0.128418549)
        Y = XYZ[:, 1]
        f = lf(XYZ / W)
        fy = f[:, 1]
        fn = lf(n * Y[:, None] / W)
        fn[:, 1] = fy
        abn = ab(fn)
        c = ab(f) - abn
        ca = A[:, :2]
        cn = np.linalg.norm(c, axis=1)
        an = np.linalg.norm(ca, axis=1)
        good = (cn > 1e-9) & (an > 1e-9) & (Y > 1e-10) & (Ys > 1e-10)
        sst = lambda e0, e1, x: (lambda q: q * q * (3 - 2 * q))(np.clip((x - e0) / (e1 - e0), 0.0, 1.0))
        g = g * sst(0.002, 0.008, an)
        hc = np.arctan2(c[:, 1], c[:, 0])
        dh = np.arctan2(ca[:, 1], ca[:, 0]) - hc
        dh = np.where(dh > np.pi, dh - 2 * np.pi, np.where(dh < -np.pi, dh + 2 * np.pi, dh))
        g = g * (1.0 - sst(0.5 * np.pi, 0.9 * np.pi, np.abs(dh)))
        t = g * A[:, 3]
        h = hc + t * dh
        # chroma hold: k = cn + b cn / (1 + (cn / C0)^4), b = chromaHold g (0.3 + 0.7 ss(st)) <= 1.
        # dk/dcn = 1 + b (1 - 3 q) / (1 + q)^2 (q = (cn / C0)^4) >= 1 - 0.5625 b > 0: monotonic by
        # construction (a more saturated film colour always renders more saturated); the boost is
        # ~ b cn for weak colours, at most ~0.11 b near cn = 0.76 C0, and fades for strongly saturated
        # colours, which the display gamut mapping would only cut back
        b = np.minimum(self.hold_chroma * g * (0.3 + 0.7 * sst(-0.5, 0.5, st)), 1.5)
        q = (cn / HOLD_C0) ** 4
        k = cn + b * cn / (1.0 + q)
        c2 = abn + np.stack([np.cos(h), np.sin(h)], 1) * k[:, None]
        f2 = np.stack([c2[:, 0] * 0.4 + fy, fy, fy - c2[:, 1]], 1)
        out = W * lfinv(f2)
        out[:, 1] = Y
        return np.where(good[:, None], out, XYZ)

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
        # A part of the negative's own crossover is kept (lab scans are not perfectly neutral at
        # every density): each channel blends the neutral-referenced inversion with its own
        # per-channel inversion (own grey density and grey gamma), weight = scan crossover.
        alpha = 0.0 if self.mono else float(neg.get('scanCrossover', sc.get('crossover', 0.0)))
        # stock-specific scan contrast (the lab's per-stock scanner profile): slope of the inverted
        # log exposure relative to the G channel's grey gamma
        contrast = float(neg.get('scanContrast', 1.0))
        tab = np.zeros((3, N_CURVE))
        for j in range(3):
            t = self.sd0[j] + (self.sd1[j] - self.sd0[j]) * np.arange(N_CURVE) / (N_CURVE - 1)
            xj = invert_monotone(xs, D[:, j], t)
            neutral = (interp_uniform(D[:, 1], SCN_XA, SCN_XB, xj) - g_grey) / gam
            jg = interp_uniform(D[:, j], SCN_XA, SCN_XB, gx)
            jgam = (interp_uniform(D[:, j], SCN_XA, SCN_XB, gx + h) - interp_uniform(D[:, j], SCN_XA, SCN_XB, gx - h)) / (2 * h)
            own = (interp_uniform(D[:, j], SCN_XA, SCN_XB, xj) - jg) / jgam
            tab[j] = ((1.0 - alpha) * neutral + alpha * own) * contrast
        self.sinv = f32(tab)
        self.Mscan = np.eye(3) if self.mono else np.array(neg.get('scanMatrix', np.eye(3).tolist()))
        self.Mscan2 = np.zeros((3, 3)) if self.mono else np.array(neg.get('scanRoot', np.zeros((3, 3)).tolist()))
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
        enc = flush(np.clip(np.where(np.isfinite(enc), enc, 0.0), -65504.0, 65504.0))
        with np.errstate(over='ignore', invalid='ignore'):
            lin = decode_transfer(self.p.input, enc)
            lin = flush(np.where(np.isnan(lin), 0.0, np.clip(lin, -65504.0, 65504.0)))   # == kernel (float32 overflow)
            out = lin @ self.Min.T
            if self.emu_on:
                r = np.maximum(out[:, 0] * out[:, 1], 0.0)
                gb = np.maximum(out[:, 1] * out[:, 2], 0.0)
                rb = np.maximum(out[:, 0] * out[:, 2], 0.0)
                out = out @ self.emuA.T + np.sqrt(np.stack([r, gb, rb], 1)) @ self.emuB.T
            out = out * self.gain
        return flush(np.clip(np.where(np.isfinite(out), out, 0.0), -65504.0, 65504.0))

    def exposures(self, rgb):
        return exposures_from_log_table(self.LK, rgb)

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
            XYZ = project(self.jlut, self.pdmax, P, ps) @ self.Mcat.T
        elif self.kind == 'reversal':
            XYZ = project(self.jlut, self.pdmax, d, sil) @ self.Mcat.T
        else:
            ds = -lut3d(self.slut, 0.0, self.slut_max, d) + sil[:, None]
            o = self.off_user if off is None else off
            v = np.stack([lut1d(self.sinv[j], self.sd0[j], self.sd1[j], ds[:, j]) for j in range(3)], 1) + o[None, :]
            lin = 10.0 ** v
            rp = np.sqrt(np.stack([lin[:, 0] * lin[:, 1], lin[:, 1] * lin[:, 2], lin[:, 0] * lin[:, 2]], 1))
            e = lin @ self.Mscan.T + rp @ self.Mscan2.T          # root-polynomial lab correction
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
        W = self.white if self.kind == 'print' else sp.xy_to_XYZ(sp.WHITE_D65)   # v1.3: the projection white
        target = np.log10(W * tY)
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
        # the unroll reaches the display peak at +10 stops over grey (speculars use the full range)
        Xw, _ = self.film_xyz(np.full((1, 3), 0.18 * 2.0 ** HDR_PEAK_STOPS), None, self.off)
        self.Ywhite = Xw[0, 1]
        self.Lref = HDR_GREY_NITS / self.Ygrey
        t = (HDR_PEAK_STOPS * math.log10(2.0) - HDR_E0) / HDR_ER
        self.Gmax = max(0.0, math.log10(self.peak / (self.Lref * self.Ywhite))) * (1.0 + t * t) / (t * t)

    # ------------------------------------------------------------------ output stage
    def gamut_compress(self, rgb, lum):
        Y = rgb @ lum
        e = 0.02 * Y                     # smooth minimum (== ColorKernels.h celc_gamut_compress)
        m1 = 0.5 * (rgb[:, 0] + rgb[:, 1] - np.sqrt((rgb[:, 0] - rgb[:, 1]) ** 2 + e * e))
        mn = 0.5 * (m1 + rgb[:, 2] - np.sqrt((m1 - rgb[:, 2]) ** 2 + e * e))
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

    def output(self, XYZ, xref, rgb_scene=None):
        lum = self.Mout_lum()
        if not self.hdr:
            if rgb_scene is not None:
                XYZ = self.apply_hold(XYZ, xref, rgb_scene)
            rgb = XYZ @ self.Mout.T + self.black_fix(XYZ, xref)
            rgb = self.gamut_compress(rgb, lum)
            rgb = self.limit(rgb, SDR_KNEE, 1.0)
            return self.encode_sdr(rgb)
        e = xref - LOG10_018
        t = np.maximum(e - HDR_E0, 0.0) / HDR_ER
        G = self.Gmax * t * t / (1.0 + t * t)
        if rgb_scene is not None:
            XYZ = self.apply_hold(XYZ, xref, rgb_scene)
        rgb = (XYZ @ self.Mout.T + self.black_fix(XYZ, xref)) * (self.Lref * 10.0 ** (self.p.hdrUnroll * G))[:, None]
        rgb = self.gamut_compress(rgb, lum)
        rgb = self.limit(rgb, HDR_KNEE * self.peak, self.peak)
        if self.out == 'Rec2100_PQ':
            return pq_encode(rgb)
        return self.hlg_encode(rgb)

    def black_fix(self, XYZ, xref):
        if getattr(self, 'black_d', None) is None:
            return 0.0
        d = np.stack([lut1d(self.black_d[c], HOLD_X0, HOLD_X1, xref) for c in range(3)], 1)
        return XYZ[:, 1:2] * d

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
        return hlg_oetf(np.minimum(E, 1.0))       # signal range (== kernel)

    # ------------------------------------------------------------------ full pixel
    def process(self, enc, noise=None):
        enc = np.atleast_2d(np.asarray(enc, dtype=np.float64))
        rgb = self.decode(enc)
        return self.scene_to_output(rgb, noise)

    def scene_to_output(self, rgb, noise=None):
        rgb = np.atleast_2d(np.asarray(rgb, dtype=np.float64))
        rgb = np.where(np.isfinite(rgb), rgb, 0.0)
        rgb = flush(np.clip(rgb, -65504.0, 65504.0))
        if noise is not None:
            noise = np.clip(np.where(np.isfinite(noise), noise, 0.0), -8.0, 8.0)
        XYZ, xref = self.film_xyz(rgb, noise)
        out = self.output(XYZ, xref, rgb)
        return np.where(np.isfinite(out), out, 0.0)

    # ------------------------------------------------------------------ Texture-Only
    def display_decode(self, v):
        v = flush(np.clip(np.where(np.isfinite(v), v, 0.0), -65504.0, 65504.0))
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
        lin = flush(np.clip(np.where(np.isfinite(lin), lin, 0.0), -65504.0, 65504.0))
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
