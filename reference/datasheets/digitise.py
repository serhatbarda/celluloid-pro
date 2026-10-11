"""Digitise the public manufacturer data sheets into reference/datasheets/data/*.csv (design-time).

    python3 reference/datasheets/digitise.py <folder with the downloaded PDFs> [stock ...]

Sources (URLs, pages, figure ids) are listed in reference/datasheets/SOURCES.md; the PDFs are not
committed. Vector graphs are read exactly from the PDF drawing operators (vector_extract.py);
raster graphs are traced from the embedded images (raster_trace.py). Each spec gives the axis
frame (PDF points or image pixels) and the axis values at the frame edges.
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import vector_extract as vx  # noqa: E402

OUT = os.path.join(HERE, 'data')

# ---- vector specs: stock -> list of (pdf, page index (0-based), title, frame, axes, curves, kind)
VEC = {
    'kodak_5219': [
        ('VISION3-500T-Color-Negative-Film-7219-TECHNICAL-DATA.pdf', 3, 'Sensitometric Curves', (353.6, 122.3, 538.2, 306.8),
         dict(x=(-8, 8), y=(0, 3)), dict(B=94, G=93, R=95), 'hd'),
        ('VISION3-500T-Color-Negative-Film-7219-TECHNICAL-DATA.pdf', 3, 'Spectral Sensitivity Curves', (352.7, 518.3, 553.1, 669.8),
         dict(x=(250, 750), y=(0, 4)), dict(Y=110, M=111, C=112), 'sens'),
        ('VISION3-500T-Color-Negative-Film-7219-TECHNICAL-DATA.pdf', 4, 'Spectral Dye Density Curves', (81.6, 155.4, 265.9, 339.9),
         dict(x=(400, 800), y=(-0.2, 1.8)), dict(a=28, b=29, c=30, neutral=31, dmin=32), 'dye'),
    ],
    # VISION3 200T 5213 (Spanish edition of H-1-5213, 2010; same figures as the English sheet)
    'kodak_5213': [
        ('VISION-200T-Technical-Data_ES.pdf', 3, 'Curvas sensitometricas', (357.5, 132.4, 542.1, 316.9),
         dict(x=(-8, 8), y=(0, 3)), dict(R=62, G=63, B=64), 'hd'),
        ('VISION-200T-Technical-Data_ES.pdf', 3, 'Curvas de sensibilidad espectral', (350.7, 536.8, 551.2, 688.2),
         dict(x=(250, 750), y=(0, 4.04)), dict(Y=101, M=100, C=102), 'sens'),
        ('VISION-200T-Technical-Data_ES.pdf', 4, 'Curvas de densidad espectral de los colorantes', (89.9, 123.9, 274.2, 308.3),
         dict(x=(400, 800), y=(-0.2, 1.776)), dict(neutral=20, dmin=21, a=22, b=23, c=24), 'dye'),
    ],
    # PORTRA 400 (E-4050, 2016): characteristic curves (Log H Ref -1.44 = normal exposure), spectral sensitivities
    'kodak_portra400': [
        ('e4050_portra_400.pdf', 3, 'Characteristic Curves (frame from axis labels)', (79.5, 102.9, 266.0, 287.8),
         dict(x=(-4, 1), y=(0, 4)), dict(B=36, G=37, R=38), 'hd'),
        ('e4050_portra_400.pdf', 3, 'Spectral-Sensitivity Curves', (76.2, 343.1, 276.7, 494.6),
         dict(x=(250, 750), y=(0, 4)), dict(Y=53, M=55, C=54), 'sens'),
    ],
    # EKTACHROME E100G (E-4024, 2009) - reversal: characteristic, spectral sensitivity, dye density
    'kodak_e100': [
        ('page_8_2_data_sheet_ektachrome_e100g_e100gx_0.pdf', 4, 'Characteristic Curves E100G (frame from axis labels)', (96.5, 136.5, 282.2, 319.8),
         dict(x=(-3, 1), y=(0, 4)), dict(R=12, G=13, B=14), 'hd'),
        ('page_8_2_data_sheet_ektachrome_e100g_e100gx_0.pdf', 4, 'Spectral-Sensitivity Curves', (368.6, 142.4, 569.1, 291.9),
         dict(x=(250, 750), y=(-2, 2)), dict(Y=46, M=45, C=47), 'sens'),
        ('page_8_2_data_sheet_ektachrome_e100g_e100gx_0.pdf', 4, 'Spectral-Dye-Density Curves (frame from axis labels)', (365.8, 402.2, 566.3, 550.9),
         dict(x=(400, 700), y=(0, 1.4)), dict(a=77, b=80, c=81, neutral=82), 'dye'),
    ],
    # VISION Premier 2393 (H-1-2393, 1998): sensitometry (Status A), spectral sensitivity
    'kodak_2393': [
        ('VISPREM.PDF', 2, 'Sensitometric Curves', (246.0, 254.8, 361.6, 370.7), dict(x=(-1, 3), y=(0, 6)), dict(B=60, R=61, G=62), 'hd'),
        ('VISPREM.PDF', 2, 'Spectral-Sensitivity Curves', (244.9, 439.4, 361.2, 527.2), dict(x=(250, 750), y=(-3, 1)),
         dict(Y=67, M=[64, 65], C=66), 'sens'),
    ],
    # Fujifilm motion picture film manual KB-0707E: ETERNA 250D 8563 characteristic curves (p. 33),
    # ETERNA-CP 3513DI characteristic curves (p. 57; 1 grid column = 0.5 log H, origin arbitrary)
    'fuji_8563': [
        ('076_fujifilm_motion_picture_film_manual.pdf', 17, 'ETERNA 250D characteristic curves (camera stops, frame from labels)',
         (277.3, 85.4, 426.1, 207.25), dict(x=(-6, 6), y=(0, 3)), dict(R=4, G=5, B=6), 'hd'),
    ],
    # v1.3 fix: v1.2 read page index 29, which is the ETERNA-CP 3521XD figure, with 4.5 instead of 5.0 log H across
    # the frame (10 grid columns of 0.5 log H). 3513DI is page index 27 (D 0 .. 4); 3521XD is appended as its own print.
    'fuji_3513': [
        ('076_fujifilm_motion_picture_film_manual.pdf', 27, 'ETERNA-CP 3513DI characteristic curves (status A; 10 x 0.5 log H)',
         (279.5, 164.1, 419.6, 274.9), dict(x=(0, 5), y=(0, 4)), dict(B=9, G=10, R=11), 'hd'),
    ],
    'fuji_3521': [
        ('076_fujifilm_motion_picture_film_manual.pdf', 29, 'ETERNA-CP 3521XD characteristic curves (status A; 10 x 0.5 log H)',
         (281.0, 164.0, 421.2, 274.9), dict(x=(0, 5), y=(0, 5)), dict(B=9, G=10, R=11), 'hd'),
    ],
    # v1.3 Fujifilm negatives (KB-0707E): characteristic curves only, frames from the grid lines
    'fuji_8543': [
        ('076_fujifilm_motion_picture_film_manual.pdf', 7, 'ETERNA Vivid 160 8543 characteristic curves (camera stops, status M)',
         (277.5, 83.7, 426.5, 208.5), dict(x=(-6, 6), y=(0, 3)), dict(B=2, G=3, R=4), 'hd'),
    ],
    'fuji_8573': [
        ('076_fujifilm_motion_picture_film_manual.pdf', 13, 'ETERNA 500 8573 characteristic curves (camera stops, status M)',
         (277.5, 83.7, 426.5, 208.5), dict(x=(-6, 6), y=(0, 3)), dict(B=4, G=5, R=6), 'hd'),
    ],
    'fuji_8592': [
        ('076_fujifilm_motion_picture_film_manual.pdf', 19, 'REALA 500D 8592 characteristic curves (relative log H, 0.5 per grid column)',
         (284.3, 83.7, 438.8, 208.5), dict(x=(0, 4.0), y=(0, 3)), dict(B=2, G=3, R=4), 'hd'),
    ],
    # v1.3 Kodak sheets with full data
    'kodak_5294': [
        ('EKTACHROME-100D-sell-sheet-EN.pdf', 1, 'Sensitometric Curves (camera stops; frame -10 .. 6.64 from the tick spacing)',
         (63.3, 533.0, 190.6, 634.9), dict(x=(-10, 6.64), y=(0, 4)), dict(G=20, B=21, R=22), 'hd'),
        ('EKTACHROME-100D-sell-sheet-EN.pdf', 2, 'Spectral-Sensitivity Curves', (60.9, 532.9, 184.8, 638.0),
         dict(x=(250, 750), y=(-2, 2)), dict(Y=4, M=5, C=6), 'sens'),
        ('EKTACHROME-100D-sell-sheet-EN.pdf', 2, 'Spectral-Dye-Density Curves (peak-normalised)', (247.5, 532.8, 373.8, 638.1),
         dict(x=(400, 700), y=(0, 1.5)), dict(a=11, b=13, c=15, neutral=17), 'dye'),
    ],
    'kodak_5218': [
        ('5218.pdf', 2, 'Sensitometric Curves (camera stops)', (236.4, 81.4, 364.3, 154.2), dict(x=(-10, 6), y=(0, 3)),
         dict(B=27, G=28, R=29), 'hd'),
        ('5218.pdf', 2, 'Spectral-Sensitivity Curves (x from the labels)', (243.4, 497.9, 356.8, 584.0),
         dict(x=(248.7, 748.7), y=(-0.01, 4.04)), dict(Y=55, M=56, C=57), 'sens'),
        ('5218.pdf', 2, 'Spectral-Dye-Density Curves (axes from the labels)', (240.2, 642.4, 358.8, 739.5),
         dict(x=(300, 801.7), y=(-0.188, 1.806)), dict(a=65, b=66, c=68, neutral=63, dmin=64), 'dye'),
    ],
    'kodak_5229': [
        ('5229.pdf', 2, 'Sensitometric Curves (camera stops)', (224.5, 68.6, 350.2, 141.3), dict(x=(-10, 6), y=(0, 3)),
         dict(B=16, G=17, R=18), 'hd'),
        ('5229.pdf', 2, 'Spectral-Sensitivity Curves (x from the labels)', (227.9, 484.8, 343.6, 572.4),
         dict(x=(248.1, 748.6), y=(0, 4)), dict(Y=42, M=43, C=44), 'sens'),
        ('5229.pdf', 2, 'Spectral-Dye-Density Curves (x from the labels)', (228.4, 628.5, 344.0, 728.6),
         dict(x=(297.4, 793.1), y=(-0.2, 1.8)), dict(a=list(range(48, 90)), b=91, c=90, neutral=93, dmin=95), 'dye'),
    ],
    # v1.3: VISION Color Intermediate 2242 (H-1-5242t, 2008): sensitometric curves (Status M, ECN-2), used for the
    # release-print duplication chain (negative -> master positive -> duplicate negative -> print)
    'kodak_2242': [
        ('2242-TI.pdf', 3, 'Sensitometric Curves (Status M; x ticks -2 .. 4 log H)', (350.6, 84.4, 535.1, 268.9),
         dict(x=(-2, 4), y=(0, 3)), dict(R=2, G=3, B=4), 'hd'),
    ],
}


# ---- raster specs: stock -> list of (pdf, page, image index, title, axes (values at the detected frame edges:
#      x left/right, y top/bottom), curves {name: (seed x, seed y, x min, x max)} in data units, kind, options)
V3_ROOT = 'VISION3-250D-Technical-Data-EN.pdf'
RAS = {
    'kodak_5207': [
        (V3_ROOT, 2, 0, 'Diffuse rms Granularity Curves (density curves)', dict(x=(0, 5), y=(3, 0)),
         dict(B=(4.0, 2.72, 0.0, 5.0), G=(4.0, 2.42, 0.0, 5.0), R=(4.0, 1.74, 0.0, 5.0)), 'hd', {}),
        (V3_ROOT, 3, 0, 'Spectral Sensitivity Curves', dict(x=(250, 750), y=(4, 0)),
         dict(Y=(430, 2.40, 355, 522), M=(520, 2.12, 398, 602), C=(630, 2.22, 525, 702)), 'sens', {}),
        (V3_ROOT, 3, 2, 'Spectral Dye Density Curves', dict(x=(400, 800), y=(1.8, -0.2)),
         dict(Y=(470, 0.80, 400, 800), M=(520, 0.78, 400, 800), C=(650, 0.88, 400, 800), neutral=(560, 1.05, 400, 800)), 'dye', {}),
    ],
    'kodak_5203': [
        ('50D_DATASHEEt.pdf', 2, 0, 'Diffuse rms Granularity Curves (density curves)', dict(x=(0, 5), y=(3, 0)),
         dict(B=dict(guide=[(0, 0.91), (0.5, 0.91), (0.9, 0.93), (1.2, 1.03), (1.6, 1.22), (2.0, 1.45), (3.0, 2.12), (4.0, 2.68), (4.5, 2.82),
                            (5.0, 2.875)], tol=10),
              R=dict(guide=[(0, 0.18), (1.0, 0.19), (1.3, 0.25), (1.6, 0.40), (2.0, 0.62), (2.5, 0.92), (3.0, 1.25), (3.5, 1.55), (4.0, 1.80), (4.5, 1.94), (5.0, 2.0)], tol=6),
              G=dict(guide=[(0, 0.62), (0.9, 0.62), (1.2, 0.68), (1.5, 0.82), (2.0, 1.12), (2.5, 1.48), (3.0, 1.85), (3.5, 2.17), (4.0, 2.42), (4.5, 2.57), (5.0, 2.63)], tol=6)),
         'hd', {}),
        ('50D_DATASHEEt.pdf', 3, 0, 'Spectral Sensitivity Curves (grid rows = 4.0 / 3.0 / 2.0; frame 5.0 .. 1.0)', dict(x=(250, 750), y=(5, 1)),
         dict(Y=(430, 2.40, 355, 500), M=(520, 2.05, 470, 595), C=(625, 2.25, 570, 680)), 'sens', {}),
        ('50D_DATASHEEt.pdf', 3, 2, 'Spectral Dye Density Curves', dict(x=(400, 800), y=(1.8, -0.2)),
         dict(Y=(470, 0.80, 400, 760), M=(515, 0.70, 400, 760), C=(650, 0.88, 400, 760), neutral=(560, 1.10, 400, 760)), 'dye', {}),
    ],
    'kodak_2383': [
        ('KODAK-VISION-Color-Print-Film-2383-3383-data-sheet.pdf', 3, 0, 'Sensitometric Curves (Status A)', dict(x=(-3, 3), y=(6, 0)),
         # guides read on a 2x zoom of the crop from (180, 150), traced row by row (steep curves)
         dict(B=dict(zoom=(180, 150, 2), transpose=True, guide=[(36, 563), (100, 556), (150, 540), (190, 500), (220, 430), (245, 330), (265, 230),
                                                                (285, 150), (310, 90), (340, 62), (380, 52), (410, 50)]),
              G=dict(zoom=(180, 150, 2), transpose=True, guide=[(76, 572), (130, 568), (180, 555), (220, 520), (250, 450), (275, 330), (295, 240),
                                                                (320, 150), (350, 85), (380, 60), (420, 50)]),
              R=dict(zoom=(180, 150, 2), transpose=True, guide=[(130, 573), (180, 568), (230, 555), (270, 510), (300, 430), (320, 330), (340, 240),
                                                                (360, 150), (380, 95), (410, 62), (440, 52), (475, 48)])),
         'hd', {}),
        ('KODAK-VISION-Color-Print-Film-2383-3383-data-sheet.pdf', 4, 0, 'Spectral Sensitivity Curves', dict(x=(250, 750), y=(1, -3)),
         dict(Y=(430, 0.05, 362, 506), C=(650, -1.75, 590, 735),
              M=dict(guide=[(453, -1.72), (480, -1.45), (510, -1.05), (525, -0.95), (540, -0.75), (550, -0.15), (560, -0.9), (575, -2.0), (588, -3.0)], tol=6)),
         'sens', {}),
        ('KODAK-VISION-Color-Print-Film-2383-3383-data-sheet.pdf', 4, 1, 'Spectral Dye Density Curves', dict(x=(250, 750), y=(1.4, 0)),
         # guides read on a 2x zoom of the crop from (60, 30); x frame 63..371 = 250..750 nm (tick marks; the
         # printed axis labels are mis-spaced), y 343..35 = 0..1.4
         dict(Y=dict(zoom=(60, 30, 2), guide=[(130, 510), (158, 545), (185, 470), (215, 330), (245, 268), (275, 330), (300, 455), (330, 560),
                                             (370, 610), (450, 625), (620, 628)], tol=3),
              M=dict(zoom=(60, 30, 2), guide=[(130, 515), (183, 495), (220, 560), (250, 595), (300, 450), (335, 310), (365, 240), (395, 320),
                                             (420, 460), (460, 580), (520, 615), (620, 626)], tol=3),
              C=dict(zoom=(60, 30, 2), guide=[(130, 590), (170, 608), (220, 595), (320, 610), (380, 540), (420, 440), (470, 270), (515, 148),
                                             (560, 230), (600, 360), (628, 445)], tol=3),
              neutral=dict(zoom=(60, 30, 2), guide=[(132, 116), (150, 230), (172, 300), (200, 210), (232, 145), (260, 180), (305, 248), (340, 180),
                                                   (375, 130), (400, 180), (428, 240), (470, 160), (510, 97), (560, 160), (600, 330), (628, 470)])),
         'dye', {}),
    ],
}


def extract_image(pdf, page, k, out):
    import pymupdf
    d = pymupdf.open(pdf)
    im = d[page].get_images()[k]
    pix = pymupdf.Pixmap(d, im[0])
    if pix.n > 3 or pix.colorspace is None or pix.colorspace.n != 3:
        pix = pymupdf.Pixmap(pymupdf.csRGB, pix)
    pix.save(out)
    return out


def run_ras(folder, stock, overlay_dir=None):
    import raster_trace as rt
    for pdf, page, k, title, ax, curves, kind, opt in RAS[stock]:
        png = os.path.join(overlay_dir or '/tmp', f'{stock}_{kind}.png')
        extract_image(os.path.join(folder, pdf), page, k, png)
        g = rt.load_gray(png)
        fr = rt.find_frame(g)
        axes = dict(x=ax['x'], y=(ax['y'][1], ax['y'][0]))     # bottom, top
        traced = {}
        for name, cs in curves.items():
            if isinstance(cs, dict) and 'zoom' in cs:   # guide read on a zoomed crop: (X, Y) -> off + XY / scale
                ox, oy, sc = cs['zoom']
                gpx = np.array([ox + p[0] / sc for p in cs['guide']])
                gpy = np.array([oy + p[1] / sc for p in cs['guide']])
                if cs.get('transpose'):     # steep monotone curve: follow it row by row (x as a function of y)
                    o = np.argsort(gpy)
                    ys, xs, ok = rt.trace_guided(g.T.copy(), (fr[1], fr[0], fr[3], fr[2]), list(zip(gpy[o], gpx[o])), tol=cs.get('tol', 5.0))
                    o = np.argsort(xs, kind='stable')
                    xs, ys, ok = xs[o], ys[o], ok[o]
                else:
                    xs, ys, ok = rt.trace_guided(g, fr, list(zip(gpx, gpy)), tol=cs.get('tol', 5.0))
            elif isinstance(cs, dict):      # hand-placed guide polyline, snapped to the printed line
                gpx, gpy = rt.data_to_px(np.array([p[0] for p in cs['guide']], float), np.array([p[1] for p in cs['guide']], float), fr, axes)
                xs, ys, ok = rt.trace_guided(g, fr, list(zip(gpx, gpy)), tol=cs.get('tol', 7.0))
            else:
                sx, sy, xa, xb = cs
                px, py = rt.data_to_px(np.array([sx, xa, xb], float), np.array([sy, 0, 0], float), fr, axes)
                xs, ys = rt.trace(g, fr, (int(round(px[0])), float(py[0])), (int(px[1]), int(px[2])), **opt)
                ok = np.ones(len(xs), bool)
            if ok.mean() > 0.3 and not (isinstance(cs, dict) and cs.get('transpose')):
                xs, ys = xs[ok], ys[ok]       # keep only points that sit on the printed line
            X, Y = rt.px_to_data(xs, ys, fr, axes)
            traced[name] = (xs, ys)
            out = os.path.join(OUT, f'{stock}_{kind}_{name}.csv')
            np.savetxt(out, np.stack([X, Y], 1), fmt='%.5f', delimiter=',',
                       header=f'x,y  ({pdf} page {page + 1}, image {k}: {title}; raster trace)')
            print(os.path.basename(out), len(X), 'x %.1f..%.1f  y %.3f..%.3f' % (X.min(), X.max(), Y.min(), Y.max()))
        if overlay_dir:
            from PIL import Image, ImageDraw
            im = Image.open(png).convert('RGB')
            dr = ImageDraw.Draw(im)
            cols = [(255, 0, 0), (0, 160, 0), (0, 0, 255), (255, 0, 255), (0, 200, 200)]
            for (nm, (xs, ys)), c in zip(traced.items(), cols):
                dr.line(list(zip(xs.tolist(), ys.tolist())), fill=c, width=2)
            im.save(os.path.join(overlay_dir, f'{stock}_{kind}_overlay.png'))


def run_vec(folder, stock):
    for pdf, page, title, frame, axes, curves, kind in VEC[stock]:
        D = vx.drawings(os.path.join(folder, pdf), page)
        for name, idx in curves.items():
            pts = []
            for i in (idx if isinstance(idx, (list, tuple)) else [idx]):
                for seg in vx.path_points(D[i]):
                    pts += seg
            xy = vx.to_data(np.array(pts), frame, axes)
            xy = xy[np.argsort(xy[:, 0], kind='stable')]
            out = os.path.join(OUT, f'{stock}_{kind}_{name}.csv')
            np.savetxt(out, xy, fmt='%.5f', delimiter=',', header=f'x,y  ({pdf} page {page + 1}: {title})')
            print(os.path.basename(out), len(xy), 'x %.2f..%.2f  y %.3f..%.3f' % (xy[:, 0].min(), xy[:, 0].max(), xy[:, 1].min(), xy[:, 1].max()))


if __name__ == '__main__':
    folder = sys.argv[1]
    ov = os.environ.get('CEL_DIGITISE_OVERLAYS')
    os.makedirs(OUT, exist_ok=True)
    for s in (sys.argv[2:] or list(VEC) + list(RAS)):
        if s in VEC:
            run_vec(folder, s)
        if s in RAS:
            run_ras(folder, s, ov)
