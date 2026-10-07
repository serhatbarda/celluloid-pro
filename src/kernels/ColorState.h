/* Celluloid Pro - colour state buffer layout (owned by core).
 *
 * core::Pipeline::prepare() fills ONE float buffer: a fixed 128-float header followed by the
 * LUT blocks whose offsets (in floats, from the start of the buffer) are stored in the header.
 * Integers are stored as exactly representable floats. Matrices are row-major 3x3.
 * The colour kernels (ColorKernels.h) read everything from here; nothing else is needed.
 *
 *   block              layout                                         domain / meaning
 *   OCT  (exposure)    N x N x 3, row = v index, column = u index       octahedral direction of scene RGB
 *                      (corner aligned on [-1,1]^2)                     -> log10 layer exposure per unit L1 magnitude
 *   NC   (neg curves)  3 x N (layer-major)                              log10 E in [NC_X0, NC_X1] -> dye density
 *   PL   (dye LUT)     N^3 x 3, index ((i0*N + i1)*N + i2)*3 + c        neg dye amounts [0, PL_MAX]^3 ->
 *                                                                       print: log10 print-layer exposure,
 *                                                                       scan:  log10 scanner-channel exposure
 *   PC   (curves 2)    3 x N                                            print: log10 H in [PC_U0,PC_U1] -> print dye;
 *                                                                       scan: log10 rel. exposure -> display density
 *   SC   (scan inv.)   3 x N                                            scanner density in [SD0_c, SD1_c] -> log10
 *                                                                       exposure relative to grey (neutral referenced)
 *   HA   (hue hold)    N x N x 4, row = v, column = u (as OCT)            scene direction -> CIELAB-scaled (a, b) of the
 *                                                                       film at grey luminance rel. to its neutral, 0, w
 *   HN   (hue hold)    3 x N                                            reference log exposure -> neutral XYZ (Y = 1)
 *   PJ   (projection)  K x 7, per spectral sample k (10 nm, 380..780):   [base density, eps_0, eps_1, eps_2,
 *                                                                        w_X, w_Y, w_Z] of the print dyes (or
 *                                                                        reversal dyes); XYZ is integrated per
 *                                                                        pixel (celc_project), dye amounts
 *                                                                        clamped to [0, PJ_MAX]
 */
#ifndef CEL_COLOR_STATE_H
#define CEL_COLOR_STATE_H

#define CEL_CS_MAGIC_VALUE   7341.0f
#define CEL_CS_LAYOUT_VALUE  4.0f
#define CEL_CS_HEADER        128

/* header indices */
#define CEL_CS_MAGIC        0
#define CEL_CS_LAYOUT       1
#define CEL_CS_TOTAL        2    /* total floats in the buffer */
#define CEL_CS_INPUT        3    /* core::InputSpace */
#define CEL_CS_OUTPUT       4    /* core::OutputSpace */
#define CEL_CS_KIND         5    /* 0 print, 1 lab scan, 2 reversal */
#define CEL_CS_MONO         6    /* 1 = black & white negative */
#define CEL_CS_HDR          7    /* 0 SDR, 1 PQ, 2 HLG */
#define CEL_CS_GAIN         8    /* 2^exposure */
#define CEL_CS_MIN          9    /* 9..17  input gamut -> Rec.2020 matrix */
#define CEL_CS_OCT_OFF      18
#define CEL_CS_OCT_N        19
#define CEL_CS_NC_OFF       20
#define CEL_CS_NC_N         21
#define CEL_CS_NC_X0        22
#define CEL_CS_NC_X1        23
#define CEL_CS_NIGAM        24   /* 24..26 1/gamma of the negative layers (interlayer normalisation) */
#define CEL_CS_INTER        27   /* 27..35 interlayer matrix (already x user interlayer) */
#define CEL_CS_LW           36   /* 36..38 layer weights for the luminance-like reference */
#define CEL_CS_DMIN         39   /* 39..41 negative D-min (dye amount at the clear end) */
#define CEL_CS_GSIG         42   /* 42..44 grain sigma per layer (x amount x push factor) */
#define CEL_CS_NB           45   /* negative bleach-bypass silver factor */
#define CEL_CS_OFF          46   /* 46..48 per-layer offsets: print log H / reversal log E / scan */
#define CEL_CS_PL_OFF       49
#define CEL_CS_PL_N         50
#define CEL_CS_PL_MAX       51
#define CEL_CS_PC_OFF       52
#define CEL_CS_PC_N         53
#define CEL_CS_PC_U0        54
#define CEL_CS_PC_U1        55
#define CEL_CS_PMIN         56   /* 56..58 print D-min (dye) */
#define CEL_CS_PB           59   /* print silver factor */
#define CEL_CS_PJ_OFF       60
#define CEL_CS_PJ_N         61   /* number of spectral samples K */
#define CEL_CS_PJ_MAX       62
#define CEL_CS_PJ_STRIDE    7    /* floats per spectral sample in the PJ block */
#define CEL_CS_MXYZ         63   /* 63..71 film colour -> D65-adapted display XYZ (CAT16 / scan Rec.2020) */
#define CEL_CS_MOUT         72   /* 72..80 display XYZ -> linear output RGB */
#define CEL_CS_LUM          81   /* 81..83 luminance weights of the output RGB */
#define CEL_CS_SD0          84   /* 84..86 scan: inversion LUT domain start (scanner density) per channel */
#define CEL_CS_SD1          87   /* 87..89 scan: inversion LUT domain end */
#define CEL_CS_MSCAN        90   /* 90..98 scan colour matrix */
#define CEL_CS_HDR_LREF     99   /* nits per display-relative unit */
#define CEL_CS_HDR_GMAX     100  /* log10 highlight unroll range */
#define CEL_CS_HDR_UNROLL   101
#define CEL_CS_HDR_PEAK     102  /* nits */
#define CEL_CS_REVSIGN      103  /* +1 negative, -1 reversal (curve direction) */
#define CEL_CS_TX_GSIG      104  /* Texture-Only display grain sigma */
#define CEL_CS_GRAIN_ON     105  /* 1 if any grain sigma > 0 */
#define CEL_CS_TX_PEAK      106  /* Texture-Only: display-linear encode scale for HDR (nits per unit = 100) */
#define CEL_CS_SC_OFF       107  /* scan inversion LUT (3 x N): scanner density -> log10 exposure rel. grey */
#define CEL_CS_SC_N         108
#define CEL_CS_HOLD         109  /* hue hold strength (0 = off) */
#define CEL_CS_HOLD_CHROMA  110  /* chroma hold (chroma x (1 + this * t)) */
#define CEL_CS_HOLD_A_OFF   111  /* anchor block: N x N x 4 (octahedral grid like OCT): CIELAB-scaled (a, b)
                                    of the film rendering at 18 % grey luminance (Y = 1) relative to the */
#define CEL_CS_HOLD_A_N     112  /*   grey neutral (see celc_hold), 0, validity weight */
#define CEL_CS_HOLD_N_OFF   113  /* neutral block: 3 x N over [CEL_HOLD_X0, CEL_HOLD_X1] of the reference */
#define CEL_CS_HOLD_N_N     114  /*   log exposure: XYZ of the film's neutral (Y = 1) */
#define CEL_CS_HOLD_N0      115  /* 115..117 XYZ (Y = 1) of the film's neutral at 18 % grey, points 0 (info) */
#define CEL_CS_MSCAN2       118  /* 118..126 scan root-polynomial terms: e += M2 * (sqrt(rg), sqrt(gb), sqrt(rb)) */
/* 127 reserved */

/* model constants shared by host and kernels (== reference/model.py) */
#define CEL_LOG10_018      (-0.7447274948966939f)
#define CEL_GRAIN_EPS      0.02f
#define CEL_HDR_E0         0.60f
#define CEL_HDR_ER         0.60f
#define CEL_GC_THR         0.80f
#define CEL_SDR_KNEE       0.95f
#define CEL_HDR_KNEE       0.90f
#define CEL_SCAN_FLOOR     1.0e-3f
#define CEL_HOLD_X0        (-6.0f)
#define CEL_HOLD_X1        4.0f
#define CEL_LOGK_FLOOR     (-10.0f)  /* floor of the layer log10 exposures (OCT block, celc_exposures) */
#define CEL_HOLD_C0        0.20f     /* chroma-hold roll-off (CIELAB C* / 200), see celc_hold */
#define CEL_HOLD_K         1.3f      /* hold weight t = hold * (1 - exp(-K stops^2)) */
#define CEL_D65_X          0.95045592705167f
#define CEL_D65_Z          1.08905775075988f

#endif /* CEL_COLOR_STATE_H */
