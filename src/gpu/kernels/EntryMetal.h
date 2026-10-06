/* Celluloid Pro - Metal Shading Language entry points for the uniform kernel ABI (gpu role).
 * CEL_GPU_ENTRY(name) defines  kernel void cel_<name>(...)  wrapping cel_k_<name>.
 * Buffer indices (bound generically by src/gpu/metal/MetalBackend.mm):
 *   [[buffer(0)]] constant CelPassArgs (setBytes, per pass)
 *   [[buffer(1)]] out, [[buffer(2..5)]] in0..in3 (unused inputs bound to a dummy buffer),
 *   [[buffer(6)]] state
 * The grid is dispatched in whole threadgroups: out-of-range threads return early. */
#ifndef CEL_GPU_ENTRY_METAL_H
#define CEL_GPU_ENTRY_METAL_H

#define CEL_GPU_ENTRY(name)                                                                     \
kernel void cel_##name(constant CelPassArgs* cel_a [[buffer(0)]],                               \
                       device float* cel_out [[buffer(1)]],                                     \
                       device const float* cel_in0 [[buffer(2)]],                               \
                       device const float* cel_in1 [[buffer(3)]],                               \
                       device const float* cel_in2 [[buffer(4)]],                               \
                       device const float* cel_in3 [[buffer(5)]],                               \
                       device const float* cel_state [[buffer(6)]],                             \
                       uint2 cel_gid [[thread_position_in_grid]]) {                             \
    const int cel_x = (int)cel_gid.x;                                                           \
    const int cel_y = (int)cel_gid.y;                                                           \
    if (cel_x >= cel_a->outW || cel_y >= cel_a->outH) return;                                   \
    cel_k_##name(cel_x, cel_y, cel_a, cel_out, cel_in0, cel_in1, cel_in2, cel_in3, cel_state);  \
}

#endif /* CEL_GPU_ENTRY_METAL_H */
