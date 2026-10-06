"""Generate reference/golden/core_golden.txt: NumPy-model outputs that tests/core compares the
C++ implementation (same bakes, float32 kernels) against.

    python3 reference/golden.py

Run after any change of the model or of reference/stocks.json (and gen_tables.py).
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_tables  # noqa: E402
import model as m  # noqa: E402
import scenes  # noqa: E402
import spectral as sp  # noqa: E402

OUT = os.path.join(sp.HERE, 'golden', 'core_golden.txt')


def cases(lib):
    C = []
    nn, npr = len(lib.negs), len(lib.prints)
    for i in range(nn):
        C.append(dict(neg=i))
    for j in range(npr):
        C.append(dict(neg=1, prt=j))
    for i in (1, 6, 9):
        C.append(dict(neg=i, process='scan'))
    C += [
        dict(neg=1, push=2.0, interlayer=2.0, neutralize=0.5),
        dict(neg=1, push=-1.5, negBleach=0.8, printBleach=0.6),
        dict(neg=4, printer=(3.0, -2.0, 1.5), printExposure=2.0),
        dict(neg=1, process='scan', printer=(2.0, 0.0, -1.0), printExposure=-1.0),
        dict(neg=8, push=1.0, neutralize=1.0, negBleach=0.5),
        dict(neg=3, prt=3, grain=1.5, push=1.0),
        dict(neg=9, prt=3, grain=1.0),
        dict(neg=1, exposure=1.5, interlayer=0.0),
    ]
    for o in m.OUTPUTS[1:]:
        C.append(dict(neg=1, output=o))
    C.append(dict(neg=1, output='Rec2100_PQ', hdrPeak=4000.0, hdrUnroll=0.5))
    C.append(dict(neg=1, output='Rec2100_PQ', hdrPeak=300.0, hdrUnroll=1.0))
    C.append(dict(neg=8, output='Rec2100_HLG'))
    for inp in m.INPUTS[1:]:
        C.append(dict(neg=1, input=inp))
    return C


def scene_pixels(rng):
    chart = scenes.chart_rgb()
    stops = np.repeat((0.18 * 2.0 ** np.arange(-10, 10.5, 1.0))[:, None], 3, 1)
    sat = scenes.hsv_sweep(24, 1.0, 0.18) * 2.0
    sat2 = scenes.hsv_sweep(12, 1.3, 0.5)
    rnd = rng.uniform(-0.05, 1.5, (24, 3)) * rng.choice([0.1, 1.0, 4.0], (24, 1))
    return np.concatenate([chart, chart * 4.0, chart * 0.25, stops, sat, sat2, rnd], 0)


def main():
    lib = m.load()
    rng = np.random.default_rng(1234)
    import json
    h = gen_tables.stock_hash(json.load(open(os.path.join(sp.HERE, 'stocks.json'))))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    lines = [f'hash {h}']
    scene = scene_pixels(rng)
    for ci, c in enumerate(cases(lib)):
        p = m.Params(**c)
        P = m.Pipeline(lib).prepare(p)
        enc = m.encode_transfer(p.input, scene @ np.linalg.inv(P.Min).T)
        enc = np.concatenate([enc, np.array([[np.nan, 0.5, 0.5], [1e6, -1e6, 0.3], [-5.0, 0.0, 2.0], [0, 0, 0]])], 0)
        noise = rng.standard_normal(enc.shape) if p.grain > 0 else np.zeros_like(enc)
        out = P.process(enc, noise if p.grain > 0 else None)
        pr = p.printer
        lines.append('case c%02d neg=%d prt=%d process=%d input=%d output=%d exposure=%r push=%r inter=%r nb=%r neut=%r '
                     'pr=%r pg=%r pb=%r pe=%r pbl=%r peak=%r unroll=%r grain=%r' % (
                         ci, p.neg, p.prt, 1 if p.process == 'scan' else 0, m.INPUTS.index(p.input),
                         m.OUTPUTS.index(p.output), p.exposure, p.push, p.interlayer, p.negBleach, p.neutralize,
                         pr[0], pr[1], pr[2], p.printExposure, p.printBleach, p.hdrPeak, p.hdrUnroll, p.grain))
        for e, n, o in zip(enc, noise, out):
            lines.append('px ' + ' '.join('%.9g' % v for v in list(e) + list(n) + list(o)))
        if ci in (0, 1) or 'output' in c:
            dv = rng.uniform(0.0, 1.0, (12, 3))
            dl = P.display_decode(dv)
            lines += ['dd ' + ' '.join('%.9g' % v for v in list(a) + list(b)) for a, b in zip(dv, dl)]
            nz = rng.standard_normal(dl.shape)
            P.txg = m.f32(2.5 * (np.array(lib.negs[p.neg]['granularity'][:1] * 3 if P.mono else lib.negs[p.neg]['granularity']) * 1.0).mean())
            df = P.display_finish(dl, nz)
            lines.append('txg %.9g' % P.txg)
            lines += ['df ' + ' '.join('%.9g' % v for v in list(a) + list(b) + list(q)) for a, b, q in zip(dl, nz, df)]
    with open(OUT, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print('wrote', OUT, len(lines), 'lines')


if __name__ == '__main__':
    main()
