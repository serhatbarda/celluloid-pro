// Celluloid Pro - texture preview renders (texture agent). Writes PNG contact sheets so the look
// of grain, halation, bloom, acutance and weave can be judged by eye at 100%.
//   tex_preview <outdir> [grain|glow|acut|weave|all]
#include "TexTestUtil.h"
#include <cstdlib>
#include <cstring>

using namespace textest;
using cel::tex::GrainFormat;
using cel::tex::TextureParams;

namespace {

std::string g_out = ".";

// copy a crop of `src` into `sheet` at (ox, oy)
struct Sheet {
    int w, h;
    std::vector<uint8_t> rgb;
    Sheet(int W, int H, uint8_t bg = 40) : w(W), h(H), rgb(size_t(W) * H * 3, bg) {}
    void put(int ox, int oy, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (ox + x < 0 || oy + y < 0 || ox + x >= w || oy + y >= h) return;
        uint8_t* p = &rgb[(size_t(oy + y) * w + (ox + x)) * 3];
        p[0] = r; p[1] = g; p[2] = b;
    }
    bool save(const std::string& name) { return writePNG(g_out + "/" + name, w, h, rgb); }
};

// grain on a flat field, rendered through a simple density -> display mapping:
// the grain is a density fluctuation (k stops RMS) around a grey of `stops` relative to 18%.
void grainTile(Sheet& sh, int ox, int oy, const Img& g, int cx, int cy, int size, double stops, double k, bool mono) {
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float* n = g.at(cx + x, cy + y);
            uint8_t c[3];
            for (int ch = 0; ch < 3; ++ch) {
                double nn = mono ? n[1] : n[ch];
                c[ch] = enc8(0.18 * std::exp2(stops + k * nn));
            }
            sh.put(ox, oy, x, y, c[0], c[1], c[2]);
        }
}

void grainSheets() {
    const GrainFormat fmts[5] = {GrainFormat::Super8, GrainFormat::Mm16, GrainFormat::Super35, GrainFormat::Mm65, GrainFormat::Stills135};
    const float soft[3] = {0.f, 0.5f, 1.f};
    for (int res = 0; res < 2; ++res) {
        int W = res ? 3840 : 1920, H = res ? 2160 : 1080;
        const int T = 256, gap = 6;
        Sheet sh(3 * (T + gap) + gap, 5 * (T + gap) + gap);
        for (int f = 0; f < 5; ++f)
            for (int s = 0; s < 3; ++s) {
                TextureParams tp;
                tp.grainEnabled = true; tp.grainFormat = fmts[f]; tp.grainSoftness = soft[s];
                tp.pixelScale = W / 1920.f; tp.time = 12;
                Img g = grainField(W, H, tp);
                double amp = cel::tex::grainFormatAmplitude(fmts[f]);
                grainTile(sh, gap + s * (T + gap), gap + f * (T + gap), g, W / 2 - T / 2, H / 2 - T / 2, T, 0.0, 0.16 * amp, false);
            }
        sh.save(res ? "grain_formats_2160p.png" : "grain_formats_1080p.png");
    }
    // tonal range for S35 at 1080p: -3, -1, 0, +1, +3 stops (vertical strip per stop), colour vs mono
    {
        const int T = 200, gap = 6;
        Sheet sh(5 * (T + gap) + gap, 2 * (T + gap) + gap);
        TextureParams tp; tp.grainEnabled = true; tp.pixelScale = 1.f; tp.time = 3;
        Img g = grainField(1920, 1080, tp);
        const double st[5] = {-3, -1, 0, 1, 3};
        for (int i = 0; i < 5; ++i) {
            grainTile(sh, gap + i * (T + gap), gap, g, 300 + i * T, 300, T, st[i], 0.16, false);
            grainTile(sh, gap + i * (T + gap), gap + T + gap, g, 300 + i * T, 300, T, st[i], 0.16, true);
        }
        sh.save("grain_tones_s35_1080p.png");
    }
    // 4x magnified S35 1080p so the grain structure is visible
    {
        TextureParams tp; tp.grainEnabled = true; tp.pixelScale = 1.f; tp.time = 3;
        const float soft2[3] = {0.f, 0.5f, 1.f};
        const int T = 96, Z = 4, gap = 6;
        Sheet sh(3 * (T * Z + gap) + gap, T * Z + 2 * gap);
        for (int s = 0; s < 3; ++s) {
            tp.grainSoftness = soft2[s];
            Img g = grainField(1920, 1080, tp);
            for (int y = 0; y < T * Z; ++y)
                for (int x = 0; x < T * Z; ++x) {
                    const float* n = g.at(900 + x / Z, 500 + y / Z);
                    sh.put(gap + s * (T * Z + gap), gap, x, y, enc8(0.18 * std::exp2(0.16 * n[0])),
                           enc8(0.18 * std::exp2(0.16 * n[1])), enc8(0.18 * std::exp2(0.16 * n[2])));
                }
        }
        sh.save("grain_s35_1080p_zoom4x.png");
    }
}

// ---- glow scene: lamps, practicals, neon lines, a window, on a dark interior --------------------
Img glowScene(int W, int H) {
    Img im(W, H, 0.012f, 0.011f, 0.010f);
    double s = W / 1920.0;
    auto disc = [&](double cx, double cy, double r, float R, float G, float B) {
        for (int y = std::max(0, int((cy - r - 2) * s)); y < std::min(H, int((cy + r + 2) * s)); ++y)
            for (int x = std::max(0, int((cx - r - 2) * s)); x < std::min(W, int((cx + r + 2) * s)); ++x) {
                double dx = (x + 0.5) / s - cx, dy = (y + 0.5) / s - cy;
                double d = std::sqrt(dx * dx + dy * dy);
                double cov = std::min(1.0, std::max(0.0, r + 0.5 - d));
                float* p = im.at(x, y);
                p[0] = float(p[0] * (1 - cov) + R * cov); p[1] = float(p[1] * (1 - cov) + G * cov); p[2] = float(p[2] * (1 - cov) + B * cov);
            }
    };
    auto rect = [&](double x0, double y0, double x1, double y1, float R, float G, float B) {
        for (int y = std::max(0, int(y0 * s)); y < std::min(H, int(y1 * s)); ++y)
            for (int x = std::max(0, int(x0 * s)); x < std::min(W, int(x1 * s)); ++x) {
                float* p = im.at(x, y); p[0] = R; p[1] = G; p[2] = B;
            }
    };
    // dim wall gradient + furniture
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float* p = im.at(x, y);
            double v = 0.02 + 0.03 * std::exp(-std::pow((x / s - 500) / 400.0, 2));
            p[0] = float(v * 1.1); p[1] = float(v); p[2] = float(v * 0.85);
        }
    rect(1200, 120, 1700, 560, 6.0f, 6.2f, 6.6f);            // bright window (+5 stops)
    rect(1200, 330, 1700, 345, 0.03f, 0.03f, 0.03f);          // window bar
    rect(1440, 120, 1455, 560, 0.03f, 0.03f, 0.03f);
    rect(200, 600, 1100, 1000, 0.09f, 0.07f, 0.05f);          // table
    disc(400, 300, 26, 40.f, 30.f, 16.f);                      // tungsten bulb (+8 stops)
    disc(800, 260, 8, 60.f, 48.f, 30.f);                       // small hot bulb
    disc(1000, 420, 3, 30.f, 30.f, 30.f);                      // tiny specular
    rect(150, 820, 1150, 826, 12.f, 1.2f, 4.0f);              // pink neon line (6 px)
    rect(150, 880, 1150, 882, 1.0f, 3.0f, 14.f);              // blue neon line (2 px)
    rect(150, 940, 1150, 943, 14.f, 5.0f, 0.6f);              // orange neon line (3 px)
    disc(1500, 850, 60, 2.0f, 1.8f, 1.5f);                     // moderately bright lampshade (+3.5 stops)
    disc(1750, 850, 40, 400.f, 380.f, 330.f);                  // extreme: sun-like
    return im;
}

void glowSheets() {
    for (int res = 0; res < 2; ++res) {
        int W = res ? 3840 : 1920, H = res ? 2160 : 1080;
        Img sc = glowScene(W, H);
        TextureParams off; off.pixelScale = W / 1920.f;
        if (!res) writeScenePNG(g_out + "/glow_scene_off.png", sc);
        struct V { const char* n; float hal, bloom, hue, thr, rad; };
        const V vs[] = {{"glow_default", 0.5f, 0.15f, 0.f, 2.5f, 20.f},
                        {"glow_strong", 1.5f, 0.15f, 0.f, 2.0f, 30.f},
                        {"glow_halation_only", 1.0f, 0.f, 0.f, 2.5f, 20.f},
                        {"glow_bloom_only", 0.f, 1.0f, 0.f, 2.5f, 20.f},
                        {"glow_hue_red", 1.0f, 0.f, -1.f, 2.5f, 20.f},
                        {"glow_hue_orange", 1.0f, 0.f, 1.f, 2.5f, 20.f}};
        for (const V& v : vs) {
            TextureParams tp = off;
            tp.halationAmount = v.hal; tp.bloomAmount = v.bloom; tp.halationHue = v.hue;
            tp.halationThreshold = v.thr; tp.halationRadius = v.rad;
            Img o = textured(sc, tp);
            if (!res) writeScenePNG(g_out + "/" + v.n + ".png", o);
            else if (std::strcmp(v.n, "glow_default") == 0) {
                // 4K: write the 1080-equivalent crop at full res around the bulb
                Img c(960, 540);
                for (int y = 0; y < 540; ++y) for (int x = 0; x < 960; ++x)
                    for (int ch = 0; ch < 4; ++ch) c.at(x, y)[ch] = o.at(300 + x, 280 + y)[ch];
                writeScenePNG(g_out + "/glow_default_2160p_crop.png", c);
            }
        }
    }
}

// ---- acutance: resolution chart-ish scene -----------------------------------------------------------
void acutSheets() {
    int W = 1920, H = 1080;
    Img im(W, H, 0.18f, 0.18f, 0.18f);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float* p = im.at(x, y);
            double v = 0.18;
            if (x < 640) v = (x / 40 + y / 40) % 2 ? 0.05 : 0.6;                      // checker
            else if (x < 1280) v = 0.18 * std::exp2(1.5 * std::sin((x - 640) * (x - 640) * 0.00025)); // chirp
            else v = (y % 120 < 60) ? 0.02 : (x > 1600 ? 4.0 : 0.5);                    // hard edges incl. bright
            double tex = 1.0 + 0.08 * std::sin(x * 0.9) * std::sin(y * 1.3);            // fine texture
            p[0] = p[1] = p[2] = float(v * tex);
        }
    writeScenePNG(g_out + "/acut_off.png", im);
    TextureParams tp; tp.pixelScale = 1.f;
    const float am[4] = {0.3f, 1.0f, 2.0f, -1.0f};
    const char* nm[4] = {"acut_0p3.png", "acut_1p0.png", "acut_2p0.png", "acut_m1p0.png"};
    for (int i = 0; i < 4; ++i) {
        tp.acutanceAmount = am[i]; tp.acutanceRadius = 2.f;
        writeScenePNG(g_out + "/" + nm[i], textured(im, tp));
    }
    TextureParams v; v.pixelScale = 1.f; v.vignette = 1.f;
    writeScenePNG(g_out + "/vignette_1p0.png", textured(Img(W, H, 0.18f, 0.18f, 0.18f), v));
}

void weaveReport() {
    std::printf("weave (amount 1, speed 1), px @1920:\n frame   dx      dy      rot(urad)\n");
    for (int f = 0; f < 48; ++f) {
        cel::tex::WeaveOffset o = cel::tex::weaveOffset(f, 1.f, 1.f);
        std::printf(" %3d  %+6.3f  %+6.3f  %+7.2f\n", f, o.dx, o.dy, o.rot * 1e6);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) g_out = argv[1];
    std::string what = argc > 2 ? argv[2] : "all";
    if (what == "grain" || what == "all") grainSheets();
    if (what == "glow" || what == "all") glowSheets();
    if (what == "acut" || what == "all") acutSheets();
    if (what == "weave" || what == "all") weaveReport();
    return 0;
}
