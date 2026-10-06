// tests/license - trial watermark kernel (CPU build of the portable kernel).
#include "TestUtil.h"
#include "PngWrite.h"
#include "../../src/kernels/LicenseKernels.h"
#include "../../src/license/Watermark.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Img {
    int w = 0, h = 0;
    std::vector<float> px;   // RGBA, packed, row 0 = TOP
    Img() = default;
    Img(int W, int H) : w(W), h(H), px(size_t(W) * H * 4, 0.f) {}
    float* at(int x, int y) { return &px[(size_t(y) * w + x) * 4]; }
    const float* at(int x, int y) const { return &px[(size_t(y) * w + x) * 4]; }
};

Img solid(int w, int h, float r, float g, float b, float a = 1.f) {
    Img im(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) { float* p = im.at(x, y); p[0] = r; p[1] = g; p[2] = b; p[3] = a; }
    return im;
}

float fract(float v) { return v - std::floor(v); }

// A display-referred "photograph": dusk sky, sun, hills, a lit window wall, foliage texture.
Img scene(int w, int h) {
    Img im(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float u = (x + 0.5f) / w, v = (y + 0.5f) / h;   // v = 0 top
            float r = 0.18f + 0.62f * v, g = 0.30f + 0.38f * v, b = 0.62f - 0.20f * v;   // sky
            const float sd = std::hypot((u - 0.28f) * w / h, v - 0.42f);
            const float sun = std::exp(-sd * sd * 900.f) + 0.25f * std::exp(-sd * 14.f);
            r += sun; g += 0.85f * sun; b += 0.55f * sun;
            const float hill = 0.58f + 0.06f * std::sin(u * 9.f) + 0.03f * std::sin(u * 23.f + 1.f);
            if (v > hill) {
                const float t = (v - hill) / (1.f - hill);
                const float n = fract(std::sin(x * 12.9898f + y * 78.233f) * 43758.547f);
                r = 0.10f + 0.10f * (1 - t) + 0.05f * n; g = 0.14f + 0.12f * (1 - t) + 0.06f * n; b = 0.08f + 0.05f * n;
            }
            if (u > 0.72f && v > 0.66f) {   // pale lit wall with a window grid behind the text
                r = 0.86f; g = 0.82f; b = 0.74f;
                if (fract(u * 14.f) < 0.12f || fract(v * 9.f) < 0.12f) { r = 0.35f; g = 0.30f; b = 0.26f; }
            }
            if (u > 0.86f && v > 0.66f && v < 0.83f) { r = 0.95f; g = 0.93f; b = 0.88f; }   // white window
            float* p = im.at(x, y);
            p[0] = std::fmin(r, 1.f); p[1] = std::fmin(g, 1.f); p[2] = std::fmin(b, 1.f); p[3] = 1.f;
        }
    }
    return im;
}

// Runs the kernel over a top-down image (i[0] = 1) or, with bottomUp, over the same picture
// stored bottom-up with i[0] = 0 (OFX convention) and flips the result back for comparison.
Img stamp(const Img& src, CelPassArgs args, bool bottomUp = false) {
    Img in = src, out(src.w, src.h);
    if (bottomUp)
        for (int y = 0; y < src.h; ++y) std::memcpy(in.at(0, y), src.at(0, src.h - 1 - y), size_t(src.w) * 16);
    args.outW = src.w; args.outH = src.h; args.outStride = src.w * 4;
    args.inW[0] = src.w; args.inH[0] = src.h; args.inStride[0] = src.w * 4;
    args.i[0] = bottomUp ? 0 : 1;
    for (int y = 0; y < src.h; ++y)
        for (int x = 0; x < src.w; ++x)
            cel_k_watermark(x, y, &args, out.px.data(), in.px.data(), nullptr, nullptr, nullptr, nullptr);
    if (bottomUp) {
        Img flipped(src.w, src.h);
        for (int y = 0; y < src.h; ++y) std::memcpy(flipped.at(0, y), out.at(0, src.h - 1 - y), size_t(src.w) * 16);
        return flipped;
    }
    return out;
}

CelPassArgs defaultArgs() { CelPassArgs a; std::memset(&a, 0, sizeof(a)); return a; }

uint8_t to8(float v) { v = std::fmin(std::fmax(v, 0.f), 1.f); return uint8_t(std::lround(v * 255.f)); }

void savePng(const Img& im, const std::string& name, int x0 = 0, int y0 = 0, int cw = -1, int ch = -1, int zoom = 1) {
    if (celtest::outDir().empty()) return;
    if (cw < 0) cw = im.w;
    if (ch < 0) ch = im.h;
    std::vector<uint8_t> rgb(size_t(cw) * zoom * ch * zoom * 3);
    for (int y = 0; y < ch * zoom; ++y)
        for (int x = 0; x < cw * zoom; ++x) {
            const float* p = im.at(x0 + x / zoom, y0 + y / zoom);
            uint8_t* q = &rgb[(size_t(y) * cw * zoom + x) * 3];
            q[0] = to8(p[0]); q[1] = to8(p[1]); q[2] = to8(p[2]);
        }
    celtest::writePng(celtest::outDir() + "/" + name, cw * zoom, ch * zoom, rgb);
}

struct Box { int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1; int n = 0; };

// Bounding box of pixels that differ by more than eps, restricted to a region.
Box diffBox(const Img& a, const Img& b, float eps, int rx0, int ry0, int rx1, int ry1) {
    Box bx;
    for (int y = ry0; y < ry1; ++y)
        for (int x = rx0; x < rx1; ++x) {
            const float* p = a.at(x, y); const float* q = b.at(x, y);
            if (std::fabs(p[0] - q[0]) > eps || std::fabs(p[1] - q[1]) > eps || std::fabs(p[2] - q[2]) > eps) {
                bx.x0 = std::min(bx.x0, x); bx.y0 = std::min(bx.y0, y);
                bx.x1 = std::max(bx.x1, x); bx.y1 = std::max(bx.y1, y); ++bx.n;
            }
        }
    return bx;
}

} // namespace

CEL_TEST(watermark_font_table_consistent) {
    float sum = 0.f;
    for (int k = 0; k < CEL_WM_MSG_LEN; ++k) {
        const int g = int(cel_wm_msg[k]);
        CHECK(g >= 0 && g < CEL_WM_NGLYPHS);
        sum += float(cel_wm_font[g * CEL_WM_GLYPH_U32]);
    }
    sum += (CEL_WM_MSG_LEN - 1) * CEL_WM_TRACKING;
    CHECK_MSG(std::fabs(sum - CEL_WM_TEXT_W) < 1e-3f, "CEL_WM_TEXT_W must be " + std::to_string(sum));
    for (int g = 0; g < CEL_WM_NGLYPHS; ++g) {
        const int w = int(cel_wm_font[g * CEL_WM_GLYPH_U32]);
        CHECK(w >= 1 && w <= 5);
        for (int r = 0; r < 7; ++r) CHECK((cel_wm_font[g * CEL_WM_GLYPH_U32 + 1 + r] >> w) == 0u);
    }
}

CEL_TEST(watermark_untouched_outside_and_alpha_preserved) {
    const int W = 1920, H = 1080;
    Img src = scene(W, H);
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) src.at(x, y)[3] = 0.25f + 0.5f * (x % 3) / 2.f;
    const Img out = stamp(src, defaultArgs());
    // alpha copied everywhere
    bool alphaOk = true;
    for (size_t i = 3; i < src.px.size(); i += 4) alphaOk &= out.px[i] == src.px[i];
    CHECK(alphaOk);
    // regions far from the diagonal band and the text are bit-identical
    const Box tl = diffBox(src, out, 0.f, 0, 0, W / 4, H / 4);
    const Box bm = diffBox(src, out, 0.f, W / 2, H * 85 / 100, W * 7 / 10, H);
    const Box br = diffBox(src, out, 0.f, W / 2, H / 2, W, H);   // text lives here
    CHECK_MSG(tl.n == 0, std::to_string(tl.n));
    CHECK_MSG(bm.n == 0, std::to_string(bm.n));
    CHECK(br.n > 1000);
    // the band goes through the centre
    CHECK(diffBox(src, out, 0.f, W / 2 - 4, H / 2 - 4, W / 2 + 4, H / 2 + 4).n == 64);
    // text sits in the bottom-right corner with a margin, ~16-20% of the width
    const Box txt = diffBox(src, out, 0.02f, W * 3 / 5, H * 4 / 5, W, H);
    std::printf("    text bbox 1080p: x %d..%d  y %d..%d  (%d px)\n", txt.x0, txt.x1, txt.y0, txt.y1, txt.n);
    CHECK(txt.x1 < W - 30 && txt.y1 < H - 30);
    CHECK(txt.x0 > W * 0.75 && txt.x0 < W * 0.86);
    CHECK(txt.y0 > H * 0.92);
    // the image is never blown out: changes stay within the blend range
    bool inRange = true;
    for (size_t i = 0; i < src.px.size(); ++i) inRange &= out.px[i] >= 0.f && out.px[i] <= 1.f;
    CHECK(inRange);
}

CEL_TEST(watermark_row_order_and_determinism) {
    const Img src = scene(1280, 720);
    const Img a = stamp(src, defaultArgs(), false);
    const Img b = stamp(src, defaultArgs(), true);
    const Img c = stamp(src, defaultArgs(), false);
    CHECK(a.px == b.px);   // OFX bottom-up buffers give the same picture
    CHECK(a.px == c.px);
}

CEL_TEST(watermark_resolution_independent) {
    // UHD render box-downsampled 2x must match the HD render closely (same layout + weight).
    const Img hd = stamp(solid(1920, 1080, 0.2f, 0.2f, 0.2f), defaultArgs());
    const Img uhd = stamp(solid(3840, 2160, 0.2f, 0.2f, 0.2f), defaultArgs());
    double sum = 0, maxd = 0;
    int n = 0;
    for (int y = 900; y < 1080; ++y)
        for (int x = 1400; x < 1920; ++x) {
            for (int ch = 0; ch < 3; ++ch) {
                const float down = 0.25f * (uhd.at(2 * x, 2 * y)[ch] + uhd.at(2 * x + 1, 2 * y)[ch] +
                                            uhd.at(2 * x, 2 * y + 1)[ch] + uhd.at(2 * x + 1, 2 * y + 1)[ch]);
                const double d = std::fabs(down - hd.at(x, y)[ch]);
                sum += d; maxd = std::max(maxd, d); ++n;
            }
        }
    std::printf("    HD vs UHD/2 in text region: mean |d| %.5f, max |d| %.4f\n", sum / n, maxd);
    CHECK(sum / n < 0.004);
    CHECK(maxd < 0.25);
    // integrated "ink" (sum of brightening) per area is the same at both sizes (within 6%)
    double inkHd = 0, inkUhd = 0;
    for (auto& v : hd.px) inkHd += std::fmax(v - 0.2f, 0.f);
    for (auto& v : uhd.px) inkUhd += std::fmax(v - 0.2f, 0.f);
    inkUhd /= 4.0;
    std::printf("    ink HD %.1f  UHD/4 %.1f\n", inkHd, inkUhd);
    CHECK(std::fabs(inkHd - inkUhd) / inkHd < 0.06);
}

CEL_TEST(watermark_portrait_and_tiny_frames_fit) {
    const Img src = solid(1080, 1920, 0.1f, 0.1f, 0.1f);
    const Img out = stamp(src, defaultArgs());
    const Box b = diffBox(src, out, 0.02f, 1080 / 3, 1920 * 7 / 8, 1080, 1920);
    std::printf("    portrait text bbox: x %d..%d y %d..%d\n", b.x0, b.x1, b.y0, b.y1);
    CHECK(b.n > 0 && b.x0 > 1080 / 4 && b.x1 < 1080 - 10);
    // a thumbnail never crashes and still gets marked
    const Img t = solid(96, 54, 0.1f, 0.1f, 0.1f);
    const Img to = stamp(t, defaultArgs());
    CHECK(diffBox(t, to, 0.01f, 0, 0, 96, 54).n > 0);
    const Img one = solid(1, 1, 0.5f, 0.5f, 0.5f);
    const Img oo = stamp(one, defaultArgs());
    CHECK(std::isfinite(oo.px[0]) && oo.px[3] == 1.f);
}

CEL_TEST(watermark_params_clamped_never_invisible) {
    const Img src = solid(1920, 1080, 0.f, 0.f, 0.f);
    CelPassArgs a = defaultArgs();
    a.f[1] = 1e-6f; a.f[2] = 1e-6f; a.f[3] = 1e-6f; a.f[4] = 1e-6f;   // try to hide it
    const Img lo = stamp(src, a);
    float maxv = 0.f;
    for (int y = 900; y < 1080; ++y) for (int x = 1300; x < 1920; ++x) maxv = std::fmax(maxv, lo.at(x, y)[0]);
    std::printf("    min-clamped text peak on black: %.3f\n", maxv);
    CHECK(maxv > 0.05f);
    a.f[1] = NAN; a.f[2] = INFINITY; a.f[3] = -INFINITY; a.f[4] = NAN;   // garbage -> defaults
    const Img g = stamp(src, a);
    const Img d = stamp(src, defaultArgs());
    CHECK(g.px == d.px);
    // f[0] carries pixelScale in the plan; it must not change the result
    CelPassArgs ps = defaultArgs();
    ps.f[0] = 2.0f;
    CHECK(stamp(src, ps).px == d.px);
    // white level drives the text value (HDR placement)
    CelPassArgs pq = defaultArgs();
    cel::lic::fillWatermarkArgs(pq, cel::lic::WatermarkTransfer::PQ, true);
    const Img p = stamp(src, pq);
    float maxp = 0.f, maxd = 0.f;
    for (int y = 900; y < 1080; ++y) for (int x = 1300; x < 1920; ++x) {
        maxp = std::fmax(maxp, p.at(x, y)[1]); maxd = std::fmax(maxd, d.at(x, y)[1]);
    }
    std::printf("    text peak on black: SDR %.3f, PQ %.3f\n", maxd, maxp);
    CHECK(maxp < maxd && maxp > 0.2f);
    CHECK(maxd <= 0.92f * 0.70f + 0.02f);
}

CEL_TEST(watermark_band_is_faint) {
    const Img src = solid(1920, 1080, 0.f, 0.f, 0.f);
    const Img out = stamp(src, defaultArgs());
    const float centre = out.at(960, 540)[0];
    std::printf("    band lift on black at centre: %.4f\n", centre);
    CHECK(centre > 0.01f && centre < 0.06f);
    const Img w = stamp(solid(1920, 1080, 1.f, 1.f, 1.f), defaultArgs());
    CHECK(w.at(960, 540)[0] < 0.99f && w.at(960, 540)[0] > 0.94f);
}

CEL_TEST(watermark_render_images) {
    // Images for human review (written only when an output directory is given).
    const Img sc = scene(1920, 1080);
    const Img out = stamp(sc, defaultArgs());
    savePng(sc, "wm_scene_1080_clean.png");
    savePng(out, "wm_scene_1080.png");
    savePng(out, "wm_scene_1080_corner_x3.png", 1440, 960, 480, 120, 3);
    const Img uhd = stamp(scene(3840, 2160), defaultArgs());
    savePng(uhd, "wm_scene_2160_corner_x1.png", 2880, 1920, 960, 240, 1);
    const Img k = stamp(solid(1920, 1080, 0.f, 0.f, 0.f), defaultArgs());
    const Img gr = stamp(solid(1920, 1080, 0.45f, 0.45f, 0.45f), defaultArgs());
    const Img wh = stamp(solid(1920, 1080, 1.f, 1.f, 1.f), defaultArgs());
    savePng(k, "wm_black_corner_x3.png", 1440, 960, 480, 120, 3);
    savePng(gr, "wm_grey_corner_x3.png", 1440, 960, 480, 120, 3);
    savePng(wh, "wm_white_corner_x3.png", 1440, 960, 480, 120, 3);
    savePng(wh, "wm_white_1080.png");
    const Img hd720 = stamp(scene(1280, 720), defaultArgs());
    savePng(hd720, "wm_scene_720.png");
    const Img portrait = stamp(scene(1080, 1920), defaultArgs());
    savePng(portrait, "wm_scene_portrait.png");
    CHECK(true);
}
