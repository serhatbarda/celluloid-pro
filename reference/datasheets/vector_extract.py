"""Digitise curves from manufacturer data-sheet PDFs whose graphs are vector drawings (design-time tool).

    python3 reference/datasheets/vector_extract.py list  <pdf> <page>              # drawings + frames
    python3 reference/datasheets/vector_extract.py curve <pdf> <page> <spec.json>  # -> CSVs

The graph frame (axis rectangle in PDF points) and the axis values at its edges come from the
spec; every path segment of the selected drawings is converted into data coordinates (Bezier
segments are sampled), points of one curve are merged, sorted by x and de-duplicated. No smoothing
or fitting happens here (reference/datasheets/fit_stocks.py does that). Needs PyMuPDF.
"""
import json
import sys

import numpy as np


def drawings(pdf, page):
    import pymupdf
    d = pymupdf.open(pdf)
    return d[page].get_drawings()


def path_points(dr, nb=12):
    pts = []
    for it in dr['items']:
        if it[0] == 'l':
            pts.append([(it[1].x, it[1].y), (it[2].x, it[2].y)])
        elif it[0] == 'c':
            p0, p1, p2, p3 = [np.array([p.x, p.y]) for p in it[1:5]]
            t = np.linspace(0, 1, nb)[:, None]
            pts.append([tuple(v) for v in ((1 - t) ** 3 * p0 + 3 * (1 - t) ** 2 * t * p1 + 3 * (1 - t) * t * t * p2 + t ** 3 * p3)])
    return pts


def cmd_list(pdf, page):
    for i, dr in enumerate(drawings(pdf, page)):
        n = len(dr['items'])
        r = dr['rect']
        print(i, dr.get('type'), 'col', tuple(round(c, 2) for c in (dr.get('color') or ())), 'fill', tuple(round(c, 2) for c in (dr.get('fill') or ())),
              'w', round(dr.get('width') or 0, 2), 'dash', dr.get('dashes'), 'n', n, 'kinds', ''.join(sorted(set(it[0] for it in dr['items']))),
              'rect', [round(v, 1) for v in r])


def to_data(xy, fr, ax):
    x0, y0, x1, y1 = fr                      # frame in PDF points (y down)
    X = ax['x'][0] + (xy[:, 0] - x0) / (x1 - x0) * (ax['x'][1] - ax['x'][0])
    t = (y1 - xy[:, 1]) / (y1 - y0)
    if ax.get('ylog'):
        Y = 10 ** (np.log10(ax['y'][0]) + t * (np.log10(ax['y'][1]) - np.log10(ax['y'][0])))
    else:
        Y = ax['y'][0] + t * (ax['y'][1] - ax['y'][0])
    return np.stack([X, Y], 1)


def cmd_curve(pdf, page, spec):
    S = json.load(open(spec))
    D = drawings(pdf, page)
    for g in S['graphs']:
        for name, idx in g['curves'].items():
            pts = []
            for i in (idx if isinstance(idx, list) else [idx]):
                for seg in path_points(D[i]):
                    pts += seg
            xy = to_data(np.array(pts), g['frame'], g['axes'])
            xr = g.get('xclip')
            if xr:
                xy = xy[(xy[:, 0] >= xr[0]) & (xy[:, 0] <= xr[1])]
            xy = xy[np.argsort(xy[:, 0], kind='stable')]
            out = g['out'].format(curve=name)
            np.savetxt(out, xy, fmt='%.5f', delimiter=',', header='x,y  (%s p.%d, %s)' % (pdf.split('/')[-1], page + 1, g.get('title', '')))
            print(out, len(xy), 'x %.2f..%.2f y %.3f..%.3f' % (xy[:, 0].min(), xy[:, 0].max(), xy[:, 1].min(), xy[:, 1].max()))


if __name__ == '__main__':
    if sys.argv[1] == 'list':
        cmd_list(sys.argv[2], int(sys.argv[3]))
    else:
        cmd_curve(sys.argv[2], int(sys.argv[3]), sys.argv[4])
