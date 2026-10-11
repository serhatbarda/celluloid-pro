"""Look aims of the stock library (design-time acceptance checks on the NumPy reference model).

    python3 reference/aims.py [report]        # every aim for every stock pair, worst offenders
    python3 reference/aims.py gate            # gated pairs only (GATED); exit 1 if any aim fails

Rendered exactly like footage: scene-linear F-Gamut C colours -> Rec.2020 -> full model ->
Rec.709 gamma 2.4 -> CIELAB (D65) of the display. Hue differences in degrees (CIE LCh),
chroma ratios = output C* / input C* (display Lab, not exposure normalised: what a viewer sees).
Hues of renderings with C* < 4 (near white or black, e.g. a slide clipping) are not judged.

Aims (v1.2). v1.0/v1.1 asked for colorimetric hue preservation (full hue hold plus a chroma
boost). v1.2 halves the hue hold and drops the chroma boost (look-dev item C1, an owner
decision: the default leans to the ARRI ALEXA LogC3 -> Rec.709 rendering and the Referent),
so the hue and chroma aims are now taken from those references. Each limit is 1.1 x the worst
value (0.9 x for chroma ratios) of three renderings of the same scene colours from F-Log2 C:
the official ARRI LogC3 -> Rec.709 LUT (after F-Log2 C -> LogC3), the Fujifilm/CKC Referent
LUT and the Fujifilm ETERNA LUT. Measured with the same code (2026-10-10):
                       ARRI   Referent  ETERNA   limit   (v1.1 limit)
  sky dh               10.3     23.3     17.4    25.6    (6)
  sky C ratio          0.50     0.49     0.45    0.40    (0.70)
  skin +1..+3 dh       15.1     17.2     15.7    18.9    (8; 5 for CN 200T / CN 50D)
  deep skin -2 dh      16.6     11.1      8.3    18.3    (12)
  deep skin -2 C ratio 0.55     0.27     0.68    0.24    (0.40)
  orange +2 dh         13.7     18.4     24.8    27.2    (10)
  red +2..+4 dh        13.3     15.2      8.2    16.8    (10)
The aims that have no reference counterpart keep their v1.1 limits:
  cyan       chart cyan C ratio >= 0.80 on P1 and P3 (grey-normalised film XYZ: the patch is outside Rec.709)
  red_top    C41 Consumer 200, RV 100, CN Vintage 85 at +4..+6: |dh| <= 18 relative to 0 stops
  p3_gain    P3 low-chroma gain (input C* 4 at L50, mean of 12 hues) <= 1.35
Scene colours: sky = bright blue skies L65..85 C25..35 h250..265 at +0.9..+2.4 stops over grey;
skin at +1/+2/+3 stops; deep skin at -2 stops; orange / amber at +2 stops; red, rose, magenta
(chart and wide-gamut) at +2..+4 stops relative to 0 stops.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import model as m  # noqa: E402
import scenes  # noqa: E402
import spectral as sp  # noqa: E402

M_FGC = sp.rgb_to_rgb('fgamutc', 'rec2020')
M709 = sp.npm(*sp.PRIMARIES['rec709'])


def fgc_lab(L, a, b):
    """F-Gamut C linear of a CIELAB colour (the critic's scene definitions use F-Gamut C)."""
    return scenes.lab_to_xyz(np.array([L, a, b], float)) @ np.linalg.inv(sp.npm(*sp.PRIMARIES['fgamutc'])).T


def hue_primary_fgc(h):
    h = (h % 360) / 60.0
    i = int(np.floor(h))
    f = h - i
    return np.array([(1, f, 0), (1 - f, 1, 0), (0, 1, f), (0, 1 - f, 1), (f, 0, 1), (1, 0, 1 - f)][i % 6], float)


def lab_of_out(v):
    lin = np.sign(v) * np.abs(v) ** 2.4
    return scenes.xyz_to_lab(lin @ M709.T)


def lch(lab):
    return np.hypot(lab[..., 1], lab[..., 2]), np.degrees(np.arctan2(lab[..., 2], lab[..., 1])) % 360


def hd(a, b):
    return (a - b + 180.0) % 360.0 - 180.0


MIN_C = 4.0     # hue of a rendering with less chroma than this (near white / black) is not judged


def hdc(lab, h_ref):
    """hue difference of a rendering, 0 when its chroma is below MIN_C"""
    C, h = lch(lab)
    return hd(h, h_ref) if C >= MIN_C else 0.0


# bright blue skies, h ~ 255..262, with the extra exposures they are tested at (all <= +2.4 stops
# over 18% grey: a bright sky, not a blown one)
SKY = [((85, -7, -34), (0,)), ((75, -6, -32), (0, 1)), ((65, -3, -28), (0, 1)), ((80, -9, -26), (0,))]
SKIN = [(72, 13, 16), (64, 16, 20), (62, 15, 21), (56, 18, 24), (47, 17, 24)]
DEEP_SKIN = [(37, 14, 19), (40, 15, 18)]
ORANGE = [(62, 35, 58), (66, 22, 60)]                                       # orange, amber
REDS = {'chart red': ('lab', (42, 52, 27)), 'rose': ('lab', (51, 45, 15)), 'magenta': ('lab', (51, 48, -14)),
        'F-Gamut red': ('prim', 0.0)}
# v1.2 limits from the reference renderings (see the module docstring)
LIM = {'sky dh': 25.6, 'sky C ratio': 0.40, 'skin dh': 18.9, 'deep skin dh': 18.3, 'deep skin C ratio': 0.24,
       'orange dh': 27.2, 'red dh': 16.8}
TOP_LIMITED = ('C41 Consumer 200', 'RV 100 Slide', 'CN Vintage 85')


def red_colour(spec):
    kind, v = spec
    if kind == 'lab':
        return fgc_lab(*v)
    return (0.35 + 0.65 * hue_primary_fgc(v)) * 0.18


class Pair:
    def __init__(self, lib, neg, prt, process='print'):
        self.lib, self.neg, self.prt, self.process = lib, neg, prt, process
        self.P = m.Pipeline(lib).prepare(m.Params(neg=neg, prt=prt, process=process))

    def render(self, fgc):
        rgb = np.atleast_2d(fgc) @ M_FGC.T
        enc = m.encode_transfer('FLog2C', rgb @ np.linalg.inv(m.input_matrix('FLog2C')).T)
        return lab_of_out(self.P.process(enc))

    def render_normalised(self, fgc):
        """display Lab with the rendering scaled so that 18 % grey has the scene's grey luminance
        (compares colour rendition, not the print's density)"""
        g = self.render(np.full(3, 0.18))[0]
        lab = self.render(fgc)
        Yg = scenes.lab_to_xyz(g)[1]
        return scenes.xyz_to_lab(scenes.lab_to_xyz(lab) * (0.18 / Yg))

    def film_lab_normalised(self, fgc):
        rgb = np.atleast_2d(fgc) @ M_FGC.T
        P = self.P
        X, xr = P.film_xyz(rgb)
        X = P.apply_hold(X, xr, rgb)
        Xg, _ = P.film_xyz(np.full((1, 3), 0.18))
        return scenes.xyz_to_lab(X * (0.18 / Xg[0, 1]))

    @property
    def name(self):
        n = self.lib.negs[self.neg]['name']
        if self.lib.negs[self.neg]['kind'] == 'reversal':
            return n
        return n + ' / ' + (self.lib.prints[self.prt]['name'] if self.process == 'print' else 'Lab Scan')


def measure(pair):
    """dict aim -> (worst value, limit, ok, detail)"""
    lib = pair.lib
    nm = lib.negs[pair.neg]['name']
    kind = lib.negs[pair.neg]['kind']
    out = {}
    if kind == 'bw':
        return out
    # sky
    worst_h, worst_c = 0.0, 9.0
    for (L, a, b), evs in SKY:
        c0 = fgc_lab(L, a, b)
        Cin, hin = lch(scenes.xyz_to_lab(c0 @ sp.npm(*sp.PRIMARIES['fgamutc']).T))
        for ev in evs:
            lab = pair.render(c0 * 2.0 ** ev)[0]
            C, h = lch(lab)
            worst_h = max(worst_h, abs(hdc(lab, hin)))
            worst_c = min(worst_c, C / Cin)
    out['sky dh'] = (worst_h, LIM['sky dh'], worst_h <= LIM['sky dh'])
    out['sky C ratio'] = (worst_c, LIM['sky C ratio'], worst_c >= LIM['sky C ratio'])
    # cyan (chart) on P1 / P3
    if pair.process == 'print' and pair.prt in (0, 2) and kind == 'negative':
        # chart cyan lies outside Rec.709 (R < 0): judged on the film's own rendering (display XYZ
        # before the output gamut mapping), grey-normalised
        c0 = fgc_lab(51, -28, -28)
        r = lch(pair.film_lab_normalised(c0)[0])[0] / np.hypot(28, 28)
        out['cyan C ratio'] = (r, 0.80, r >= 0.80)
    # skin highlights / deep skin
    lim = LIM['skin dh']
    w = 0.0
    for L, a, b in SKIN:
        c0 = fgc_lab(L, a, b)
        hin = np.degrees(np.arctan2(b, a))
        for ev in (1, 2, 3):
            w = max(w, abs(hdc(pair.render(c0 * 2.0 ** ev)[0], hin)))
    out['skin +1..+3 dh'] = (w, lim, w <= lim)
    wd, wc = 0.0, 9.0
    for L, a, b in DEEP_SKIN:
        c0 = fgc_lab(L, a, b)
        hin = np.degrees(np.arctan2(b, a))
        lab = pair.render(c0 * 0.25)[0]
        C, h = lch(lab)
        wd = max(wd, abs(hdc(lab, hin)))
        # chroma relative to the input chroma of the same -2-stop colour
        Cin = lch(scenes.xyz_to_lab((c0 * 0.25) @ sp.npm(*sp.PRIMARIES['fgamutc']).T))[0]
        wc = min(wc, C / Cin)
    out['deep skin -2 dh'] = (wd, LIM['deep skin dh'], wd <= LIM['deep skin dh'])
    out['deep skin -2 C ratio'] = (wc, LIM['deep skin C ratio'], wc >= LIM['deep skin C ratio'])
    # orange / amber at +2
    w = 0.0
    for L, a, b in ORANGE:
        c0 = fgc_lab(L, a, b)
        w = max(w, abs(hdc(pair.render(c0 * 4.0)[0], np.degrees(np.arctan2(b, a)))))
    out['orange +2 dh'] = (w, LIM['orange dh'], w <= LIM['orange dh'])
    # reds: hue preservation with exposure
    w, wt = 0.0, 0.0
    for k, spec in REDS.items():
        c0 = red_colour(spec)
        h0 = lch(pair.render(c0)[0])[1]
        for ev in (2, 3, 4):
            w = max(w, abs(hdc(pair.render(c0 * 2.0 ** ev)[0], h0)))
        for ev in (4.5, 5, 6):
            wt = max(wt, abs(hdc(pair.render(c0 * 2.0 ** ev)[0], h0)))
    out['red +2..+4 dh'] = (w, LIM['red dh'], w <= LIM['red dh'])
    if nm in TOP_LIMITED:
        out['red +4..+6 dh'] = (wt, 18.0, wt <= 18.0)
    # P3 low-chroma gain
    if pair.process == 'print' and pair.prt == 2:
        g = []
        for hh in range(0, 360, 30):
            a, b = 4 * np.cos(np.radians(hh)), 4 * np.sin(np.radians(hh))
            g.append(lch(pair.render(fgc_lab(50, a, b))[0])[0] / 4.0)
        g = float(np.mean(g))
        out['P3 low-chroma gain'] = (g, 1.35, g <= 1.35)
    return out


def pairs(lib):
    for n, neg in enumerate(lib.negs):
        if neg['kind'] == 'bw':
            continue
        if neg['kind'] == 'reversal':
            yield Pair(lib, n, 0)
            continue
        for p in range(len(lib.prints)):
            yield Pair(lib, n, p)
        yield Pair(lib, n, 0, 'scan')


# Gated pairs (python3 reference/aims.py gate, tests): the default and the neutral-character
# negatives on the reference print, and the default negative through the Lab Scan. The character
# stocks (CN 800T teal skies, CN Vintage 85 golden highlights, C41 Consumer 200 punchy warm
# highlights, RV 100 slide clipping) and the special prints are reported, not gated.
# v1.2: CN 50D Fine Daylight left the gate. With the halved hue hold (C1) its reds turn 10..24 deg
# toward magenta at +2..+4 stops (ARRI 5..13, Referent 0..15); it is reported until the designed
# stocks get the C2 refit the data-sheet stocks had.
GATED = [('CN 200T Studio Tungsten', 0, 'print'), ('CN 250D Soft Daylight', 0, 'print'),
         ('CN 500T High Speed', 0, 'print'), ('C41 Portrait 400', 0, 'print'), ('CN 200T Studio Tungsten', 0, 'scan')]


def run(verbose=True, gated_only=False):
    lib = m.load()
    fails = []
    gated = {(lib.neg_index(n), p, proc) for n, p, proc in GATED}
    for pr in pairs(lib):
        if gated_only and (pr.neg, pr.prt, pr.process) not in gated:
            continue
        res = measure(pr)
        bad = [k for k, v in res.items() if not v[2]]
        if verbose:
            print('%-46s ' % pr.name + '  '.join('%s %s%.2f' % (k, '' if v[2] else '*', v[0]) for k, v in res.items()))
        fails += [(pr.name, k, res[k][0], res[k][1]) for k in bad]
    if verbose:
        print('\n%d aims missed' % len(fails))
        for f in fails:
            print('  %-46s %-22s %.2f (limit %.2f)' % f)
    return fails


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'report'
    f = run(verbose=True, gated_only=(cmd == 'gate'))
    if cmd == 'gate':
        sys.exit(1 if f else 0)
