#!/usr/bin/env python3
"""Generate the Reafilm Pro plugin icon (128x128 RGBA PNG) from scratch.

Original artwork, computed procedurally: a strip of motion-picture film (sprocket holes along
both edges) with a seven-blade lens iris in the frame, the aperture glowing with a warm
halation-like light. Pure Python standard library (zlib + struct), no third-party packages, so
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


# palette (linear-ish sRGB values 0..1)
BG_TOP = (0.105, 0.098, 0.094)
BG_BOT = (0.040, 0.037, 0.036)
FILM = (0.165, 0.112, 0.070)          # amber film base
FILM_EDGE = (0.230, 0.150, 0.085)
HOLE = (0.025, 0.022, 0.021)
FRAME_LINE = (0.330, 0.215, 0.120)
BARREL = (0.070, 0.068, 0.067)
BARREL_RIM = (0.560, 0.530, 0.500)
BLADE_A = (0.135, 0.130, 0.128)
BLADE_B = (0.185, 0.178, 0.172)
GLOW_CORE = (1.000, 0.905, 0.700)
GLOW_MID = (1.000, 0.560, 0.200)
GLOW_EDGE = (0.800, 0.250, 0.070)


def shade(u, v):
    """Colour + alpha of the icon at normalised coordinates u, v in [0, 1] (v down)."""
    # rounded-square plate
    d_plate = rounded_rect_sd(u, v, 0.5, 0.5, 0.48, 0.48, 0.11)
    if d_plate > 0.0:
        return (0.0, 0.0, 0.0, 0.0)
    col = mix3(BG_TOP, BG_BOT, v)

    # film strip across the plate (slight diagonal for energy)
    tilt = -0.10
    sv = (v - 0.5) * math.cos(tilt) - (u - 0.5) * math.sin(tilt)
    su = (u - 0.5) * math.cos(tilt) + (v - 0.5) * math.sin(tilt)
    half = 0.30
    if abs(sv) < half:
        edge = smooth(half - 0.012, half, abs(sv))
        col = mix3(mix3(FILM, FILM_EDGE, abs(sv) / half), BG_BOT, edge)
        # sprocket holes along both edges
        pitch = 0.105
        k = round(su / pitch)
        for side in (-1.0, 1.0):
            hd = rounded_rect_sd(su, sv, k * pitch, side * (half - 0.055), 0.026, 0.020, 0.008)
            if hd < 0.0:
                col = HOLE
        # frame lines between image areas
        fl = abs(abs(sv) - (half - 0.105))
        if fl < 0.006:
            col = mix3(col, FRAME_LINE, 0.8)

    # lens iris
    cx, cy = 0.5, 0.5
    x, y = u - cx, v - cy
    r = math.hypot(x, y)
    R_OUT, R_RIM, R_BLADE = 0.235, 0.215, 0.205
    if r < R_OUT:
        col = BARREL
        if r > R_RIM:
            t = (r - R_RIM) / (R_OUT - R_RIM)
            col = mix3(BARREL_RIM, BARREL, abs(t - 0.4) * 1.6)
        if r < R_BLADE:
            n = 7
            # curved blade boundaries: angle twisted with radius
            ang = math.atan2(y, x) + 1.9 * (r / R_BLADE) ** 1.4
            seg = 2.0 * math.pi / n
            idx = int(math.floor(ang / seg)) % n
            frac = (ang / seg) - math.floor(ang / seg)
            base = BLADE_A if idx % 2 == 0 else BLADE_B
            # soft highlight on each blade's leading edge
            hl = smooth(0.82, 1.0, frac) * 0.35
            col = mix3(base, (0.42, 0.40, 0.38), hl)
            # crisp dark seam where one blade overlaps the next
            col = mix3(col, (0.03, 0.03, 0.03), (1.0 - smooth(0.0, 0.05, frac)) * 0.85)
            # aperture opening (heptagon) with warm glow
            d_ap = polygon_sd(x, y, n, 0.092, 0.3)
            if d_ap < 0.0:
                g = min(1.0, r / 0.085)
                core = mix3(GLOW_CORE, GLOW_MID, smooth(0.0, 0.75, g))
                col = mix3(core, GLOW_EDGE, smooth(0.55, 1.0, g))
            else:
                # halation: warm light spilling onto the blades just outside the opening
                spill = math.exp(-d_ap / 0.030) * 0.55
                col = mix3(col, GLOW_EDGE, spill)
    # subtle glow over the film around the barrel
    elif r < 0.36:
        spill = math.exp(-(r - R_OUT) / 0.05) * 0.18
        col = mix3(col, GLOW_MID, spill)

    # plate edge highlight
    rim = smooth(-0.012, 0.0, d_plate)
    col = mix3(col, (0.30, 0.29, 0.28), rim * 0.6)
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
