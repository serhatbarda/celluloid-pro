"""Metrics on rendered images."""
import numpy as np

from . import colour as C


def finite_stats(img):
    a = np.asarray(img[..., :3], np.float64)
    return dict(nan=int(np.isnan(a).sum()), inf=int(np.isinf(a).sum()),
                min=float(np.nanmin(a)) if a.size else 0.0, max=float(np.nanmax(a)) if a.size else 0.0)


def wedge_metrics(chart, samples):
    idx = chart.group("wedge")
    stops = np.array([chart.patches[i]["stop"] for i in idx])
    v = samples[idx, :3]
    def at(s):
        return v[int(np.argmin(np.abs(stops - s)))]
    d = np.diff(v, axis=0)
    grey = at(0.0)
    m = dict(
        stops=stops.tolist(),
        values=v.tolist(),
        grey=grey.tolist(),
        grey_mean=float(np.mean(grey)),
        neutral=float(np.max(grey) - np.min(grey)),
        minus6=at(-6.0).tolist(),
        plus6=at(6.0).tolist(),
        plus8=at(8.0).tolist(),
        minus10=at(-10.0).tolist(),
        slope_7_8=(at(8.0) - at(7.0)).tolist(),
        min_step=float(np.min(d)),
        min_step_per_channel=np.min(d, axis=0).tolist(),
        max=float(np.max(v)),
        min=float(np.min(v)),
    )
    # first stop where any channel stops rising (hard clip detection)
    flat = np.where(np.any(d <= 1e-6, axis=1) & (stops[1:] > 0))[0]
    m["first_flat_stop"] = float(stops[1:][flat[0]]) if len(flat) else None
    return m


def monotonic(m, tol=1e-6):
    return m["min_step"] >= -tol


def output_lab(samples, out):
    """Encoded display RGB -> CIELAB relative to display white (D65 reference white)."""
    xyz = out.to_xyz(samples[..., :3])
    if out.hdr:
        xyz = xyz / 2.03   # HDR: reference white 203 nits (BT.2408) as L* = 100
    return C.xyz_to_lab(xyz)


def chart_labs(chart, samples, out, group="chart24"):
    idx = chart.group(group)
    return output_lab(samples[idx], out)


def skin_metrics(chart, samples, out):
    idx = chart.group("skin")
    lin = np.array([chart.patches[i]["lin"] for i in idx])
    lab_in = C.scene_lab_fgamutc(lin)
    lab_out = output_lab(samples[idx], out)
    lch_in, lch_out = C.lab_to_lch(lab_in), C.lab_to_lch(lab_out)
    dh = C.hue_diff(lch_in[:, 2], lch_out[:, 2])
    return dict(names=[chart.patches[i]["name"] for i in idx], hue_in=lch_in[:, 2].tolist(), hue_out=lch_out[:, 2].tolist(),
                hue_shift=dh.tolist(), max_abs_shift=float(np.max(np.abs(dh))), a_out=lab_out[:, 1].tolist(),
                b_out=lab_out[:, 2].tolist(), chroma_in=lch_in[:, 1].tolist(), chroma_out=lch_out[:, 1].tolist(),
                warm=bool(np.all(lab_out[:, 1] > 0) and np.all(lab_out[:, 2] > 0)))


def torture_metrics(chart, samples, out):
    res = dict(nan=0, inf=0, below0=0, above1=0, max_hue_jump=0.0, worst_hue=None, max_step=0.0, worst_step=None)
    for grp in ("torture", "sweep", "specials"):
        idx = chart.group(grp)
        v = samples[idx, :3]
        res["nan"] += int(np.isnan(v).sum())
        res["inf"] += int(np.isinf(v).sum())
        if not out.hdr:
            res["below0"] += int((v < -1e-6).sum())
            res["above1"] += int((v > 1.0 + 1e-6).sum())
    for grp in ("torture", "sweep"):
        idx = chart.group(grp)
        hues = sorted(set(chart.patches[i]["hue"] for i in idx))
        for h in hues:
            ii = [i for i in idx if chart.patches[i]["hue"] == h]
            v = np.nan_to_num(samples[ii, :3])
            lch = C.lab_to_lch(output_lab(v, out))
            steps = np.max(np.abs(np.diff(v, axis=0)), axis=1)
            k = int(np.argmax(steps))
            if steps[k] > res["max_step"]:
                res["max_step"] = float(steps[k])
                res["worst_step"] = "%s hue %d at %+.2f stops" % (grp, h, chart.patches[ii[k]]["stop"])
            ok = (lch[:-1, 1] > 8) & (lch[1:, 1] > 8)
            if ok.any():
                jumps = np.abs(C.hue_diff(lch[:-1, 2], lch[1:, 2]))[ok]
                j = float(np.max(jumps))
                if j > res["max_hue_jump"]:
                    res["max_hue_jump"] = j
                    res["worst_hue"] = "%s hue %d" % (grp, h)
    return res


def de_matrix(labs):
    names = list(labs)
    n = len(names)
    M = np.zeros((n, n))
    for i in range(n):
        for j in range(n):
            M[i, j] = float(np.mean(C.delta_e2000(labs[names[i]], labs[names[j]])))
    return names, M


def ring_energy(diff, cx, cy, r0, r1):
    h, w = diff.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    d = np.hypot(xx - cx, yy - cy)
    m = (d >= r0) & (d < r1)
    if not m.any():
        return np.zeros(3)
    return diff[m][:, :3].mean(axis=0)


def autocorr_length(noise):
    """Correlation length (pixels) of a 2-D zero-mean field: lag where the radially averaged
    autocorrelation falls below 0.5 (linear interpolation)."""
    a = noise - noise.mean()
    F = np.fft.rfft2(a)
    ac = np.fft.irfft2(F * np.conj(F), s=a.shape)
    ac = ac / ac[0, 0]
    h, w = a.shape
    lags = []
    for L in range(1, min(h, w) // 4):
        vals = [ac[0, L], ac[L, 0], ac[0, -L], ac[-L, 0]]
        lags.append(float(np.mean(vals)))
    prev = 1.0
    for i, v in enumerate(lags):
        if v < 0.5:
            L = i + 1
            return float(L - 1 + (prev - 0.5) / max(prev - v, 1e-9))
        prev = v
    return float(len(lags))


def channel_corr(noise):
    a = noise[..., :3].reshape(-1, 3)
    a = a - a.mean(axis=0)
    c = np.corrcoef(a.T)
    return dict(rg=float(c[0, 1]), rb=float(c[0, 2]), gb=float(c[1, 2]))


def estimate_shift(a, b):
    """Integer+subpixel (parabolic) translation of b relative to a via phase correlation."""
    A = np.fft.fft2(a - a.mean())
    B = np.fft.fft2(b - b.mean())
    R = A.conj() * B
    R /= np.maximum(np.abs(R), 1e-12)
    r = np.real(np.fft.ifft2(R))
    iy, ix = np.unravel_index(int(np.argmax(r)), r.shape)
    h, w = r.shape

    def sub(c, m, p):
        den = m - 2 * c + p
        return 0.0 if abs(den) < 1e-12 else 0.5 * (m - p) / den
    dy = iy + sub(r[iy, ix], r[(iy - 1) % h, ix], r[(iy + 1) % h, ix])
    dx = ix + sub(r[iy, ix], r[iy, (ix - 1) % w], r[iy, (ix + 1) % w])
    if dy > h / 2:
        dy -= h
    if dx > w / 2:
        dx -= w
    return float(dx), float(dy)


def downsample2(img):
    h, w = img.shape[0] // 2 * 2, img.shape[1] // 2 * 2
    a = img[:h, :w].astype(np.float64)
    return a.reshape(h // 2, 2, w // 2, 2, -1).mean(axis=(1, 3))


def edge_contrast(img, y0, y1, xe, half):
    """Mean luminance difference across a vertical edge at column xe within +-half pixels."""
    lum = img[y0:y1, :, :3].mean(axis=2)
    left = lum[:, max(0, xe - half):xe - 1].mean()
    right = lum[:, xe + 1:xe + half].mean()
    grad = np.abs(np.diff(lum[:, xe - half:xe + half], axis=1)).max(axis=1).mean()
    return float(right - left), float(grad)
