/* Texture kernels (texture agent): one CEL_KERNEL(name) line per kernel.
 * Bodies, f[]/i[] usage and buffer conventions: src/kernels/TextureKernels.h.
 * Pass graph that uses them: src/engine/Plan.cpp. */
CEL_KERNEL(tx_copy)          /* in0 -> out, plain copy (same size) */
CEL_KERNEL(tx_weave)         /* gate weave: in0 resampled (Catmull-Rom, de-ringed) at a per-frame offset + rotation */
CEL_KERNEL(tx_acut_luma)     /* acutance: in0 RGB -> out.x = log2 luminance */
CEL_KERNEL(tx_acut_apply)    /* acutance: in0 image, in1 blurred log2 luminance -> sharpened/softened image */
CEL_KERNEL(tx_pyr_down)      /* pyramid: 2x downsample of in0 ([1 3 3 1] binomial, all 4 channels) */
CEL_KERNEL(tx_blur4)         /* separable Gaussian, one direction, per-channel kernels (radius <= 7) */
CEL_KERNEL(tx_pyr_up_add)    /* pyramid: out = B-spline upsample(in0) + per-channel weight * in1 */
CEL_KERNEL(tx_up_v)          /* vertical half of a separable B-spline upsample (coarse width x target height) */
CEL_KERNEL(tx_hb_prefilter)  /* halation/bloom: per-pixel glow source (bloom RGB, halation energy) */
CEL_KERNEL(tx_hb_composite)  /* halation/bloom/vignette: in0 image + in1 glow pyramid -> image */
CEL_KERNEL(tx_vignette)      /* natural cos^4-like falloff, aspect-correct (stand-alone, when no glow pass) */
CEL_KERNEL(tx_tonemap)       /* Texture-Only: display-linear <-> film-like pseudo scene (exact bijection) */
CEL_KERNEL(tx_grain_gen)     /* grain: hashed white noise (4 independent channels) on the generation grid */
CEL_KERNEL(tx_grain_finish)  /* grain (v1.2): diagnostic dye-cloud grain field at a fixed exposure (the colour pass evaluates it itself) */
CEL_KERNEL(tx_params)        /* parameter table (8 x 1) from f[]: hands a following pass more than 32 floats */
CEL_KERNEL(tx_damage)        /* film damage field: dust, scratches, hair (DamageKernels.h; read by color / display_finish as in3) */
CEL_KERNEL(tx_hb_prefilter_down) /* v1.2 fusion: glow source + 2x downsample (tx_hb_prefilter -> tx_pyr_down in one pass) */
CEL_KERNEL(tx_diag)          /* v1.3 diagnostic views: module contribution (with vs without), halation mask, wipe */
CEL_KERNEL(tx_gate)          /* Lab & Gate: film gate (frame line, rounded corners, soft edge, gate hair): camera mask / printer-gate density / mask view */
