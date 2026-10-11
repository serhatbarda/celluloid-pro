"""Write the real (data-sheet) stocks into reference/stocks.json (append-only indices).

    python3 reference/datasheets/build_stocks.py && python3 reference/gen_tables.py

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
    ('Fujifilm Eterna-CP 3513DI', 'fuji_3513', 'P2 Low Contrast', 0.112,
     'ETERNA-CP 3513DI: Fujifilm print for digital intermediates: neutral, gentle highlights, clean colour.',
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


if __name__ == '__main__':
    main()
