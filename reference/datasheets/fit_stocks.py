"""Fit the model parameters of the real stocks to the digitised data-sheet curves (design-time, SciPy).

    python3 reference/datasheets/fit_stocks.py            # fit all, print residuals, write fits.json
    python3 reference/datasheets/fit_stocks.py --plots D   # + overlay plots (data vs fit) into folder D
    python3 reference/datasheets/fit_stocks.py --v13       # refit only the v1.3 additions

What is fitted (reference/datasheets/data/*.csv -> reference/datasheets/fits.json):
  spectral sensitivity  log sensitivity -> relative linear sensitivity (peak 1), fitted by up to 6
                        asymmetric Gaussian lobes (the model's LobeSet); the residual mixes linear and
                        log error so the skirts that drive metamerism are kept, not only the peaks
  spectral dye density  peak-normalised dye curves -> up to 6 lobes; lobes centred within the dye's
                        main band are 'primary', the others 'secondary' (the part masking couplers
                        compensate in a negative)
  characteristic curve  status M / A density over log exposure -> the model's CurveDef
                        (fog, gamma, k, x0, R, p: soft-plus toe, straight line, shoulder), fitted to
                        density above D-min (+ the model's fog) with x in log10 exposure relative to
                        an 18 % grey of 1 (grey at log10 0.18; for sheets on a relative scale the
                        grey position is noted per stock)
The resulting parameters are written into reference/stocks.json by build_stocks.py.
"""
import json
import os
import sys

import numpy as np
from scipy.optimize import least_squares

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, 'data')
L18 = np.log10(0.18)
STOP = np.log10(2.0)
WL = np.arange(380, 781, 5.0)


def load(name):
    a = np.loadtxt(os.path.join(DATA, name + '.csv'), delimiter=',', ndmin=2)
    a = a[np.argsort(a[:, 0], kind='stable')]
    # merge duplicate x (vector paths repeat end points): average
    ux, inv = np.unique(np.round(a[:, 0], 3), return_inverse=True)
    uy = np.bincount(inv, a[:, 1]) / np.bincount(inv)
    return ux, uy


def resample(x, y, grid):
    return np.interp(grid, x, y, left=np.nan, right=np.nan)


# ------------------------------------------------------------------ lobes
def lobes_eval(p, wl):
    out = np.zeros_like(wl, dtype=float)
    for mu, sl, sr, a in np.reshape(p, (-1, 4)):
        s = np.where(wl < mu, sl, sr)
        out += a * np.exp(-0.5 * ((wl - mu) / s) ** 2)
    return out


def fit_lobes(wl, y, n, init=None, logw=0.0, floor=1e-3, bounds_mu=(360, 800), wts=None):
    wts = np.ones_like(y) if wts is None else wts
    if init is None:
        pk = wl[np.argsort(-y)]
        mus = []
        for m in pk:
            if all(abs(m - q) > 25 for q in mus):
                mus.append(m)
            if len(mus) == n:
                break
        while len(mus) < n:
            mus.append(float(np.mean(wl)))
        init = []
        for m in mus:
            init += [m, 25.0, 25.0, float(np.interp(m, wl, y))]
    lo, hi = [], []
    for _ in range(n):
        lo += [bounds_mu[0], 4.0, 4.0, 0.0]
        hi += [bounds_mu[1], 150.0, 150.0, 3.0]
    init = np.clip(init, np.array(lo) + 1e-6, np.array(hi) - 1e-6)

    def res(p):
        f = lobes_eval(p, wl)
        r = (f - y) * wts
        if logw:
            r = np.r_[r, logw * (np.log10(np.maximum(f, 0) + floor) - np.log10(np.maximum(y, 0) + floor)) * wts]
        return r
    s = least_squares(res, init, bounds=(lo, hi), max_nfev=20000)
    return s.x, float(np.sqrt(np.mean((lobes_eval(s.x, wl) - y) ** 2)))


def sens_curve(stock, layer, lo=380, hi=780):
    x, ls = load(f'{stock}_sens_{layer}')
    g = np.arange(max(lo, np.ceil(x.min() / 5) * 5), min(hi, np.floor(x.max() / 5) * 5) + 1, 5.0)
    v = 10 ** resample(x, ls, g)
    return g, v


def fit_sens(stocks_layers, n=4):
    """sensitivities of three layers, normalised to a common peak (relative heights are irrelevant:
    the model normalises every layer at grey)"""
    out = {}
    for layer, (g, v) in stocks_layers.items():
        v = v / v.max()
        # zero outside the plotted range (the sheets plot down to ~2 decades below the peak)
        gz = np.r_[np.arange(380, g.min(), 5.0), g, np.arange(g.max() + 5, 781, 5.0)]
        vz = np.r_[np.zeros(len(np.arange(380, g.min(), 5.0))), v, np.zeros(len(np.arange(g.max() + 5, 781, 5.0)))]
        best = None
        for k in range(2, n + 1):
            p, e = fit_lobes(gz, vz, k, logw=0.08, floor=3e-3)
            if best is None or e < best[1] * 0.85:
                best = (p, e, k)
        out[layer] = dict(lobes=np.round(np.reshape(best[0], (-1, 4)), 4).tolist(), rms=best[1])
    return out


def fit_dye(g, v, main, n=5):
    """main = (lo, hi) of the dye's own absorption band: lobes centred in it are primary"""
    v = np.maximum(v, 0)
    best = None
    for k in range(2, n + 1):
        p, e = fit_lobes(g, v, k, logw=0.02, floor=0.02, bounds_mu=(340, 820))
        if best is None or e < best[1] * 0.85:
            best = (p, e)
    L = np.reshape(best[0], (-1, 4))
    prim = [l for l in L if main[0] <= l[0] <= main[1] and l[3] > 0.02]
    sec = [l for l in L if not (main[0] <= l[0] <= main[1]) and l[3] > 0.003]
    sc = 1.0   # absolute scale is irrelevant: the model re-normalises dye amounts (END)
    r = lambda ls: np.round(np.array(ls) * np.array([1, 1, 1, sc]), 4).tolist() if len(ls) else []  # noqa: E731
    return dict(primary=r(prim), secondary=r(sec), rms=best[1])


# ------------------------------------------------------------------ characteristic curves
def softplus(z):
    return np.where(z > 30, z, np.log1p(np.exp(np.minimum(z, 30))))


def dev(y, gamma, k, R, p):
    r = gamma * softplus(k * y) / k
    return r / (1 + (r / R) ** p) ** (1 / p)


def fit_curve(x, d, fog=0.1, reversal=False, p0=None, pmax=12.0, rmin=0.5):
    """x: log10 exposure (grey at log10 0.18); d: density ABOVE D-min (+fog added here)"""
    y = d + fog

    def model(q, xx):
        g, k, x0, R, p = q
        return fog + dev((x0 - xx) if reversal else (xx - x0), g, k, R, p)
    q0 = p0 or ([0.6, 4.0, -2.5, 2.5, 2.5] if not reversal else [2.0, 4.0, 0.0, 4.0, 4.0])
    q0 = list(q0[:4]) + [min(q0[4], pmax)]
    lo = [0.2, 0.8, -6.0, rmin, 1.0]
    hi = [6.0, 20.0, 3.0, 7.0, pmax]
    s = least_squares(lambda q: model(q, x) - y, q0, bounds=(lo, hi), max_nfev=20000)
    g, k, x0, R, p = s.x
    rms = float(np.sqrt(np.mean((model(s.x, x) - y) ** 2)))
    return dict(fog=fog, gamma=round(float(g), 4), k=round(float(k), 4), x0=round(float(x0), 4), R=round(float(R), 4), p=round(float(p), 4)), rms


def hd(stock, layer, xmap):
    x, d = load(f'{stock}_hd_{layer}')
    return xmap(x), d


# ------------------------------------------------------------------ stocks
def stops(x):
    return L18 + x * STOP


def neg_grey_from_density(stock, ref_above_dmin):
    """grey position for sheets on a relative log H scale: where the G curve reaches the same density
    above D-min as the stops-scaled reference (VISION3 500T / 200T at 0 stops)"""
    x, d = load(f'{stock}_hd_G')
    above = d - d[:max(3, len(d) // 20)].mean()
    i = np.argmax(above > ref_above_dmin)
    return float(np.interp(ref_above_dmin, above[max(i - 3, 0):i + 3], x[max(i - 3, 0):i + 3]))


DYE_BANDS = dict(C=(600, 760), M=(500, 590), Y=(400, 485))


def identify_dyes(stock, names):
    """map anonymous dye curves (vector sheets) to C / M / Y by their peak wavelength"""
    out = {}
    for nm in names:
        g, v = load(f'{stock}_dye_{nm}')
        pk = g[np.argmax(v)]
        key = 'Y' if pk < 485 else ('M' if pk < 595 else 'C')
        out[key] = (g, v)
    return out


def fit_negative(stock, xmap, dyes_from=None, dye_names=('a', 'b', 'c'), reversal=False, fog=0.1, hd_layers=('R', 'G', 'B')):
    res = dict(stock=stock)
    layers = {'R': sens_curve(stock, 'C'), 'G': sens_curve(stock, 'M'), 'B': sens_curve(stock, 'Y')}
    res['sens'] = fit_sens(layers)
    ds = dyes_from or stock
    try:
        D = identify_dyes(ds, dye_names)
    except OSError:
        D = {k: load(f'{ds}_dye_{k}') for k in ('C', 'M', 'Y')}
    res['dyes'] = {k: fit_dye(np.r_[D[k][0]], D[k][1], DYE_BANDS[k]) for k in ('C', 'M', 'Y')}
    res['dyes_from'] = ds
    res['curves'] = {}
    for L in hd_layers:
        x, d = hd(stock, L, xmap)
        dmin = d[np.argmin(x)] if not reversal else d.min()
        above = d - (d[:max(3, len(d) // 25)].mean() if not reversal else d.min())
        c, rms = fit_curve(x, np.maximum(above, 0), fog=fog, reversal=reversal)
        res['curves'][L] = dict(c, rms=rms, dmin=float(dmin))
    return res


def fit_print(stock, xmap, dyes_from, fog=0.05, sens_from=None, pmax=12.0):
    res = dict(stock=stock)
    ss = sens_from or stock
    res['sens'] = fit_sens({'R': sens_curve(ss, 'C'), 'G': sens_curve(ss, 'M'), 'B': sens_curve(ss, 'Y')}, n=4)
    D = {k: load(f'{dyes_from}_dye_{k}') for k in ('C', 'M', 'Y')}
    res['dyes'] = {k: fit_dye(D[k][0], D[k][1], DYE_BANDS[k]) for k in ('C', 'M', 'Y')}
    res['dyes_from'] = dyes_from
    res['curves'] = {}
    for L in ('R', 'G', 'B'):
        x, d = hd(stock, L, xmap)
        above = d - d[:max(3, len(d) // 25)].mean()
        c, rms = fit_curve(x, np.maximum(above, 0), fog=fog, p0=[3.0, 5.0, float(np.median(x)) - 0.6, 4.0, 3.5], pmax=pmax)
        res['curves'][L] = dict(c, rms=rms, dmin=float(d.min()))
    return res


def main():
    plots = sys.argv[sys.argv.index('--plots') + 1] if '--plots' in sys.argv else None
    if '--v13' in sys.argv:
        # refit only what v1.3 added, keep every other entry of fits.json as it is
        F = json.load(open(os.path.join(HERE, 'fits.json')))
        fit_v13(F)
        json.dump(F, open(os.path.join(HERE, 'fits.json'), 'w'), indent=1)
        return
    F = {}
    # stops axes (0 = normal exposure of an 18 % grey card)
    F['kodak_5219'] = fit_negative('kodak_5219', stops)
    F['kodak_5213'] = fit_negative('kodak_5213', stops)
    # relative log H axes: grey placed where G has the 500T's density above D-min at 0 stops
    x5, d5 = load('kodak_5219_hd_G')
    ref = float(np.interp(0.0, x5, d5) - d5[:8].mean())
    for st in ('kodak_5207', 'kodak_5203'):
        xg = neg_grey_from_density(st, ref)
        F[st] = fit_negative(st, (lambda xg: lambda x: L18 + (x - xg))(xg), dyes_from='kodak_5219')
        F[st]['grey_logH'] = xg
    # Portra 400: "Log H Ref -1.44" = normal exposure; dye curves are not published -> VISION3 set (C-41 / ECN-2 coupler families)
    F['kodak_portra400'] = fit_negative('kodak_portra400', lambda x: L18 + (x + 1.44), dyes_from='kodak_5219')
    # Fujifilm ETERNA 250D: characteristic curves only (camera stops); spectral data -> VISION3 250D proxies
    f8 = fit_negative('kodak_5207', stops)  # placeholder sens/dyes, curves replaced below
    F['fuji_8563'] = dict(stock='fuji_8563', sens=F['kodak_5207']['sens'], dyes=F['kodak_5207']['dyes'], dyes_from='kodak_5219 (proxy)',
                          sens_from='kodak_5207 (proxy)', curves={})
    for L in ('R', 'G', 'B'):
        x, d = load(f'fuji_8563_hd_{L}')
        c, rms = fit_curve(stops(x), np.maximum(d - d[:3].mean(), 0))
        F['fuji_8563']['curves'][L] = dict(c, rms=rms, dmin=float(d[:3].mean()))
    del f8
    # Ektachrome E100(G) reversal: log H axis, grey placed at the exposure giving G density 1.0 above D-min (+0.1 fog)
    xe, de = load('kodak_e100_hd_G')
    above = de - de.min()
    xg = float(np.interp(-1.0, -above[::-1], xe[::-1]))  # above is decreasing with x
    F['kodak_e100'] = fit_negative('kodak_e100', lambda x: L18 + (x - xg), dye_names=('a', 'b', 'c'), reversal=True, fog=0.15)
    F['kodak_e100']['grey_logH'] = xg
    # prints (Status A): x is the printer's log H; the model's grey balance places the operating point
    F['kodak_2383'] = fit_print('kodak_2383', lambda x: x - 1.0, 'kodak_2383')
    F['kodak_2393'] = fit_print('kodak_2393', lambda x: x - 0.9, 'kodak_2383')
    # 3513DI: the free fit puts a near-step shoulder (p = 12, the bound) on R and B. With G at
    # p = 8 the layers then leave D-max at different rates and the projected red of a grey ramp
    # dips by up to 6e-5 in the last stops before black. p <= 6 keeps the ramp monotone; the rms
    # against the published curves rises from 0.052 / 0.040 D to 0.082 / 0.062 D (R / B), almost
    # all of it in the shoulder above D 4, where the data sheet itself is least certain.
    F['fuji_3513'] = fit_print('fuji_3513', lambda x: x - 2.0, 'kodak_2383', sens_from='kodak_2383', pmax=6.0)
    # v1.3: VISION Color Intermediate 2242 (Status M, relative log H): the duplicating curve of the release-print
    # chain (master positive and duplicate negative are both printed on it); curves only, D-min kept as the fog
    F['kodak_2242'] = dict(stock='kodak_2242', curves={})
    for L in ('R', 'G', 'B'):
        x, d = load(f'kodak_2242_hd_{L}')
        dmin = float(d[:3].mean())
        c, rms = fit_curve(x, np.maximum(d - dmin, 0), fog=round(dmin, 4), p0=[1.0, 4.0, -0.5, 2.5, 3.0])
        F['kodak_2242']['curves'][L] = dict(c, rms=rms, dmin=dmin)
    fit_v13(F)
    for k, v in F.items():
        if 'sens' not in v:
            print(k, 'curve rms', {l: round(s['rms'], 4) for l, s in v['curves'].items()})
            continue
        print(k, 'sens rms', {l: round(s['rms'], 4) for l, s in v['sens'].items()}, 'dye rms', {l: round(s['rms'], 4) for l, s in v.get('dyes', {}).items()},
              'curve rms', {l: round(s['rms'], 4) for l, s in v['curves'].items()})
    json.dump(F, open(os.path.join(HERE, 'fits.json'), 'w'), indent=1)
    if plots:
        plot_all(F, plots)


def fit_v13(F):
    """v1.3 WP-2 additions (append-only stocks; the fits above are unchanged)"""
    # VISION2 500T 5218: full Kodak data (camera stops)
    F['kodak_5218'] = fit_negative('kodak_5218', stops)
    # EKTACHROME 100D 5294 (reversal, camera stops; 0 = normal exposure)
    F['kodak_5294'] = fit_negative('kodak_5294', stops, reversal=True, fog=0.15)
    # EASTMAN DOUBLE-X 5222 (B&W, D-96 6 1/2 min). Axis: log H in lux-seconds. Grey = the ISO speed point
    # (0.1 above fog at EI 250 daylight: H = 0.8 / 250) + 1.0 log H, the usual 18 % card placement.
    # The traced 6 1/2 min curve ends at D 1.66; R >= 2.4 keeps the extrapolated shoulder near the longer
    # development curves of the same figure instead of a hard stop just above the data.
    xg = float(np.log10(0.8 / 250.0) + 1.0)
    x, d = load('kodak_5222_hd_G')
    fog = float(d[:5].mean())
    c, rms = fit_curve(L18 + (x - xg), np.maximum(d - fog, 0), fog=0.2, p0=[0.66, 3.0, -2.2, 2.6, 2.5], pmax=4.0, rmin=2.4)
    g, v = sens_curve('kodak_5222', 'G')
    F['kodak_5222'] = dict(stock='kodak_5222', sens=fit_sens({'G': (g, v)}), grey_logH=xg,
                           curves={'G': dict(c, rms=rms, dmin=fog)})


def plot_all(F, folder):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    os.makedirs(folder, exist_ok=True)
    for k, v in F.items():
        fig, ax = plt.subplots(1, 3, figsize=(15, 4))
        for L, c in zip(('R', 'G', 'B'), 'rgb'):
            ax[0].plot(WL, lobes_eval(np.ravel(v['sens'][L]['lobes']), WL), c)
        for K, c in zip(('C', 'M', 'Y'), 'cmy'):
            dd = v['dyes'][K]
            ax[1].plot(WL, lobes_eval(np.ravel(dd['primary'] + dd['secondary']) if dd['secondary'] else np.ravel(dd['primary']), WL), c)
        for L, c in zip(('R', 'G', 'B'), 'rgb'):
            cv = v['curves'][L]
            xx = np.linspace(-5, 3, 300)
            y = cv['fog'] + (dev(cv['x0'] - xx, cv['gamma'], cv['k'], cv['R'], cv['p']) if k == 'kodak_e100' else dev(xx - cv['x0'], cv['gamma'], cv['k'], cv['R'], cv['p']))
            ax[2].plot(xx, y, c)
        ax[0].set_title(k + ' sensitivity (fit)')
        ax[1].set_title('dyes (fit)')
        ax[2].set_title('curves (fit)')
        fig.savefig(os.path.join(folder, k + '.png'), dpi=70)
        plt.close(fig)


if __name__ == '__main__':
    main()
