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
 *   NS   (stage axis)  4 x N (v1.3, only when a stage exit is active):  rows 0..2: printing density of each print
 *                      layer (Log Scan convention, above the clear      layer for a NEUTRAL scene at log10 grey level
 *                      negative) over X in [NS_X0, NS_X1];              X (strictly increasing); row 3: the scene-
 *                                                                       referred log10 level Y(X) that density maps
 *                                                                       back to (Negative -> Scene, Print Only)
 *   TN   (tone, v1.3)  1 x N (only when Tone is active): log2 scene    x = log2(Y / 0.18) of the scene luminance ->
 *                                                                       gain in stops (Zones + Filmic Contrast)
 *   DN   (dye sat.)    3 x N (only when Dye Saturation is active):     weighted dye density t -> the dye amounts of a
 *                                                                       NEUTRAL with that t (print, reversal or scan)
 */
#ifndef CEL_COLOR_STATE_H
#define CEL_COLOR_STATE_H

#define CEL_CS_MAGIC_VALUE   7341.0f
#define CEL_CS_LAYOUT_VALUE  7.0f   /* v1.3 Tone: 6 -> 7 (header 256, TN / DN blocks) */
#define CEL_CS_HEADER        256   /* v1.2: 128 -> 160; v1.3 WP-1: 160 -> 208 (indices 160..207 below);
                                       v1.3 Tone: 208 -> 256 (indices 224..255 below; 208..223 free) */

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
/* ---- v1.2 colour (stream A): camera emulation + hue-hold onset (indices 128..147; 148..149 v1.3 NT, 150..159 free) */
#define CEL_CS_EMU_ON       128  /* 1 = camera emulation active (scene-linear, in decode) */
#define CEL_CS_EMU_A        129  /* 129..137 linear part (already blended with the strength: I + s (A - I)) */
#define CEL_CS_EMU_B        138  /* 138..146 root-polynomial part x strength: + B (sqrt(rg), sqrt(gb), sqrt(rb)) */
#define CEL_CS_HOLD_ONSET   147  /* hue hold acts beyond this many stops from grey (celc_hold) */
/* ---- v1.3 WP-2: monotone black (NT block, 3 x N over [CEL_HOLD_X0, CEL_HOLD_X1] of the reference log exposure:
 * display-linear correction per unit luminance, added after the hue hold; see Core.cpp bakeBlackMonotone) */
#define CEL_CS_BLACK_OFF    148
#define CEL_CS_BLACK_N      149  /* 0 = off (the neutral ramp is already monotonic): bit-identical output */

/* ---- v1.3 WP-1: pipeline stage exits (indices 160..207). 0 / unset = Full chain (bit-identical to v1.2) */
#define CEL_CS_STAGE        160  /* 0 full, 1 negative -> log scan, 2 negative -> scene, 3 print only (core::Stage) */
#define CEL_CS_SCENE_TF     161  /* Negative -> Scene: transfer (CEL_IN_* code of ColorTransfer.h) */
#define CEL_CS_NS_OFF       162  /* NS block (4 x N, see the table above) */
#define CEL_CS_NS_N         163
#define CEL_CS_NS_X0        164
#define CEL_CS_NS_X1        165
#define CEL_CS_HCLEAR       166  /* 166..168 log10 print-layer exposure through the clear negative (D-min, no silver) */
#define CEL_CS_MSCENE       169  /* 169..177 scene-linear Rec.2020 -> Negative -> Scene output gamut */
#define CEL_CS_MMASK        178  /* 178..186 Negative -> Scene: grey-preserving dye-crosstalk correction (linear) */
#define CEL_CS_MASK_ON      187  /* 1 = apply MMASK */
#define CEL_CS_MMASKI       188  /* 188..196 inverse of MMASK (Print Only: scene level -> film layer level) */
/* 197..204 free */
/* ---- v1.3 ALEXA Rendering (indices 205..207; AL block, see celc_alexa_* in ColorKernels.h). Absent = off:
 * the block is only appended when Pipeline Stage = ALEXA Rec.709 or ALEXA Colour > 0 (bit-identical otherwise) */
#define CEL_CS_AL_OFF       205  /* offset of the AL block (0 = no block) */
#define CEL_CS_AL_RENDER    206  /* 1 = stage 4: camera -> ALEXA Rec.709 display rendering (no film) */
#define CEL_CS_AL_COLOUR    207  /* ALEXA Colour strength 0..1 in decode (scene-linear, before the negative) */
/* AL block layout (floats from CEL_CS_AL_OFF) */
#define CEL_AL_MR           0    /* 0..8    scene-linear Rec.2020 -> render RGB (render matrix x Rec.2020 -> AWG3) */
#define CEL_AL_TONE         9    /* 9..14   tone curve eg, c, es, ps, et, pt (on the LogC3 code) */
#define CEL_AL_RB           15   /* 15..47  render bands (CEL_AL_NB floats, layout: celc_al_bands) */
#define CEL_AL_CB           48   /* 48..80  ALEXA Colour bands */
#define CEL_AL_LG_R         81   /* Oklab L of the rendering's display grey */
#define CEL_AL_LG_C         82   /* Oklab L of scene grey 0.18 (ALEXA Colour) */
#define CEL_AL_DISP         83   /* display gain: 1 (SDR) or nits/100 per display unit (HDR: grey at the HDR grey) */
#define CEL_AL_M709_2020    84   /* 84..92  linear Rec.709 -> Rec.2020 */
#define CEL_AL_M2020_709    93   /* 93..101 linear Rec.2020 -> Rec.709 */
#define CEL_AL_MOUT         102  /* 102..110 display-linear Rec.709 -> linear output RGB */
#define CEL_AL_OK1          111  /* 111..119 Oklab M1 (linear Rec.709 -> LMS) */
#define CEL_AL_OK2          120  /* 120..128 Oklab M2 (LMS^(1/3) -> Lab) */
#define CEL_AL_OK2I         129  /* 129..137 inverse of M2 */
#define CEL_AL_OK1I         138  /* 138..146 inverse of M1 */
#define CEL_AL_Y2020        147  /* 147..149 luminance weights of linear Rec.2020 */
#define CEL_AL_SIZE         150
#define CEL_AL_NB           33   /* band floats: dh0 7, dh1 5, ds0 7, ds1 5, dl0 5, s2 1, skin 3 */
#define CEL_AL_U_GREY       0.391006832f  /* LogC3 (EI 800) code of 18 % grey (pivot of the tone curve) */
#define CEL_AL_C0           0.02f      /* chroma fade-in of the bands (Oklab C) */
#define CEL_AL_FLOOR        1.5e-5f    /* display-linear soft floor (celc_al_floor), below the neutral black 4.3e-5 */

/* ---- v1.3 Tone (indices 224..255). 0 / unset = off (bit-identical to the render without it) */
#define CEL_CS_TONE_ON      224  /* 1 = scene tone (Zones + Filmic Contrast) active, in decode after Exposure */
#define CEL_CS_TONE_OFF     225  /* TN block: 1 x N, log2 gain E(x) in stops over x = scene stops re 18 % grey */
#define CEL_CS_TONE_N       226
#define CEL_CS_TONE_X0      227  /* table domain (stops) */
#define CEL_CS_TONE_X1      228
#define CEL_CS_TONE_K0      229  /* dE/dx below X0 (linear extrapolation) */
#define CEL_CS_TONE_K1      230  /* dE/dx above X1 */
#define CEL_CS_TONE_KAPPA   231  /* chroma coupling of the contrast: channel ratios to luminance ^ kappa (1 = ratio-preserving) */
#define CEL_CS_DSAT_ON      232  /* 1 = subtractive (dye) saturation active */
#define CEL_CS_DSAT_S       233  /* saturation: factor on the dye deviation from the pixel's neutral */
#define CEL_CS_DSAT_RICH    234  /* richness: neutral dye added per unit of saturation change x the colour excess (celc_dsat) */
#define CEL_CS_DSAT_BAL     235  /* highlight / shadow balance: S x 2^(bal u), u = +1 highlights .. -1 shadows */
#define CEL_CS_DSAT_TG      236  /* weighted dye density t of the neutral at 18 % grey */
#define CEL_CS_DSAT_W       237  /* balance transition width (density) */
#define CEL_CS_DSAT_OFF     238  /* DN block: 3 x N, dye amounts of the neutral over t in [T0, T1] */
#define CEL_CS_DSAT_N       239
#define CEL_CS_DSAT_T0      240
#define CEL_CS_DSAT_T1      241
#define CEL_CS_ROLL_ON      242  /* info: 1 = the print curve carries a Film Roll-off (baked into PC, no kernel term) */
/* 243..255 free */
#define CEL_TONE_LW_R       0.26270021f   /* Rec.2020 luminance (scene, zone position) */
#define CEL_TONE_LW_G       0.67799807f
#define CEL_TONE_LW_B       0.05930172f
#define CEL_DSAT_EPS        0.02f         /* smooth split of the dye deviation into its +/- parts (density) */

/* Log Scan code values (Cineon-style printing density): code = (CV0 + PD / STEP) / 1023, PD = printing density
 * above the clear (D-min) negative, 0.002 density per 10-bit code value, D-min at code 95 */
#define CEL_LOGSCAN_CV0     95.0f
#define CEL_LOGSCAN_STEP    0.002f

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
