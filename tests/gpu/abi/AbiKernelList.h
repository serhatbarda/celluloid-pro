/* Reafilm Pro - GPU ABI test kernel set (X-macro list). Every entry is a kernel with the
 * uniform ABI exercising one aspect of the backends (args layout, strides, 4 inputs, state
 * buffer, size changes, math parity). Used by tests/gpu/test_gpu_abi.cpp only. */
CEL_KERNEL(t_gradient)   /* no inputs: function of (x, y, f[0..3], i[0..1]) */
CEL_KERNEL(t_add4)       /* out = in0 + 2 in1 + 3 in2 + 4 in3, inputs sampled at proportional coords */
CEL_KERNEL(t_state)      /* out = in0 * state[i[0] + hash(x,y) % i[1]] + state[i[2]] */
CEL_KERNEL(t_math)       /* every cel_* math wrapper of in0 */
CEL_KERNEL(t_down2)      /* half resolution box filter of in0 */
CEL_KERNEL(t_bilinear)   /* cel_sample_bilinear of in0 at scaled coords (f[0], f[1] scale, f[2], f[3] offset) */
CEL_KERNEL(t_args)       /* echoes every CelPassArgs field */
CEL_KERNEL(t_hash)       /* integer hash noise, must be bit exact */
CEL_KERNEL(t_unused)     /* reports which inputs are unused (stride 0) and reads only used ones */
