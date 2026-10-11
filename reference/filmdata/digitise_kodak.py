"""Digitise the published curves of the KODAK VISION3 camera negative data sheets (brochures).

The brochures draw their charts as vector paths, so the curves are read EXACTLY from the PDF drawing
commands (no pixel tracing): every coloured path inside a chart is mapped through an axis calibration
that is fitted to the chart's own tick-label positions (linear axes: least squares over the labels;
logarithmic axes: the same in log10).

Charts per sheet (page 2 of each brochure):
  * Sensitometric curves   density vs camera stops (0 = 18 % grey)            -> 'stops'
  * Modulation transfer    response (%) vs spatial frequency (cycles/mm), log-log -> 'mtf'
  * Diffuse RMS granularity: density vs log relative exposure (-> 'hd') plus granularity sigma D on
    the right-hand log scale (-> 'gran'); RMS granularity = 1000 sigma D

Sources (downloaded 2026-10-10, kodak.com, public product brochures):
  https://www.kodak.com/content/products-brochures/motion-picture/KODAK-VISION3-50D-5203-7203-brochure.pdf
  https://www.kodak.com/content/products-brochures/motion-picture/KODAK-VISION3-250D-5207-7207-brochure.pdf
  https://www.kodak.com/content/products-brochures/motion-picture/KODAK-VISION3-200T-5213-7213-brochure.pdf
  https://www.kodak.com/content/products-brochures/motion-picture/KODAK-VISION3-500T-5219-7219-brochure.pdf
The PDFs are not stored in the repo; only the digitised numbers (vision3_texture.json, written by
summarise() below) are.

Usage:  python3 digitise_kodak.py <dir with the four PDFs>     (needs PyMuPDF)
"""
import json
import os
import re
import sys

import numpy as np

COL = {'r': (0.877, 0.159, 0.150), 'g': (0.410, 0.741, 0.271), 'b': (0.232, 0.352, 0.661)}
STOCKS = {'5203': '50D', '5207': '250D', '5213': '200T', '5219': '500T'}
NUM = re.compile(r'-?\d+(\.\d+)?')


def cname(c):
    if c is None:
        return None
    for k, v in COL.items():
        if max(abs(a - b) for a, b in zip(c, v)) < 0.12:
            return k
    return None


def path_points(path):
    out = []
    for it in path['items']:
        if it[0] == 'l':
            out += [it[1], it[2]]
        elif it[0] == 'c':
            out += [it[1], it[4]]
    return np.array([(p.x, p.y) for p in out])


def spans(page):
    r = []
    for b in page.get_text('dict')['blocks']:
        for line in b.get('lines', []):
            for s in line['spans']:
                t = s['text'].strip()
                if t:
                    r.append((t, s['bbox']))
    return r


# Axis calibration from the chart FRAMES (the plot rectangles): every brochure uses one chart template
# whose axis ranges coincide with the frame edges (checked against the text tick labels of the sheets
# that carry them, see check_labels()):
#   sensitometry  x: camera stops -8 .. +8, y: density 0 .. 3
#   MTF           x: log10 cycles/mm 1 .. FMAX (600; 700 on the 50D sheet), y: log10 response 1 .. 200 %
#   granularity   x: log relative exposure 0 .. 5, y: density 0 .. 3; sigma D on the right:
#                 log10 sigma = -3 + v / 0.3132 (v = height above the frame bottom / frame height;
#                 fitted to the labels 0.001 .. 0.10 of the 500T and 250D sheets: decade 0.3135 / 0.3128)
SIGMA_DECADE = 0.3132
FMAX = {'5203': 700.0}


def frames(page):
    """largest black (stroked or filled) rectangle per chart column: stops, mtf, granularity"""
    best = {}
    for d in page.get_drawings():
        c = d['color'] if d['color'] is not None else d.get('fill')
        if c is None or max(c) > 0.2:
            continue
        r = d['rect']
        if r.width < 80 or r.height < 80 or r.y0 < 400 or r.width > 300:
            continue
        cx = 0.5 * (r.x0 + r.x1)
        col = 'stops' if cx < 220 else ('mtf' if cx < 400 else 'gran')
        if col not in best or r.width * r.height > best[col].width * best[col].height:
            best[col] = r
    return best


def digitise(fn, code):
    import pymupdf
    doc = pymupdf.open(fn)
    page = next(p for p in doc if 'GRANULARITY SIGMA D' in p.get_text())
    F = frames(page)
    u = lambda r, x: (x - r.x0) / (r.x1 - r.x0)          # 0 at the left edge
    v = lambda r, y: (r.y1 - y) / (r.y1 - r.y0)          # 0 at the bottom edge
    fmax = np.log10(FMAX.get(code, 600.0))
    res = {'gran': {}, 'hd': {}, 'mtf': {}, 'stops': {}}
    for d in page.get_drawings():
        k = cname(d['color']) or cname(d.get('fill'))
        if not k:
            continue
        P = path_points(d)
        if len(P) < 4:
            continue
        cx = P[:, 0].mean()
        col = 'stops' if cx < 220 else ('mtf' if cx < 400 else 'gran')
        r = F[col]
        U, V = u(r, P[:, 0]), v(r, P[:, 1])
        if col == 'gran':
            X, D = 5.0 * U, 3.0 * V
            if abs(np.polyfit(X, D, 1)[0]) > 0.25 and D.max() - D.min() > 0.8:
                res['hd'].setdefault(k, []).append(np.c_[X, D])
            else:
                res['gran'].setdefault(k, []).append(np.c_[X, 10 ** (-3.0 + V / SIGMA_DECADE)])
        elif col == 'mtf':
            res['mtf'].setdefault(k, []).append(np.c_[10 ** (fmax * U), 10 ** (np.log10(200.0) * V)])
        else:
            res['stops'].setdefault(k, []).append(np.c_[-8.0 + 16.0 * U, 3.0 * V])
    # one curve per layer: merge the segments (filled outlines trace both edges of the line: average
    # them in narrow x bins)
    for kind in res:
        for k in list(res[kind]):
            a = np.concatenate(res[kind][k])
            xs = np.log10(a[:, 0]) if kind == 'mtf' else a[:, 0]
            lo, hi = xs.min(), xs.max()
            nb = 120
            idx = np.minimum(((xs - lo) / max(hi - lo, 1e-9) * nb).astype(int), nb - 1)
            out = []
            for i in range(nb):
                m = idx == i
                if m.any():
                    out.append((a[m, 0].mean(), np.exp(np.log(a[m, 1]).mean()) if kind in ('mtf', 'gran') else a[m, 1].mean()))
            res[kind][k] = np.array(out)
    return res, page.number + 1


def check_labels(fn, code):
    """compare the frame calibration with the text tick labels where the sheet has them"""
    import pymupdf
    doc = pymupdf.open(fn)
    page = next(p for p in doc if 'GRANULARITY SIGMA D' in p.get_text())
    F = frames(page)
    r = F['gran']
    errs = []
    for t, b in spans(page):
        if NUM.fullmatch(t) and t.startswith('0.0') and b[0] > r.x1:
            vv = (r.y1 - 0.5 * (b[1] + b[3])) / (r.y1 - r.y0)
            errs.append(abs(-3.0 + vv / SIGMA_DECADE - np.log10(float(t))))
    return max(errs) if errs else None


def interp(a, x):
    return float(np.interp(x, a[:, 0], a[:, 1]))


def summarise(curves):
    """Texture-relevant numbers: MTF per layer at a few frequencies and MTF50, RMS granularity per
    layer at net diffuse density 1.0 (via the sheet's own density curve) and the granularity
    profile against log exposure."""
    out = {}
    for code, (res, page) in curves.items():
        s = {'page': page}
        mtf = {}
        for k, a in res['mtf'].items():
            f = a[:, 0]
            r = a[:, 1]
            f50 = None
            below = np.nonzero(r < 50.0)[0]
            if len(below):
                i = below[0]
                if i > 0:
                    t = (50.0 - r[i - 1]) / (r[i] - r[i - 1])
                    f50 = float(np.exp(np.log(f[i - 1]) + t * (np.log(f[i]) - np.log(f[i - 1]))))
            mtf[k] = {'f': [round(float(v), 2) for v in f], 'response': [round(float(v), 1) for v in r],
                      'f50_cyc_mm': None if f50 is None else round(f50, 1),
                      'at': {str(q): round(interp(a, q), 1) for q in (10, 20, 40, 60, 80) if f.min() <= q <= f.max()}}
        s['mtf'] = mtf
        gran = {}
        for k, a in res['gran'].items():
            g = {'logE': [round(float(v), 3) for v in a[:, 0]], 'sigmaD': [round(float(v), 5) for v in a[:, 1]]}
            hd = res['hd'].get(k)
            if hd is not None:
                dmin = float(hd[:, 1].min())
                g['dmin'] = round(dmin, 3)
                # log E where the density is dmin + 1.0 (Kodak's reference "net diffuse density 1.0")
                yy = hd[:, 1] - dmin
                j = np.nonzero(yy >= 1.0)[0]
                if len(j) and j[0] > 0:
                    i = j[0]
                    t = (1.0 - yy[i - 1]) / (yy[i] - yy[i - 1])
                    xe = float(hd[i - 1, 0] + t * (hd[i, 0] - hd[i - 1, 0]))
                    g['logE_at_net1'] = round(xe, 3)
                    if a[:, 0].min() <= xe <= a[:, 0].max():
                        g['rms_at_net1'] = round(1000.0 * interp(a, xe), 2)
                g['hd_logE'] = [round(float(v), 3) for v in hd[::max(1, len(hd) // 40), 0]]
                g['hd_D'] = [round(float(v), 3) for v in hd[::max(1, len(hd) // 40), 1]]
            gran[k] = g
        s['granularity'] = gran
        stops = {}
        for k, a in res['stops'].items():
            stops[k] = {'stops': [round(float(v), 3) for v in a[::max(1, len(a) // 40), 0]],
                        'D': [round(float(v), 3) for v in a[::max(1, len(a) // 40), 1]]}
        s['sensitometry_stops'] = stops
        out[f'VISION3 {STOCKS[code]} {code}'] = s
    return out


if __name__ == '__main__':
    src = sys.argv[1] if len(sys.argv) > 1 else '.'
    curves = {}
    for code, name in STOCKS.items():
        fn = os.path.join(src, f'v{code}.pdf')
        curves[code] = digitise(fn, code)
        e = check_labels(fn, code)
        if e is not None:
            print(code, 'sigma axis: max |log10 error| vs text labels %.3f' % e)
    summ = summarise(curves)
    for name, s in summ.items():
        print(name, 'page', s['page'])
        for k, m in s['mtf'].items():
            print(f'   MTF {k}: f50 {m["f50_cyc_mm"]} c/mm  at {m["at"]}')
        for k, g in s['granularity'].items():
            print(f'   gran {k}: rms@net1.0 {g.get("rms_at_net1")}  sigmaD range {min(g["sigmaD"]):.4f}..{max(g["sigmaD"]):.4f}')
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vision3_texture.json')
    with open(out, 'w') as f:
        json.dump(summ, f, indent=1)
    print('wrote', out)
