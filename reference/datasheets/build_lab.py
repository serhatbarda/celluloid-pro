"""v1.3: write the lab / projection conditions ("lab") and the per-stock provenance tags into reference/stocks.json.

    python3 reference/datasheets/build_lab.py && python3 reference/gen_tables.py

Idempotent; touches only stocks.json["lab"] and the "provenance" / "provenanceClass" fields of every stock (the
stock data itself is left alone, so the calibrated interlayer / mask fits of fit_look.py stay as they are).

Provenance classes (shown read-only in the UI and in docs/STOCKS.md):
  datasheet  - modelled from the manufacturer's public technical data (fitted, not scanned or measured from film)
  partial    - data sheet for part of the data, the rest borrowed from a named proxy stock
  designed   - Reafilm original, not a real stock
  style      - style approximation of a real stock without public data
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
REF = os.path.dirname(HERE)

DESIGNED = 'Designed (Reafilm original): parametric spectra and curves, not a real stock.'
FRESH = 'Fresh stock at the nominal process; fitted to the published curves, not measured from film.'

NEG = {
    'Kodak Vision3 50D 5203': ('datasheet', 'Data sheet: Kodak H-1-5203 (2022): spectral sensitivity, characteristic curves; '
                               'dyes from H-1-5219t (VISION3 dye set). ECN-2. ' + FRESH),
    'Kodak Vision3 250D 5207': ('datasheet', 'Data sheet: Kodak H-1-5207 (2015): spectral sensitivity, characteristic curves; '
                                'dyes from H-1-5219t (VISION3 dye set). ECN-2. ' + FRESH),
    'Kodak Vision3 200T 5213': ('datasheet', 'Data sheet: Kodak H-1-5213 (2010): spectral sensitivity, characteristic curves, '
                                'spectral dye density. ECN-2. ' + FRESH),
    'Kodak Vision3 500T 5219': ('datasheet', 'Data sheet: Kodak H-1-5219t (2015): spectral sensitivity, characteristic curves, '
                                'spectral dye density. ECN-2. ' + FRESH),
    'Fujifilm Eterna 250D 8563': ('partial', 'Partial data sheet: Fujifilm KB-0707E (characteristic curves only); spectral '
                                  'sensitivity and dyes borrowed from VISION3 250D (proxy). Discontinued; modelled as fresh stock.'),
    'Kodak Portra 400': ('partial', 'Partial data sheet: Kodak E-4050 (2016): characteristic curves, spectral sensitivity; dyes '
                         'borrowed from the VISION3 set (proxy; not published). C-41. ' + FRESH),
    'Kodak Ektachrome E100': ('datasheet', 'Data sheet: Kodak E-4024 (2009, E100G): characteristic curves, spectral '
                              'sensitivity, spectral dye density. E-6. ' + FRESH),
    # v1.3 additions
    'Kodak Vision2 500T 5218': ('datasheet', 'Data sheet: Kodak H-1-5218 (2007): spectral sensitivity, characteristic curves, '
                                'spectral dye density. ECN-2. Discontinued; modelled as fresh stock.'),
    'Kodak Ektachrome 100D 5294': ('datasheet', 'Data sheet: Kodak EKTACHROME 100D sell sheet: spectral sensitivity, '
                                   'characteristic curves, spectral dye density. E-6. ' + FRESH),
    'Kodak Double-X 5222': ('datasheet', 'Data sheet: Kodak DOUBLE-X 5222 technical data: characteristic curve (D-96, 6 1/2 min) '
                            'and spectral sensitivity, traced from the raster figures. ' + FRESH),
    # v1.3, owner decision 2026-10-11 (close data-sheet stocks are allowed for variety)
    'Kodak Vision2 Expression 500T 5229': ('datasheet', 'Data sheet: Kodak H-1-5229 (2007): spectral sensitivity, '
                                           'characteristic curves, spectral dye density. ECN-2. Discontinued; modelled as fresh stock.'),
    'Fujifilm Eterna Vivid 160 8543': ('partial', 'Partial data sheet: Fujifilm KB-0707E (characteristic curves only); spectral '
                                       'sensitivity and dyes borrowed from VISION3 200T (proxy). Discontinued; modelled as fresh stock.'),
    'Fujifilm Eterna 500 8573': ('partial', 'Partial data sheet: Fujifilm KB-0707E (characteristic curves only); spectral '
                                 'sensitivity and dyes borrowed from VISION3 500T (proxy). Discontinued; modelled as fresh stock.'),
    'Fujifilm Reala 500D 8592': ('partial', 'Partial data sheet: Fujifilm KB-0707E (characteristic curves only); spectral '
                                 'sensitivity and dyes borrowed from VISION3 250D (proxy); the fourth (cyan-sensitive) layer is not '
                                 'modelled. Discontinued; modelled as fresh stock.'),
    'Kodachrome 64 Style': ('style', 'Style approximation: no public Kodachrome data; designed from the Ektachrome E100 data '
                            '(higher contrast, deeper D-max, shorter cyan dye). Not measured from Kodachrome.'),
}
PRT = {
    'Kodak Vision 2383': ('datasheet', 'Data sheet: Kodak H-1-2383 (2022): spectral sensitivity, Status A curves, spectral '
                          'dye density. ECP-2 nominal; lab aim, projection and rendering are set in this group. ' + FRESH),
    'Kodak Vision Premier 2393': ('partial', 'Data sheet: Kodak H-1-2393 (1998): spectral sensitivity, characteristic curves; '
                                  'dyes from 2383 (not published for 2393). ECP-2 nominal.'),
    'Fujifilm Eterna-CP 3513DI (v1.2 legacy curve)': ('partial', 'Partial data sheet: Fujifilm KB-0707E; v1.2 legacy curve '
                                                      '(read from the 3521XD figure with a 10 % short exposure axis, kept so saved '
                                                      'projects render unchanged); spectral data borrowed from Kodak 2383 (proxy). '
                                                      'ECP-2 nominal.'),
    'Fujifilm Eterna-CP 3521XD': ('partial', 'Partial data sheet: Fujifilm KB-0707E (characteristic curves only); spectral '
                                  'data borrowed from Kodak 2383 (proxy). ECP-2 nominal.'),
    'Fujifilm Eterna-CP 3513DI': ('partial', 'Partial data sheet: Fujifilm KB-0707E (characteristic curves only, corrected in '
                                  'v1.3); spectral data borrowed from Kodak 2383 (proxy). ECP-2 nominal.'),
    'Kodak Endura Premier (paper)': ('datasheet', 'Data sheet: Kodak E-4070 (2017): spectral sensitivity, Status A curves, '
                                     'spectral dye density. RA-4 nominal; a reflection print: the paper white is the display '
                                     'white. ' + FRESH),
    'Kodak Portra Endura (paper)': ('partial', 'Data sheet: Kodak E-4021 (2003): Status A curves; spectral data from ENDURA '
                                    'Premier (same dye family, the E-4021 spectral figures are not traceable). RA-4 nominal.'),
    'Kodak Supra Endura (paper)': ('partial', 'Data sheet: Kodak E-4021 (2003): Status A curves; spectral data from ENDURA '
                                   'Premier (same dye family, the E-4021 spectral figures are not traceable). RA-4 nominal.'),
    'Fujifilm Crystal Archive (paper)': ('partial', 'Partial data sheet: Fujifilm CRYSTAL ARCHIVE bulletin: spectral sensitivity, '
                                         'spectral dye density; no curves published -> Kodak ENDURA Premier curves (proxy). '
                                         'RA-4 nominal.'),
    'P6 Modern Print': ('designed', 'Designed: Kodak 2383 spectral data (H-1-2383) with our own tone curves (fitted to the '
                        'default-look target).'),
}

LAB = {
    'source': 'v1.3 print lab model (docs/STOCKS.md 5c.1): projection whites (CIE D65; ACES D60 = x 0.32168 y 0.33767; '
              'the projector lamp itself); process aim = development change of the print within a lab tolerance band '
              '(our values); duplication on VISION Color Intermediate 2242 (Kodak H-1-5242t, 2008: Status M curves, '
              'LAD aims MP 1.15/1.60/1.70, DN 1.00/1.45/1.55); telecine black set.',
    # order = ProjectionWhite choice: D65 (adapted), Theatre Xenon (unadapted), D60 Sim (adapted)
    'whites': [dict(adapt=1, x=0.3127, y=0.3290), dict(adapt=0, x=0.0, y=0.0), dict(adapt=1, x=0.32168, y=0.33767)],
    # order = PrintAim choice: Nominal, Rich, Soft (development units, Print Contrast adds to it)
    'aimStep': [0.0, 1.0, -1.0],
    # per development unit: gamma x (1 + 0.12 w), D-max span x (1 + 0.06 w), fog + 0.01 w (forced development only).
    # w = development sensitivity per layer: the bottom (yellow, blue-sensitive) layer of a print film reacts most to
    # development changes, the top (magenta) layer least (developer reaches it first)
    'aimGamma': 0.12, 'aimDmax': 0.06, 'aimFog': 0.01,
    'aimLayer': [1.0, 0.8, 1.2],
    'mpAim': [1.15, 1.60, 1.70],
    'dnAim': [1.00, 1.45, 1.55],
    # telecine: black set just above the print's D-max (97 % of the D-max transmission is removed)
    'telecineBlack': 0.97,
}


def main():
    path = os.path.join(REF, 'stocks.json')
    J = json.load(open(path))
    F = json.load(open(os.path.join(HERE, 'fits.json')))
    lab = dict(LAB)
    lab['dupe'] = [{k: F['kodak_2242']['curves'][L][k] for k in ('fog', 'gamma', 'k', 'x0', 'R', 'p')} for L in ('R', 'G', 'B')]
    J['lab'] = lab
    for n in J['negatives']:
        cls, txt = NEG.get(n['name'], ('designed', DESIGNED))
        n['provenanceClass'], n['provenance'] = cls, txt
    for p in J['prints']:
        cls, txt = PRT.get(p['name'], ('designed', DESIGNED))
        p['provenanceClass'], p['provenance'] = cls, txt
    json.dump(J, open(path, 'w'), indent=1)
    print('lab + provenance written for', len(J['negatives']), 'negatives,', len(J['prints']), 'prints')


if __name__ == '__main__':
    main()
