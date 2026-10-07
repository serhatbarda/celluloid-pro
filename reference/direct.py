"""Direct (LUT-free for the dye stages) evaluation of the film model, for stock design /
optimisation and for measuring the LUT interpolation error of the baked pipeline.

Only the stock-independent spectral upsampling uses its octahedral table (that IS the model);
negative transmittance, printing, print curves and projection are integrated spectrally per
pixel in float64.
"""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import model as m  # noqa: E402
import spectral as sp  # noqa: E402


class Direct:
    def __init__(self, lib, neg, prt, process='print', push=0.0, interlayer=1.0, neutralize=0.0):
        self.lib = lib
        self.neg = neg
        self.kind = 'reversal' if neg['kind'] == 'reversal' else process
        self.mono = neg['kind'] == 'bw'
        if self.mono:
            prt = m.print_mono_variant(prt)
        self.prt = prt
        self.LK = m.log_exposure_table(m.bake_exposure_lut(lib, neg), lib.up_a.reshape(lib.N_OCT, lib.N_OCT) > 0)
        rev = neg['kind'] == 'reversal'
        cs = neg['curves'] if not self.mono else [neg['curves'][0]] * 3
        self.ncs = [m.push_params(c, push, rev) for c in cs]
        self.rev = rev
        self.ngamma = np.array([c['gamma'] for c in self.ncs])
        self.dmin = np.array([m.neg_curve_eval(c, np.array(m.NEG_X1 if rev else m.NEG_X0), rev) for c in self.ncs])
        self.inter = np.array(neg.get('interlayer', np.zeros((3, 3)).tolist()), float) * interlayer
        np.fill_diagonal(self.inter, 0.0)
        self.eps, self.sec = m.neg_dyes_scaled(lib, neg)
        self.masks = np.array(neg.get('mask', [0, 0, 0]), float)
        self.dsat = np.array([c['fog'] + c['R'] for c in (neg['curves'] if not self.mono else [neg['curves'][0]] * 3)])
        self.nbase = m.base_density(neg['base']) if 'base' in neg else np.zeros(sp.NWL)
        lamp = lib.lamps['projector']
        self.lamp = lamp
        self.Mcat = m.cat_to_d65(lamp)
        if self.kind == 'print':
            S = m.print_sens(prt)
            w = S * lib.lamps['printer'][None, :]
            self.pw = w / w.sum(1, keepdims=True)
            self.pcs = m.print_curve_params(prt)
            self.pmin = np.array([c['fog'] + m.dev(np.array(m.PRT_U0 - c['x0']), c['gamma'], c['k'], c['R'], c['p']) for c in self.pcs])
            eps = m.print_dyes(prt)
            self.pbase = m.base_density(prt['base'])
            if not self.mono:
                eps = eps * m.visual_end_scale(eps, self.pbase, lamp)[:, None]
            eps = eps * m.print_fade(prt)[:, None]
            self.peps = eps
            dclear = self.pbase + self.pmin @ eps
            self.Yb = sp.XYZ_of(lamp * 10.0 ** (-dclear))[1]
            self.off = np.zeros(3)
            self.balance()
        elif self.kind == 'reversal':
            dclear = self.nbase + self.dmin @ self.eps
            self.Yb = sp.XYZ_of(lamp * 10.0 ** (-dclear))[1]
            self.off = np.zeros(3)
            self.balance()

    def curve(self, x):
        return np.stack([m.neg_curve_eval(c, x[:, i], self.rev) for i, c in enumerate(self.ncs)], 1)

    def exposures(self, rgb):
        return m.exposures_from_log_table(self.LK, rgb)

    def densities(self, rgb):
        x = self.exposures(rgb)
        xref = x @ m.LAYER_W
        sgn = -1.0 if self.rev else 1.0
        c = sgn * (self.curve(x) - self.curve(np.repeat(xref[:, None], 3, 1))) / self.ngamma[None, :]
        delta = -(c @ self.inter.T)
        delta = delta - (delta @ m.LAYER_W)[:, None]
        x = x + delta
        if self.kind == 'reversal':
            x = x + self.off[None, :]
        return self.curve(x), xref

    def neg_T(self, d):
        dens = self.nbase[None, :] + d @ self.eps + (self.masks[None, :] * np.maximum(self.dsat[None, :] - d, 0.0)) @ self.sec
        return 10.0 ** (-dens)

    def xyz(self, rgb, off=None):
        o = self.off if off is None else off
        d, xref = self.densities(rgb)
        if self.kind == 'print':
            T = self.neg_T(d)
            h = np.log10(T @ self.pw.T) + o[None, :]
            P = np.stack([c['fog'] + m.dev(h[:, j] - c['x0'], c['gamma'], c['k'], c['R'], c['p']) for j, c in enumerate(self.pcs)], 1)
            Tp = 10.0 ** (-(self.pbase[None, :] + P @ self.peps))
            X = (Tp * self.lamp[None, :]) @ sp.CMF / self.Yb
        elif self.kind == 'reversal':
            Tp = 10.0 ** (-(self.nbase[None, :] + d @ self.eps))
            X = (Tp * self.lamp[None, :]) @ sp.CMF / self.Yb
        else:
            raise NotImplementedError('scan direct path not needed for design')
        return X @ self.Mcat.T

    def balance(self):
        tY = (self.prt if self.kind == 'print' else self.neg)['greyY']
        target = np.log10(sp.xy_to_XYZ(sp.WHITE_D65, tY))
        grey = np.full((1, 3), 0.18)
        # initial guess: bisection per layer on the curve
        d, _ = self.densities(grey) if self.kind == 'print' else (None, None)
        off = np.zeros(3)
        dt = -math.log10(tY)
        if self.kind == 'print':
            h0 = np.log10(self.neg_T(d) @ self.pw.T)[0]
            for j, c in enumerate(self.pcs):
                lo, hi = m.PRT_U0, m.PRT_U1
                for _ in range(60):
                    mid = 0.5 * (lo + hi)
                    if c['fog'] + m.dev(np.array(mid - c['x0']), c['gamma'], c['k'], c['R'], c['p']) - self.pmin[j] < dt:
                        lo = mid
                    else:
                        hi = mid
                off[j] = 0.5 * (lo + hi) - h0[j]
        else:
            for j, c in enumerate(self.ncs):
                lo, hi = -4.0, 4.0
                for _ in range(60):
                    mid = 0.5 * (lo + hi)
                    if m.neg_curve_eval(c, np.array(m.LOG10_018 + mid), True) - self.dmin[j] > dt:
                        lo = mid
                    else:
                        hi = mid
                off[j] = 0.5 * (lo + hi)
        for _ in range(40):
            def f(o):
                self.off = o
                return np.log10(np.maximum(self.xyz(grey, o)[0], 1e-12))
            y0 = f(off)
            hs = 1e-4
            if self.mono:
                r = y0[1] - target[1]
                if abs(r) < 1e-11:
                    break
                dy = (f(off + hs)[1] - y0[1]) / hs
                off = off + np.clip(-r / dy, -0.5, 0.5)
                continue
            r = y0 - target
            if np.abs(r).max() < 1e-11:
                break
            J = np.zeros((3, 3))
            for k in range(3):
                e = np.zeros(3)
                e[k] = hs
                J[:, k] = (f(off + e) - y0) / hs
            off = off + np.clip(np.linalg.solve(J, -r), -0.5, 0.5)
        self.off = off

    def output709(self, rgb):
        X = self.xyz(rgb)
        lin = X @ np.linalg.inv(sp.npm(*sp.PRIMARIES['rec709'])).T
        return lin
