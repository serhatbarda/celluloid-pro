// Celluloid Pro - host-side helper for the trial watermark pass (kernel: cel_k_watermark in
// src/kernels/LicenseKernels.h). Header-only; for the plan builder (src/engine) and the OFX shell.
//
// The watermark pass is the LAST pass of a frame: in0 = the finished, encoded output image,
// out = the host destination (must not alias in0). With all f[]/i[] left at 0 the kernel uses
// its SDR defaults and the OFX bottom-up row order, so the minimal plan entry is simply
//     PassDesc p; p.kernel = KID_watermark; p.in[0] = <encoded image>; p.out = BUF_DST;
// Use fillWatermarkArgs() to place the text at reference white on HDR outputs, e.g.
//     fillWatermarkArgs(p.args, output == Rec2100_PQ ? WatermarkTransfer::PQ :
//                               output == Rec2100_HLG ? WatermarkTransfer::HLG : WatermarkTransfer::SDR);
#pragma once
#include "../kernels/KernelABI.h"

namespace cel { namespace lic {

enum class WatermarkTransfer : int { SDR = 0, PQ = 1, HLG = 2 };

// Output code value used as the text "white" (reference white for HDR, so the watermark is not
// a 1000-nit highlight): SDR 0.92, PQ 0.58 (~203 cd/m2, BT.2408), HLG 0.75 (BT.2408).
inline float watermarkWhiteLevel(WatermarkTransfer t) {
    switch (t) {
        case WatermarkTransfer::PQ:  return 0.58f;
        case WatermarkTransfer::HLG: return 0.75f;
        default:                     return 0.92f;
    }
}

// f[1] opacity, f[2] size, f[3] white level, f[4] band strength, i[0] row order
// (0 = row 0 is the bottom of the picture = OFX host images, 1 = row 0 is the top).
// f[0] is left alone (the plan builder may store pixelScale there; the kernel ignores it).
inline void fillWatermarkArgs(CelPassArgs& a, WatermarkTransfer t, bool rowZeroIsTop = false,
                              float opacity = 0.70f, float size = 1.0f) {
    a.f[1] = opacity;
    a.f[2] = size;
    a.f[3] = watermarkWhiteLevel(t);
    a.f[4] = 1.0f;
    a.i[0] = rowZeroIsTop ? 1 : 0;
}

}} // namespace cel::lic
