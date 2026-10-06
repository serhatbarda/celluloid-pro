"""Adobe/Resolve .cube 3D LUT parsing and trilinear evaluation."""
import numpy as np


class Cube:
    def __init__(self, size, table, dmin=(0.0, 0.0, 0.0), dmax=(1.0, 1.0, 1.0), title=""):
        self.size = size
        self.table = table          # (size^3, 3), r fastest
        self.dmin = np.asarray(dmin, np.float64)
        self.dmax = np.asarray(dmax, np.float64)
        self.title = title

    def grid(self):
        return self.table.reshape(self.size, self.size, self.size, 3)   # [b, g, r]

    def apply(self, rgb):
        """Trilinear interpolation of input code values (N,3)."""
        rgb = np.asarray(rgb, np.float64)
        n = self.size
        t = (rgb - self.dmin) / (self.dmax - self.dmin) * (n - 1)
        t = np.clip(t, 0, n - 1)
        i0 = np.minimum(np.floor(t).astype(int), n - 2)
        f = t - i0
        g = self.grid()
        out = np.zeros_like(rgb)
        for db in (0, 1):
            for dg in (0, 1):
                for dr in (0, 1):
                    w = ((f[:, 2] if db else 1 - f[:, 2]) * (f[:, 1] if dg else 1 - f[:, 1]) *
                         (f[:, 0] if dr else 1 - f[:, 0]))
                    out += w[:, None] * g[i0[:, 2] + db, i0[:, 1] + dg, i0[:, 0] + dr]
        return out


def parse_cube(path):
    size = None
    title = ""
    dmin, dmax = (0.0, 0.0, 0.0), (1.0, 1.0, 1.0)
    rows = []
    with open(path) as f:
        for ln, line in enumerate(f, 1):
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            key = s.split()[0].upper()
            if key == "TITLE":
                title = s[5:].strip().strip('"')
            elif key == "LUT_3D_SIZE":
                size = int(s.split()[1])
            elif key == "LUT_1D_SIZE":
                raise ValueError("%s: 1D LUT, expected 3D" % path)
            elif key == "DOMAIN_MIN":
                dmin = tuple(float(x) for x in s.split()[1:4])
            elif key == "DOMAIN_MAX":
                dmax = tuple(float(x) for x in s.split()[1:4])
            elif key[0].isdigit() or key[0] in "-+.":
                vals = s.split()
                if len(vals) != 3:
                    raise ValueError("%s:%d: expected 3 values" % (path, ln))
                rows.append([float(v) for v in vals])
            else:
                raise ValueError("%s:%d: unknown keyword %s" % (path, ln, key))
    if size is None:
        raise ValueError("%s: no LUT_3D_SIZE" % path)
    if len(rows) != size ** 3:
        raise ValueError("%s: %d entries, expected %d" % (path, len(rows), size ** 3))
    return Cube(size, np.array(rows, np.float64), dmin, dmax, title)
