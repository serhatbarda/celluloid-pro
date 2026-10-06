"""Design plots of the stock library (needs matplotlib): reference/plots/*.png

    python3 reference/plots.py
"""
import os
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design  # noqa: E402
import model as m  # noqa: E402
import scenes  # noqa: E402
import spectral as sp  # noqa: E402

OUT = os.path.join(sp.HERE, 'plots')
COL = ['#c0392b', '#27ae60', '#2e5fd8']
DYE = ['#16a6b6', '#c2187a', '#d9b200']


def neg_sens_plot(lib):
    n = len(lib.negs)
    fig, axs = plt.subplots(3, 4, figsize=(16, 10))
    for i, ax in enumerate(axs.flat):
        if i >= n:
            ax.axis('off')
            continue
        neg = lib.negs[i]
        S = m.neg_sensitivities(neg)
        I = sp.illuminant(neg['balance'])
        for k in range(3 if neg['kind'] != 'bw' else 1):
            s = S[k] / S[k].max()
            ax.plot(sp.WL, np.log10(np.maximum(s, 1e-4)), color=COL[k] if neg['kind'] != 'bw' else 'k')
        ax.set_ylim(-3, 0.1)
        ax.set_title(neg['name'], fontsize=10)
        ax.set_xlabel('nm')
        ax.set_ylabel('log relative sensitivity')
        ax.plot(sp.WL, np.log10(I) * 0.5 - 2.5, color='#999', lw=0.8, ls='--')
        ax.grid(alpha=0.3)
    fig.suptitle('Designed negative spectral sensitivities (dashed: design illuminant, log, offset)')
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, 'neg_sensitivities.png'), dpi=90)
    plt.close(fig)


def dye_plot(lib):
    fig, axs = plt.subplots(2, 4, figsize=(16, 7))
    for ax, i in zip(axs[0], [1, 5, 7, 8]):
        neg = lib.negs[i]
        eps, sec = m.neg_dyes_scaled(lib, neg)
        for k in range(3):
            ax.plot(sp.WL, eps[k], color=DYE[k])
            if neg['kind'] != 'reversal':
                ax.plot(sp.WL, sec[k] * neg['mask'][k], color=DYE[k], ls=':', lw=0.9)
        ax.set_title(neg['name'] + ' dyes (END units; dotted = mask)', fontsize=9)
        ax.grid(alpha=0.3)
    for ax, j in zip(axs[1], [0, 2, 3, 4]):
        prt = lib.prints[j]
        eps = m.print_dyes(prt)
        base = m.base_density(prt['base'])
        eps = eps * m.visual_end_scale(eps, base, lib.lamps['projector'])[:, None]
        for k in range(3):
            ax.plot(sp.WL, eps[k], color=DYE[k])
        ax.plot(sp.WL, base, color='#777', ls='--')
        ax.set_title(prt['name'] + ' dyes + base', fontsize=9)
        ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, 'dyes.png'), dpi=90)
    plt.close(fig)


def hd_plot(lib):
    fig, axs = plt.subplots(3, 4, figsize=(16, 10))
    x = np.linspace(-4.5, 2.5, 400)
    for i, ax in enumerate(axs.flat):
        if i >= len(lib.negs):
            ax.axis('off')
            continue
        neg = lib.negs[i]
        d, _ = m.build_neg_curves(neg, 0.0, 0.0)
        xs = np.linspace(m.NEG_X0, m.NEG_X1, m.N_CURVE)
        for k in range(3):
            ax.plot(xs, d[k], color=COL[k] if neg['kind'] != 'bw' else 'k')
        d2, _ = m.build_neg_curves(neg, 2.0, 0.0)
        ax.plot(xs, d2[1], color='#555', ls='--', lw=0.8)
        ax.axvline(m.LOG10_018, color='#aaa', lw=0.8)
        ax.set_xlim(x[0], x[-1])
        ax.set_title(neg['name'] + ' (dashed: G, +2 push)', fontsize=9)
        ax.set_xlabel('log10 exposure (18% grey = line)')
        ax.set_ylabel('dye density (END)')
        ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, 'hd_negatives.png'), dpi=90)
    plt.close(fig)
    fig, ax = plt.subplots(1, 1, figsize=(7, 5))
    u = np.linspace(m.PRT_U0, m.PRT_U1, m.N_CURVE)
    for j, prt in enumerate(lib.prints):
        P = m.build_print_curves(prt)
        ax.plot(u, P[1], label=prt['name'])
    ax.set_xlabel('log10 print exposure (relative)')
    ax.set_ylabel('print dye density (END)')
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, 'hd_prints.png'), dpi=90)
    plt.close(fig)


def system_tone_plot(lib):
    stops = np.linspace(-10, 10, 401)
    grey = np.repeat((0.18 * 2.0 ** stops)[:, None], 3, 1)
    fig, axs = plt.subplots(1, 3, figsize=(18, 5.5))
    for i, neg in enumerate(lib.negs):
        o, _ = design.run(lib, m.Params(neg=i), grey)
        axs[0].plot(stops, o[:, 1], label=neg['name'])
    axs[0].set_title('Negatives on P1 Standard Projection (Rec.709 2.4, G channel)')
    for j, prt in enumerate(lib.prints):
        o, _ = design.run(lib, m.Params(neg=1, prt=j), grey)
        axs[1].plot(stops, o[:, 1], label=prt['name'])
    o, _ = design.run(lib, m.Params(neg=1, process='scan'), grey)
    axs[1].plot(stops, o[:, 1], 'k--', label='CN 200T Lab Scan')
    axs[1].set_title('Prints with CN 200T')
    for pk in (300, 1000, 4000):
        o, _ = design.run(lib, m.Params(neg=1, output='Rec2100_PQ', hdrPeak=float(pk)), grey)
        axs[2].plot(stops, m.pq_decode(o[:, 1]), label=f'PQ peak {pk}')
    o, _ = design.run(lib, m.Params(neg=1, output='Rec2100_PQ', hdrUnroll=0.0), grey)
    axs[2].plot(stops, m.pq_decode(o[:, 1]), 'k--', label='PQ unroll 0')
    axs[2].set_yscale('log')
    axs[2].set_title('HDR highlight unroll (nits)')
    for ax in axs:
        ax.grid(alpha=0.3)
        ax.legend(fontsize=7)
        ax.set_xlabel('stops from 18% grey')
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, 'system_tone.png'), dpi=90)
    plt.close(fig)


def upsampling_plot(lib):
    cols = scenes.chart_rgb()[:18]
    fig, ax = plt.subplots(1, 1, figsize=(9, 5))
    u, v, L1 = sp.oct_encode(cols)
    N = lib.N_OCT
    for k, (uu, vv) in enumerate(zip(u, v)):
        i = int(round((uu + 1) / 2 * (N - 1)))
        j = int(round((vv + 1) / 2 * (N - 1)))
        c = lib.up_coef[j, i]
        R = 0.5 + 0.5 * np.tanh(0.5 * (c[0] + c[1] * sp.TNORM + c[2] * sp.TNORM ** 2))
        rgb709 = np.clip(cols[k] @ sp.rgb_to_rgb('rec2020', 'rec709').T / 0.5, 0, 1) ** (1 / 2.2)
        ax.plot(sp.WL, R, color=rgb709)
    ax.set_title('Spectral upsampling: sigmoid-polynomial reflectance shapes of the chart colours')
    ax.set_xlabel('nm')
    ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, 'upsampling.png'), dpi=90)
    plt.close(fig)


def main():
    os.makedirs(OUT, exist_ok=True)
    lib = m.load()
    neg_sens_plot(lib)
    dye_plot(lib)
    hd_plot(lib)
    system_tone_plot(lib)
    upsampling_plot(lib)
    print('plots written to', OUT)


if __name__ == '__main__':
    main()
