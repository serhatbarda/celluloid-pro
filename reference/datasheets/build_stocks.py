"""Write the real (data-sheet) stocks into reference/stocks.json (append-only indices).

    python3 reference/datasheets/build_stocks.py && python3 reference/gen_tables.py
    python3 reference/datasheets/build_stocks.py --v13   # v1.3: append the new stocks only (keeps all calibrations)

Every stock is built from reference/datasheets/fits.json (fit_stocks.py) for the measured parts
(spectral sensitivities, dye densities, characteristic curves) and from the closest designed
stock for the parts no data sheet publishes (interlayer / DIR matrix, granularity weights, Lab
Scan calibration, halation / bloom / grain-size scales). Existing entries are matched by name and
updated in place; new ones are appended, so OFX choice indices of saved projects stay valid.
"""
import copy
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
REF = os.path.dirname(HERE)


def lobes(ls):
    return [[round(float(v), 4) for v in l] for l in ls]


def sens(F):
    return [lobes(F['sens'][L]['lobes']) for L in ('R', 'G', 'B')]


def dyes(F):
    return [dict(primary=lobes(F['dyes'][K]['primary']), secondary=lobes(F['dyes'][K]['secondary'])) for K in ('C', 'M', 'Y')]


def curves(F, scale=1.0, fog=None):
    out = []
    for L in ('R', 'G', 'B'):
        c = {k: F['curves'][L][k] for k in ('fog', 'gamma', 'k', 'x0', 'R', 'p')}
        c['gamma'] = round(c['gamma'] * scale, 4)
        if fog is not None:
            c['fog'] = fog
        out.append(c)
    return out


NEG = [
    # name, fit key, template (designed stock name), balance, grain size, halation, bloom, granularity scale, description, source
    ('Kodak Vision3 50D 5203', 'kodak_5203', 'CN 50D Fine Daylight', ('daylight', 5500), 0.7, 0.5, 0.9, 1.0,
     'VISION3 50D: the finest-grain daylight negative, clean and saturated with long highlight latitude.',
     'Kodak H-1-5203 (2022): spectral sensitivity, characteristic curves; VISION3 dye set (H-1-5219t)'),
    ('Kodak Vision3 250D 5207', 'kodak_5207', 'CN 250D Soft Daylight', ('daylight', 5500), 0.95, 0.6, 1.0, 1.0,
     'VISION3 250D: medium-speed daylight negative, natural colour and soft, open highlights.',
     'Kodak H-1-5207 (2015): spectral sensitivity, characteristic curves; VISION3 dye set (H-1-5219t)'),
    ('Kodak Vision3 200T 5213', 'kodak_5213', 'CN 200T Studio Tungsten', ('blackbody', 3200), 1.0, 0.6, 1.0, 1.0,
     'VISION3 200T: tungsten negative with fine grain, rich mid-tones and clean shadows.',
     'Kodak H-1-5213 (2010, Spanish edition): sensitometry, spectral sensitivity, spectral dye density'),
    ('Kodak Vision3 500T 5219', 'kodak_5219', 'CN 500T High Speed', ('blackbody', 3200), 1.2, 0.75, 1.1, 1.0,
     'VISION3 500T: the high-speed tungsten workhorse: low shadow grain, warm under tungsten, big latitude.',
     'Kodak H-1-5219t (2015): sensitometry, spectral sensitivity, spectral dye density'),
    ('Fujifilm Eterna 250D 8563', 'fuji_8563', 'CN 250D Soft Daylight', ('daylight', 5500), 0.95, 0.6, 1.0, 1.0,
     'ETERNA 250D: soft-contrast daylight negative with restrained, calm colour (Fujifilm curves; spectral proxies).',
     'Fujifilm KB-0707E p.33: characteristic curves; spectral sensitivities and dyes not published there -> VISION3 250D / dye-set proxies'),
    ('Kodak Portra 400', 'kodak_portra400', 'C41 Portrait 400', ('daylight', 5500), 1.1, 0.6, 1.05, 1.0,
     'PORTRA 400: portrait colour negative: warm, low-chroma skin, very wide latitude.',
     'Kodak E-4050 (2016): characteristic curves, spectral sensitivity; dye curves not published -> VISION3 dye set'),
    ('Kodak Ektachrome E100', 'kodak_e100', 'RV 100 Slide', ('daylight', 5500), 0.8, 0.5, 0.9, 1.0,
     'EKTACHROME E100: daylight reversal: clean neutral greys, crisp, saturated colour, short latitude.',
     'Kodak E-4024 (2009, E100G): characteristic curves, spectral sensitivity, spectral dye density'),
]
PRT = [
    ('Kodak Vision 2383', 'kodak_2383', 'P1 Standard Projection', 0.106,
     'VISION Color Print Film 2383: the standard release print: deep blacks, rich colour, warm highlights.',
     'Kodak H-1-2383 (2022): sensitometry (status A), spectral sensitivity, spectral dye density'),
    ('Kodak Vision Premier 2393', 'kodak_2393', 'P3 Rich Contrast', 0.100,
     'VISION Premier 2393: premium print with higher D-max: denser blacks and more saturated colour than 2383.',
     'Kodak H-1-2393 (1998): sensitometry, spectral sensitivity; 2383 dye set (not published for 2393)'),
    # v1.3: renamed (same index 7, same curves). Its v1.2 curves were digitised from the 3521XD figure with a 10 %
    # short log H axis (docs/STOCKS.md 5c.3); the corrected 3513DI is appended in PRT13.
    ('Fujifilm Eterna-CP 3513DI (v1.2 legacy curve)', 'fuji_3513', 'P2 Low Contrast', 0.112,
     'ETERNA-CP 3513DI, v1.2 legacy curve: the curve read in v1.2 (from the 3521XD figure, 10 % short exposure axis). '
     'Kept so saved projects render unchanged; the corrected 3513DI is a separate entry.',
     'Fujifilm KB-0707E p.57: characteristic curves; spectral data not published there -> 2383 proxies'),
]


def put(lst, entry):
    for i, e in enumerate(lst):
        if e['name'] == entry['name']:
            lst[i] = entry
            return i
    lst.append(entry)
    return len(lst) - 1


def main():
    path = os.path.join(REF, 'stocks.json')
    J = json.load(open(path))
    F = json.load(open(os.path.join(HERE, 'fits.json')))
    byname = {n['name']: n for n in J['negatives']}
    pbyname = {p['name']: p for p in J['prints']}
    for name, key, tmpl, bal, gss, hal, blo, gsc, desc, src in NEG:
        t = copy.deepcopy(byname[tmpl])
        f = F[key]
        e = t
        e['name'] = name
        e['description'] = desc
        e['source'] = src
        e['balance'] = {'type': bal[0], 'cct': bal[1]}
        e['grainSizeScale'], e['halationScale'], e['bloomScale'] = gss, hal, blo
        e['sensitivity'] = sens(f)
        e['dyes'] = dyes(f)
        e['curves'] = curves(f, fog=0.15 if t['kind'] == 'reversal' else 0.1)
        e['granularity'] = [round(g * gsc, 5) for g in t['granularity']]
        if t['kind'] == 'reversal':
            e['hueHold'], e['chromaHold'] = 0.5, 0.0
        put(J['negatives'], e)
    # Kodachrome-style: no public data sheet with curves -> designed from the E100 data (K-14 traits:
    # higher contrast, deeper D-max, stronger inter-image effects, a cyan dye peaking lower, warmer reds)
    k = copy.deepcopy(byname.get('Kodak Ektachrome E100') or [n for n in J['negatives'] if n['name'] == 'Kodak Ektachrome E100'][0])
    k['name'] = 'Kodachrome 64 Style'
    k['description'] = 'Kodachrome-style reversal (designed, no data sheet): dense, contrasty, warm reds, deep blue skies.'
    k['source'] = 'designed from the Ektachrome E100 data: gamma x1.25, D-max +0.4, cyan dye 25 nm shorter, interlayer x1.4'
    for c in k['curves']:
        c['gamma'] = round(c['gamma'] * 1.25, 4)
        c['R'] = round(c['R'] + 0.4, 4)
    for l in k['dyes'][0]['primary']:
        l[0] = round(l[0] - 25, 2)   # 25 nm: >= 2 mean dE2000 from E100 (15 nm gave 1.5)
    k['interlayer'] = [[round(v * 1.4, 4) for v in r] for r in k['interlayer']]
    k['granularity'] = [round(g * 0.85, 5) for g in k['granularity']]
    k['grainSizeScale'] = 0.75
    put(J['negatives'], k)
    for name, key, tmpl, greyY, desc, src in PRT:
        t = copy.deepcopy(pbyname[tmpl])
        f = F[key]
        t.update(name=name, description=desc, source=src, greyY=greyY, sensitivity=sens(f), dyes=dyes(f), curves=curves(f),
                 hueHold=0.5, chromaHold=0.0)
        put(J['prints'], t)
    # P6 Modern Print (designed): the measured 2383 spectral data (sensitivities, dyes) with tone curves fitted to
    # the v1.2 default-look target (reference/fit_look.py writes the curves; created here on first run)
    if not any(p['name'] == 'P6 Modern Print' for p in J['prints']):
        t = copy.deepcopy([p for p in J['prints'] if p['name'] == 'Kodak Vision 2383'][0])
        t.update(name='P6 Modern Print',
                 description='Modern projection print (default): Referent-dense lower mids, true blacks, ARRI-soft highlight '
                             'roll-off, calm colour. Kodak 2383 dyes and sensitivities with our tone scale.',
                 source='designed: Kodak 2383 spectral data (H-1-2383) + tone curves fitted by reference/fit_look.py')
        put(J['prints'], t)
    # C1 (v1.2 look-dev): no chroma boost in the hue hold, half the hue hold (it acts only beyond +-1.5 stops,
    # kernel), for every print, the scanner and the reversal stocks
    for p in J['prints']:
        p['hueHold'], p['chromaHold'] = 0.5, 0.0
    for n in J['negatives']:
        if n['kind'] == 'reversal':
            n['hueHold'], n['chromaHold'] = 0.5, 0.0
    J['scanner']['hueHold'] = min(J['scanner'].get('hueHold', 0.0), 0.5)
    J['scanner']['chromaHold'] = 0.0
    json.dump(J, open(path, 'w'), indent=1)
    print('negatives:', [n['name'] for n in J['negatives']])
    print('prints:', [p['name'] for p in J['prints']])


# v1.3 WP-2: appended stocks. Template = the closest real stock already in stocks.json (its calibrated interlayer /
# mask are the start point of fit_look.py calibrate), or the designed B&W stock for Double-X.
NEG13 = [
    ('Kodak Vision2 500T 5218', 'kodak_5218', 'Kodak Vision3 500T 5219', ('blackbody', 3200), 1.3, 0.8, 1.1, 1.15,
     'VISION2 500T: the 2000s high-speed tungsten negative: a little more grain and less latitude than VISION3, warm, punchy mids.',
     'Kodak H-1-5218 (2007): sensitometry, spectral sensitivity, spectral dye density'),
    ('Kodak Ektachrome 100D 5294', 'kodak_5294', 'Kodak Ektachrome E100', ('daylight', 5500), 0.8, 0.5, 0.9, 1.0,
     'EKTACHROME 100D: the motion-picture colour reversal: clean, saturated, contrasty, short latitude.',
     'Kodak EKTACHROME 100D sell sheet (EN): sensitometry, spectral sensitivity, spectral dye density'),
    ('Kodak Double-X 5222', 'kodak_5222', 'BW 400 Panchromatic', ('daylight', 5500), 1.35, 0.7, 1.0, 1.3,
     'DOUBLE-X 5222: the classic panchromatic B&W negative: open grain, blue-heavy sensitivity, gentle toe.',
     'Kodak DOUBLEX technical data (EN): characteristic curves (D-96, 6 1/2 min), spectral sensitivity (raster traced)'),
    # v1.3 (owner decision 2026-10-11: close data-sheet stocks are wanted for variety): appended 22-25
    ('Kodak Vision2 Expression 500T 5229', 'kodak_5229', 'Kodak Vision3 500T 5219', ('blackbody', 3200), 1.25, 0.8, 1.1, 1.1,
     'VISION2 Expression 500T: low-contrast VISION2 tungsten negative for a soft, flat scan with lifted shadows.',
     'Kodak H-1-5229 (2007): sensitometry, spectral sensitivity, spectral dye density'),
    ('Fujifilm Eterna Vivid 160 8543', 'fuji_8543', 'Kodak Vision3 200T 5213', ('blackbody', 3200), 0.95, 0.6, 1.0, 1.0,
     'ETERNA Vivid 160: tungsten negative with more saturation and contrast than ETERNA (Fujifilm curves; spectral proxies).',
     'Fujifilm KB-0707E p.12-13: characteristic curves; spectral sensitivities and dyes not published there -> VISION3 200T proxies'),
    ('Fujifilm Eterna 500 8573', 'fuji_8573', 'Kodak Vision3 500T 5219', ('blackbody', 3200), 1.2, 0.75, 1.1, 1.0,
     'ETERNA 500: high-speed tungsten negative with calm, low-saturation ETERNA colour (Fujifilm curves; spectral proxies).',
     'Fujifilm KB-0707E p.24-25: characteristic curves; spectral sensitivities and dyes not published there -> VISION3 500T proxies'),
    ('Fujifilm Reala 500D 8592', 'fuji_8592', 'Kodak Vision3 250D 5207', ('daylight', 5500), 1.15, 0.7, 1.05, 1.05,
     'REALA 500D: high-speed daylight negative (Fujifilm curves; spectral proxies, the fourth colour layer is not modelled).',
     'Fujifilm KB-0707E p.36-37: characteristic curves; spectral sensitivities and dyes not published there -> VISION3 250D proxies'),
]
# v1.3 prints, appended 9-10. The corrected 3513DI is read from its own figure (p.52-53, 5.0 log H across the frame).
LEGACY_3513 = 'Fujifilm Eterna-CP 3513DI (v1.2 legacy curve)'
PRT13 = [
    ('Fujifilm Eterna-CP 3521XD', 'fuji_3521', LEGACY_3513, 0.110,
     'ETERNA-CP 3521XD: Fujifilm high-D-max print: deeper blacks and richer contrast than 3513DI.',
     'Fujifilm KB-0707E p.56-57: characteristic curves; spectral data not published there -> 2383 proxies'),
    ('Fujifilm Eterna-CP 3513DI', 'fuji_3513v13', LEGACY_3513, 0.112,
     'ETERNA-CP 3513DI: Fujifilm print for digital intermediates: neutral, gentle highlights, clean colour.',
     'Fujifilm KB-0707E p.52-53: characteristic curves; spectral data not published there -> 2383 proxies'),
]


def main_v13():
    """append the v1.3 stocks (by name; existing entries are left alone)"""
    path = os.path.join(REF, 'stocks.json')
    J = json.load(open(path))
    F = json.load(open(os.path.join(HERE, 'fits.json')))
    byname = {n['name']: n for n in J['negatives']}
    pbyname = {p['name']: p for p in J['prints']}
    for name, key, tmpl, bal, gss, hal, blo, gsc, desc, src in NEG13:
        if name in byname:
            continue
        e = copy.deepcopy(byname[tmpl])
        f = F[key]
        e.update(name=name, description=desc, source=src, balance={'type': bal[0], 'cct': bal[1]},
                 grainSizeScale=gss, halationScale=hal, bloomScale=blo)
        e['granularity'] = [round(g * gsc, 5) for g in e['granularity']]
        if e['kind'] == 'bw':
            e['sensitivity'] = [lobes(f['sens']['G']['lobes'])]
            c = {k: f['curves']['G'][k] for k in ('fog', 'gamma', 'k', 'x0', 'R', 'p')}
            e['curves'] = [c]
        else:
            e['sensitivity'] = sens(f)
            e['dyes'] = dyes(f)
            e['curves'] = curves(f, fog=0.15 if e['kind'] == 'reversal' else 0.1)
            e['calibration'] = 'interlayer + mask taken from %s (not fitted)' % tmpl
        put(J['negatives'], e)
    for old in J['prints']:
        if old['name'] == 'Fujifilm Eterna-CP 3513DI' and old['source'].startswith('Fujifilm KB-0707E p.57'):
            # v1.2 projects: index 7 keeps its curves under an honest name
            old.update(name=PRT[2][0], description=PRT[2][4])
            pbyname[old['name']] = old
    for name, key, tmpl, greyY, desc, src in PRT13:
        # prints carry no calibration: an existing entry is rebuilt in place (same index)
        t = copy.deepcopy(pbyname[tmpl])
        f = F[key]
        t.update(name=name, description=desc, source=src, greyY=greyY, sensitivity=sens(f), dyes=dyes(f), curves=curves(f))
        if key == 'fuji_3513v13':
            # corrected 3513DI: the legacy entry's spectral data (the 2383 proxies) unchanged, only the curves are new
            t['sensitivity'], t['dyes'] = pbyname[tmpl]['sensitivity'], pbyname[tmpl]['dyes']
        put(J['prints'], t)
    json.dump(J, open(path, 'w'), indent=1)
    print('negatives:', len(J['negatives']), [n['name'] for n in J['negatives']][19:])
    print('prints:', len(J['prints']), [p['name'] for p in J['prints']][9:])


if __name__ == '__main__':
    import sys
    main_v13() if '--v13' in sys.argv else main()
