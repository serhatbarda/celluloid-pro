"""Trace curves in raster data-sheet graphs (black lines on white, design-time tool).

The axis frame is found automatically (longest dark rows / columns), gridlines (rows / columns that
are dark over most of the frame) are masked, and each curve is followed column by column from a
seed point: in every column the dark-run centre nearest to the linear prediction from the last
points is taken (within a jump limit), so curves are followed through crossings by their slope.
Short gaps (dashes, crossings, labels) are bridged by the prediction. Needs Pillow.
"""
import numpy as np


def load_gray(path):
    from PIL import Image
    return np.asarray(Image.open(path).convert('L'), np.float64)


def find_frame(g, thr=110, frac=0.55):
    d = g < thr
    H, W = d.shape
    rows = np.where(d.mean(1) > frac)[0]
    cols = np.where(d.mean(0) > frac * H / W * 0 + 0.5 * (len(rows) and 1))[0]
    # frame columns: dark over most of the span between the frame rows
    y0, y1 = rows.min(), rows.max()
    cf = d[y0:y1 + 1].mean(0)
    cols = np.where(cf > 0.85)[0]
    x0, x1 = cols.min(), cols.max()
    return int(x0), int(y0), int(x1), int(y1)


def grid_mask(d, frame, frac=0.6):
    x0, y0, x1, y1 = frame
    m = np.zeros_like(d)
    sub = d[y0:y1 + 1, x0:x1 + 1]
    gr = np.where(sub.mean(1) > frac)[0] + y0
    gc = np.where(sub.mean(0) > frac)[0] + x0
    for r in gr:
        m[max(r - 1, 0):r + 2, :] = True
    for c in gc:
        m[:, max(c - 1, 0):c + 2] = True
    return m, gr, gc


def runs(col):
    """centres and lengths of dark runs in a boolean column"""
    idx = np.where(col)[0]
    if len(idx) == 0:
        return np.zeros(0), np.zeros(0)
    br = np.where(np.diff(idx) > 1)[0]
    starts = np.r_[idx[0], idx[br + 1]]
    ends = np.r_[idx[br], idx[-1]]
    return (starts + ends) / 2.0, (ends - starts + 1).astype(float)


def trace(g, frame, seed_px, xrange_px, thr=110, maxjump=6.0, maxrun=40, gridmask=True, direction=(1, -1), hist=6, snap=25):
    d = g < thr
    x0, y0, x1, y1 = frame
    if gridmask:
        gm, _, _ = grid_mask(d, frame)
        dd = d & ~gm
    else:
        dd = d
    out = {}
    sx, sy = seed_px
    c, L = runs(dd[y0 + 1:y1, sx])          # snap the seed to the nearest line in its column
    if len(c):
        c = c + y0 + 1
        j = int(np.argmin(abs(c - sy)))
        if abs(c[j] - sy) < snap:
            sy = c[j]
    for step in direction:
        xs, ys = [sx], [sy]
        x = sx + step
        miss = 0
        while xrange_px[0] <= x <= xrange_px[1] and x0 < x < x1 and miss < 25:
            if len(xs) >= 3:
                k = min(hist, len(xs))
                p = np.polyfit(np.array(xs[-k:], float), np.array(ys[-k:], float), 1)
                pred = np.polyval(p, x)
            else:
                pred = ys[-1]
            c, L = runs(dd[y0 + 1:y1, x])
            c = c + y0 + 1
            ok = L <= maxrun
            c, L = c[ok], L[ok]
            if len(c):
                j = int(np.argmin(abs(c - pred)))
                lim = maxjump + 0.5 * L[j]
                if abs(c[j] - pred) <= lim:
                    xs.append(x)
                    ys.append(c[j])
                    miss = 0
                else:
                    miss += 1
            else:
                miss += 1
            x += step
        out[step] = (xs, ys)
    xa = out[-1][0][::-1][:-1] + out[1][0] if -1 in out else out[1][0]
    ya = out[-1][1][::-1][:-1] + out[1][1] if -1 in out else out[1][1]
    return np.array(xa, float), np.array(ya, float)


def trace_guided(g, frame, guide_px, thr=110, tol=7.0, maxrun=60, gridmask=True):
    """Follow a curve along a hand-placed polyline of waypoints (pixels): in every column the dark-run
    centre nearest to the polyline within `tol` px is taken (so the result sits on the printed line,
    not on the waypoints); columns without a line there (labels, crossings, vertical drops) are filled
    from the polyline. Returns xs, ys, snapped-flag."""
    d = g < thr
    x0, y0, x1, y1 = frame
    if gridmask:
        gm, _, _ = grid_mask(d, frame)
        d = d & ~gm
    gx = np.array([p[0] for p in guide_px], float)
    gy = np.array([p[1] for p in guide_px], float)
    xs, ys, ok = [], [], []
    for x in range(int(np.ceil(gx.min())), int(np.floor(gx.max())) + 1):
        if not (x0 < x < x1):
            continue
        pred = float(np.interp(x, gx, gy))
        c, L = runs(d[y0 + 1:y1, x])
        c = c + y0 + 1
        m = L <= maxrun
        c = c[m]
        if len(c) and np.min(abs(c - pred)) <= tol:
            ys.append(float(c[np.argmin(abs(c - pred))]))
            ok.append(True)
        else:
            ys.append(pred)
            ok.append(False)
        xs.append(float(x))
    # steep segments (|slope| > 3 px / px) are taken from the polyline only between snapped points
    return np.array(xs), np.array(ys), np.array(ok)


def px_to_data(xs, ys, frame, axes):
    x0, y0, x1, y1 = frame
    X = axes['x'][0] + (xs - x0) / (x1 - x0) * (axes['x'][1] - axes['x'][0])
    t = (y1 - ys) / (y1 - y0)
    Y = axes['y'][0] + t * (axes['y'][1] - axes['y'][0])
    return X, Y


def data_to_px(X, Y, frame, axes):
    x0, y0, x1, y1 = frame
    xs = x0 + (X - axes['x'][0]) / (axes['x'][1] - axes['x'][0]) * (x1 - x0)
    ys = y1 - (Y - axes['y'][0]) / (axes['y'][1] - axes['y'][0]) * (y1 - y0)
    return xs, ys
