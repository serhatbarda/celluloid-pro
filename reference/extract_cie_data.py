"""One-time extraction of the PUBLIC CIE colorimetric tables used by Celluloid Pro.

Run with a Python that has `colour-science` installed (not needed for normal use; the
resulting CSV files are committed in reference/data/):

    python3 reference/extract_cie_data.py

Sources (all public CIE standards, reproduced by colour-science from the CIE publications):
  * CIE 1931 2 degree standard observer colour matching functions (CIE 015:2018, Table 1),
    1 nm data sampled at 5 nm 380..780 nm.
  * CIE standard illuminant D65 relative SPD (ISO 11664-2 / CIE S 014-2), 5 nm.
  * CIE daylight basis functions S0, S1, S2 (CIE 015:2018, Table 6) used to compute
    D-series illuminants (e.g. D55) at any CCT, 5 nm (CIE-interpolated).
"""
import os
import numpy as np
import colour

HERE = os.path.dirname(os.path.abspath(__file__))
WL = np.arange(380, 781, 5)

def main():
    cmfs = colour.MSDS_CMFS['CIE 1931 2 Degree Standard Observer']
    d65 = colour.SDS_ILLUMINANTS['D65']
    basis = colour.colorimetry.SDS_BASIS_FUNCTIONS_CIE_ILLUMINANT_D_SERIES
    with open(os.path.join(HERE, 'data', 'cie1931_2deg_5nm.csv'), 'w') as f:
        f.write('# CIE 1931 2 deg standard observer CMFs, 380-780 nm @ 5 nm (CIE 015:2018)\n')
        f.write('nm,xbar,ybar,zbar\n')
        for w in WL:
            x, y, z = cmfs[w]
            f.write(f'{w},{x:.8g},{y:.8g},{z:.8g}\n')
    with open(os.path.join(HERE, 'data', 'cie_d65_5nm.csv'), 'w') as f:
        f.write('# CIE standard illuminant D65 relative SPD, 380-780 nm @ 5 nm (ISO 11664-2)\n')
        f.write('nm,d65\n')
        for w in WL:
            f.write(f'{w},{d65[w]:.8g}\n')
    with open(os.path.join(HERE, 'data', 'cie_daylight_basis_5nm.csv'), 'w') as f:
        f.write('# CIE daylight basis functions S0,S1,S2, 380-780 nm @ 5 nm (CIE 015:2018 Table 6)\n')
        f.write('nm,s0,s1,s2\n')
        for w in WL:
            vals = [basis[k][w] for k in ('S0', 'S1', 'S2')]
            f.write(f'{w},' + ','.join(f'{v:.8g}' for v in vals) + '\n')

if __name__ == '__main__':
    main()
