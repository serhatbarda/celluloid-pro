"""Stock design / analysis harness (NumPy reference model).

    python3 reference/design.py metrics [neg] [print]      # SPEC section 6 numbers
    python3 reference/design.py render  <neg> <print> [process] [out.png]
    python3 reference/design.py sheet   [outdir]            # contact sheets of all stocks
    python3 reference/design.py deltae                      # Delta E 2000 distinctness tables
    python3 reference/design.py gates                       # distinctness gates (exit 1 on failure)

Scenes are generated in scene-linear Rec.2020 and encoded to Fujifilm F-Log2 C / F-Gamut C,
then run through the full model (decode -> film -> Rec.709 gamma 2.4) exactly like footage.
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import model as m  # noqa: E402
import scenes  # noqa: E402
import spectral as sp  # noqa: E402

import tempfile  # noqa: E402
OUT = os.path.join(tempfile.gettempdir(), 'celluloid_renders')


def to_flog2c(rgb2020):
    M = np.linalg.inv(m.input_matrix('FLog2C'))
    return m.encode_transfer('FLog2C', rgb2020 @ M.T)


def run(lib, params, rgb2020, noise=None):
    P = m.Pipeline(lib).prepare(params)
    enc = to_flog2c(rgb2020.reshape(-1, 3))
    return P.process(enc, noise).reshape(rgb2020.shape), P


def display_to_lab(v, output='Rec709_G24'):
    """Encoded SDR output -> CIELAB (D65)."""
    if output == 'Rec709_G24':
        lin = np.sign(v) * np.abs(v) ** 2.4
    else:
        raise ValueError(output)
    X = lin @ sp.npm(*sp.PRIMARIES['rec709']).T
    return scenes.xyz_to_lab(X)


def ciede2000(lab1, lab2):
    L1, a1, b1 = lab1[..., 0], lab1[..., 1], lab1[..., 2]
    L2, a2, b2 = lab2[..., 0], lab2[..., 1], lab2[..., 2]
    C1 = np.hypot(a1, b1)
    C2 = np.hypot(a2, b2)
    Cb = (C1 + C2) / 2
    G = 0.5 * (1 - np.sqrt(Cb ** 7 / (Cb ** 7 + 25.0 ** 7)))
    a1p, a2p = (1 + G) * a1, (1 + G) * a2
    C1p, C2p = np.hypot(a1p, b1), np.hypot(a2p, b2)
    h1p = np.degrees(np.arctan2(b1, a1p)) % 360
    h2p = np.degrees(np.arctan2(b2, a2p)) % 360
    dLp = L2 - L1
    dCp = C2p - C1p
    dh = h2p - h1p
    dh = np.where(C1p * C2p == 0, 0, np.where(dh > 180, dh - 360, np.where(dh < -180, dh + 360, dh)))
    dHp = 2 * np.sqrt(C1p * C2p) * np.sin(np.radians(dh) / 2)
    Lbp = (L1 + L2) / 2
    Cbp = (C1p + C2p) / 2
    hs = h1p + h2p
    hbp = np.where(C1p * C2p == 0, hs, np.where(np.abs(h1p - h2p) > 180, np.where(hs < 360, (hs + 360) / 2, (hs - 360) / 2), hs / 2))
    T = 1 - 0.17 * np.cos(np.radians(hbp - 30)) + 0.24 * np.cos(np.radians(2 * hbp)) + 0.32 * np.cos(np.radians(3 * hbp + 6)) - 0.20 * np.cos(np.radians(4 * hbp - 63))
    dth = 30 * np.exp(-((hbp - 275) / 25) ** 2)
    Rc = 2 * np.sqrt(Cbp ** 7 / (Cbp ** 7 + 25.0 ** 7))
    Sl = 1 + 0.015 * (Lbp - 50) ** 2 / np.sqrt(20 + (Lbp - 50) ** 2)
    Sc = 1 + 0.045 * Cbp
    Sh = 1 + 0.015 * Cbp * T
    Rt = -np.sin(np.radians(2 * dth)) * Rc
    return np.sqrt((dLp / Sl) ** 2 + (dCp / Sc) ** 2 + (dHp / Sh) ** 2 + Rt * (dCp / Sc) * (dHp / Sh))


def metrics(lib, neg=1, prt=0, process='print', verbose=True):
    p = m.Params(neg=neg, prt=prt, process=process)
    stops = np.arange(-10, 10.01, 1.0)
    grey = np.repeat((0.18 * 2.0 ** stops)[:, None], 3, 1)
    out, P = run(lib, p, grey)
    res = {}
    g = out[10]
    res['grey'] = g.tolist()
    res['grey_neutral'] = float(g.max() - g.min())
    res['m6'] = out[4].tolist()
    res['p6'] = out[16].tolist()
    res['p8'] = out[18].tolist()
    res['p10'] = out[20].tolist()
    fine = np.repeat((0.18 * 2.0 ** np.linspace(-10, 10, 2001))[:, None], 3, 1)
    of, _ = run(lib, p, fine)
    d = np.diff(of, axis=0)
    res['monotonic'] = bool((d >= -1e-7).all())
    res['max_out'] = float(of.max())
    # local slope (encoded per stop) near +8 (no hard clip: slope must still be > 0)
    i8 = np.argmin(np.abs(np.linspace(-10, 10, 2001) - 8))
    res['slope_p8'] = float((of[i8 + 10, 1] - of[i8 - 10, 1]) / 0.2)
    # skin hue
    sk = scenes.skin_rgb()
    so, _ = run(lib, p, sk)
    lab_in = scenes.xyz_to_lab(sk @ sp.M2020_TO_XYZ.T)
    lab_out = display_to_lab(so)
    hin = np.degrees(np.arctan2(lab_in[:, 2], lab_in[:, 1]))
    hout = np.degrees(np.arctan2(lab_out[:, 2], lab_out[:, 1]))
    res['skin_dh'] = ((hout - hin + 180) % 360 - 180).tolist()
    res['skin_C'] = [float(np.hypot(*lab_in[i, 1:])) for i in range(len(sk))], [float(np.hypot(*lab_out[i, 1:])) for i in range(len(sk))]
    if verbose:
        nm = lib.negs[neg]['name'] + ' / ' + lib.prints[prt]['name'] + ' / ' + process
        print(nm)
        print('  grey %.4f %.4f %.4f  (neutral %.5f)' % (*g, res['grey_neutral']))
        print('  -6: %s  +6: %s  +8: %s  +10: %s' % tuple(np.round(np.array(res[k]), 3).tolist() for k in ('m6', 'p6', 'p8', 'p10')))
        print('  monotonic', res['monotonic'], ' max', round(res['max_out'], 4), ' slope@+8', round(res['slope_p8'], 4))
        print('  ramp G ', np.round(out[:, 1], 3).tolist())
        print('  skin dh', np.round(res['skin_dh'], 1).tolist(), ' C in/out', np.round(res['skin_C'][0], 1).tolist(), np.round(res['skin_C'][1], 1).tolist())
    return res


def render(lib, neg, prt, process='print', path=None, W=960, label=True, **kw):
    img, _ = scenes.test_image(W)
    p = m.Params(neg=neg, prt=prt, process=process, **kw)
    out, P = run(lib, p, img)
    a = (np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8)
    im = Image.fromarray(a)
    if label:
        dr = ImageDraw.Draw(im)
        txt = lib.negs[neg]['name'] + ' | ' + (lib.prints[prt]['name'] if process == 'print' else 'Lab Scan')
        dr.rectangle([0, 0, 8 * len(txt) + 8, 14], fill=(0, 0, 0))
        dr.text((4, 2), txt, fill=(255, 255, 255))
    if path:
        im.save(path)
    return im


def sheet(lib, outdir=OUT):
    os.makedirs(outdir, exist_ok=True)
    for i in range(len(lib.negs)):
        render(lib, i, 0, path=os.path.join(outdir, f'neg{i:02d}_P1.png'))
    for j in range(len(lib.prints)):
        render(lib, 1, j, path=os.path.join(outdir, f'CN200T_P{j + 1}.png'))
    render(lib, 1, 0, process='scan', path=os.path.join(outdir, 'CN200T_scan.png'))


def deltae(lib, verbose=True):
    chart = scenes.chart_rgb()
    labs = []
    names = []
    for i in range(len(lib.negs)):
        o, _ = run(lib, m.Params(neg=i, prt=0), chart)
        labs.append(display_to_lab(o))
        names.append(lib.negs[i]['name'])
    n = len(labs)
    T = np.zeros((n, n))
    for a in range(n):
        for b in range(n):
            T[a, b] = ciede2000(labs[a], labs[b]).mean()
    if verbose:
        print('mean dE2000 over the 24-patch chart (negatives on P1):')
        for a in range(n):
            print('%-26s ' % names[a] + ' '.join('%5.1f' % v for v in T[a]))
    labs2 = []
    for j in range(len(lib.prints)):
        o, _ = run(lib, m.Params(neg=1, prt=j), chart)
        labs2.append(display_to_lab(o))
    n2 = len(labs2)
    T2 = np.zeros((n2, n2))
    for a in range(n2):
        for b in range(n2):
            T2[a, b] = ciede2000(labs2[a], labs2[b]).mean()
    if verbose:
        print('prints with CN 200T:')
        for a in range(n2):
            print('%-26s ' % lib.prints[a]['name'] + ' '.join('%5.1f' % v for v in T2[a]))
    return names, T, T2


def scan_deltae(lib):
    """mean dE2000 (24-patch chart + skin) between the colour negatives through the Lab Scan"""
    pat = np.concatenate([scenes.chart_rgb(), scenes.skin_rgb()])
    idx = [i for i, n in enumerate(lib.negs) if n['kind'] == 'negative']
    labs = {}
    for i in idx:
        o, _ = run(lib, m.Params(neg=i, process='scan'), pat)
        labs[i] = display_to_lab(o)
    T = {(a, b): float(ciede2000(labs[a], labs[b]).mean()) for a in idx for b in idx if a < b}
    return T


def gates(lib, limit=2.0):
    """Every negative distinct from every other on P1, every print distinct with CN 200T, and
    every colour negative distinct through the Lab Scan: min mean dE2000 >= limit."""
    names, T, T2 = deltae(lib, verbose=False)
    fails = []
    n = len(names)
    for a in range(n):
        for b in range(a + 1, n):
            if T[a, b] < limit:
                fails.append('P1: %s vs %s %.2f' % (names[a], names[b], T[a, b]))
    for a in range(len(T2)):
        for b in range(a + 1, len(T2)):
            if T2[a, b] < limit:
                fails.append('prints: %s vs %s %.2f' % (lib.prints[a]['name'], lib.prints[b]['name'], T2[a, b]))
    S = scan_deltae(lib)
    for (a, b), v in S.items():
        if v < limit:
            fails.append('Lab Scan: %s vs %s %.2f' % (lib.negs[a]['name'], lib.negs[b]['name'], v))
    tri = lambda M: min(M[a, b] for a in range(len(M)) for b in range(a + 1, len(M)))
    print('min mean dE2000: negatives on P1 %.2f, prints (CN 200T) %.2f, negatives through the Lab Scan %.2f'
          % (tri(T), tri(T2), min(S.values())))
    for f in fails:
        print('  FAIL', f)
    return fails


def main():
    lib = m.load()
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'metrics'
    if cmd == 'metrics':
        neg = int(sys.argv[2]) if len(sys.argv) > 2 else 1
        prt = int(sys.argv[3]) if len(sys.argv) > 3 else 0
        proc = sys.argv[4] if len(sys.argv) > 4 else 'print'
        metrics(lib, neg, prt, proc)
    elif cmd == 'render':
        neg, prt = int(sys.argv[2]), int(sys.argv[3])
        proc = sys.argv[4] if len(sys.argv) > 4 else 'print'
        path = sys.argv[5] if len(sys.argv) > 5 else os.path.join(OUT, f'r_{neg}_{prt}_{proc}.png')
        os.makedirs(os.path.dirname(path), exist_ok=True)
        render(lib, neg, prt, proc, path)
        print(path)
    elif cmd == 'sheet':
        sheet(lib, sys.argv[2] if len(sys.argv) > 2 else OUT)
    elif cmd == 'deltae':
        deltae(lib)
    elif cmd == 'gates':
        sys.exit(1 if gates(lib) else 0)


if __name__ == '__main__':
    main()
