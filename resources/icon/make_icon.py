#!/usr/bin/env python3
"""Generate the Reafilm Pro plugin icon (128x128 RGBA PNG) from scratch.

Original artwork, computed procedurally: a short strip of motion-picture film with one frame
showing a low sun with its red halation over teal shadows; the frame edge is drawn in the cyan,
magenta and yellow dye layers of colour film. Pure Python standard library (zlib + struct), no third-party packages, so
it runs anywhere. The generated PNG is committed in resources/; re-run this script only when the
artwork changes:

    python3 resources/icon/make_icon.py resources/de.serhatbarda.CelluloidPro.png [size]
"""
import math
import struct
import sys
import zlib

SS = 4  # supersampling per axis (16 samples per pixel)


def lerp(a, b, t):
    return a + (b - a) * t


def mix3(c0, c1, t):
    return (lerp(c0[0], c1[0], t), lerp(c0[1], c1[1], t), lerp(c0[2], c1[2], t))


def smooth(e0, e1, x):
    t = min(1.0, max(0.0, (x - e0) / (e1 - e0)))
    return t * t * (3.0 - 2.0 * t)


def rounded_rect_sd(px, py, cx, cy, hw, hh, r):
    """Signed distance to a rounded rectangle centred at (cx, cy)."""
    qx = abs(px - cx) - (hw - r)
    qy = abs(py - cy) - (hh - r)
    ox, oy = max(qx, 0.0), max(qy, 0.0)
    return math.hypot(ox, oy) + min(max(qx, qy), 0.0) - r


def polygon_sd(px, py, n, radius, rot):
    """Signed distance (approx.) to a regular n-gon of circumradius `radius` at the origin."""
    ang = math.atan2(py, px) - rot
    seg = 2.0 * math.pi / n
    a = (ang % seg) - seg * 0.5
    r = math.hypot(px, py)
    apothem = radius * math.cos(math.pi / n)
    return r * math.cos(a) - apothem


# palette (sRGB values 0..1)
BG_TOP = (0.090, 0.086, 0.092)
BG_BOT = (0.030, 0.029, 0.034)
STRIP = (0.060, 0.057, 0.060)          # film strip, a shade lighter than the plate
HOLE = (0.012, 0.011, 0.013)
SKY_TOP = (0.040, 0.105, 0.130)        # deep teal shadows at the top of the frame
SKY_MID = (0.560, 0.230, 0.120)
SUN_CORE = (1.000, 0.960, 0.840)
SUN_MID = (1.000, 0.700, 0.300)
HALATION = (0.900, 0.160, 0.060)       # the red ring film draws around bright light
DYE_C = (0.000, 0.680, 0.800)          # the three dye layers of colour film
DYE_M = (0.880, 0.120, 0.520)
DYE_Y = (1.000, 0.800, 0.060)


def frame_sd(u, v, dx=0.0, dy=0.0):
    return rounded_rect_sd(u, v, 0.5 + dx, 0.5 + dy, 0.335, 0.235, 0.040)


def shade(u, v):
    """Colour + alpha of the icon at normalised coordinates u, v in [0, 1] (v down).

    A short strip of film with one picture frame: a low sun with its red halation over teal
    shadows, and the frame edge printed three times, slightly offset, in the cyan, magenta and
    yellow dye layers of colour film.
    """
    d_plate = rounded_rect_sd(u, v, 0.5, 0.5, 0.48, 0.48, 0.12)
    if d_plate > 0.0:
        return (0.0, 0.0, 0.0, 0.0)
    col = mix3(BG_TOP, BG_BOT, v)

    # horizontal film strip with sprocket holes above and below the frame
    if abs(v - 0.5) < 0.375:
        col = mix3(STRIP, col, smooth(0.365, 0.375, abs(v - 0.5)))
        pitch = 0.112
        k = round((u - 0.5 - pitch / 2) / pitch)
        for side in (-1.0, 1.0):
            if rounded_rect_sd(u, v, 0.5 + pitch / 2 + k * pitch, 0.5 + side * 0.31, 0.026, 0.022, 0.008) < 0.0:
                col = HOLE

    # dye-layer fringes: the frame edge in C, M and Y, offset like misregistered layers
    for dye, dx, dy in ((DYE_C, -0.013, -0.010), (DYE_M, 0.013, 0.010), (DYE_Y, 0.0, 0.0)):
        d = frame_sd(u, v, dx, dy)
        if abs(d) < 0.008:
            col = mix3(col, dye, 0.95 * (1.0 - smooth(0.004, 0.008, abs(d))))

    d_frame = frame_sd(u, v)
    if d_frame < -0.008:
        # picture: teal shadows at the top warming towards the horizon
        t = smooth(0.27, 0.66, v)
        pic = mix3(SKY_TOP, SKY_MID, t)
        # dark ground below the horizon
        if v > 0.60:
            pic = mix3(pic, (0.020, 0.018, 0.022), smooth(0.60, 0.62, v))
        # low sun on the horizon with its halation ring
        sx, sy = u - 0.5, (v - 0.585) * 1.0
        r = math.hypot(sx, sy)
        if v < 0.61:
            glow = math.exp(-max(0.0, r - 0.075) / 0.14) * 0.8
            pic = mix3(pic, SUN_MID, glow * 0.6)
            ring = math.exp(-((r - 0.092) / 0.016) ** 2)
            pic = mix3(pic, HALATION, ring * 0.6)
            disc = 1.0 - smooth(0.070, 0.078, r)
            pic = mix3(pic, mix3(SUN_CORE, SUN_MID, smooth(0.0, 0.078, r)), disc)
        # horizon line catching the light
        hl = math.exp(-((v - 0.61) / 0.004) ** 2) * math.exp(-abs(u - 0.5) / 0.12)
        pic = mix3(pic, HALATION, hl * 0.7)
        col = mix3(pic, col, smooth(-0.012, -0.008, d_frame))

    rim = smooth(-0.010, 0.0, d_plate)
    col = mix3(col, (0.26, 0.25, 0.26), rim * 0.5)
    return (col[0], col[1], col[2], 1.0)


def to_srgb8(c):
    c = min(1.0, max(0.0, c))
    return int(round(c * 255.0))


def render(size):
    rows = []
    for py in range(size):
        row = bytearray([0])  # PNG filter type 0
        for px in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(SS):
                for sx in range(SS):
                    u = (px + (sx + 0.5) / SS) / size
                    v = (py + (sy + 0.5) / SS) / size
                    r, g, b, a = shade(u, v)
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            n = float(SS * SS)
            a = acc[3] / n
            if a > 0.0:
                rgb = [acc[i] / acc[3] for i in range(3)]  # un-premultiply
            else:
                rgb = [0.0, 0.0, 0.0]
            row += bytes([to_srgb8(rgb[0]), to_srgb8(rgb[1]), to_srgb8(rgb[2]), to_srgb8(a)])
        rows.append(bytes(row))
    return b"".join(rows)


def write_png(path, size, raw):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    size = int(sys.argv[2]) if len(sys.argv) > 2 else 128
    write_png(sys.argv[1], size, render(size))
    return 0


if __name__ == "__main__":
    sys.exit(main())
