// Celluloid Pro - texture test utilities (images, running plans on the CPU backend, PNG output).
#pragma once
#include "engine/Engine.h"
#include "texture/TexturePlan.h"
#include "texture/TextureDesign.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace textest {

struct Img {
    int w = 0, h = 0;
    std::vector<float> px;   // RGBA, packed
    Img() = default;
    Img(int W, int H, float r = 0.f, float g = 0.f, float b = 0.f, float a = 1.f) : w(W), h(H), px(size_t(W) * H * 4) {
        for (size_t i = 0; i < size_t(W) * H; ++i) { px[i * 4] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = a; }
    }
    float* at(int x, int y) { return px.data() + (size_t(y) * w + x) * 4; }
    const float* at(int x, int y) const { return px.data() + (size_t(y) * w + x) * 4; }
};

inline cel::engine::HostImage host(Img& im) {
    cel::engine::HostImage h;
    h.data = im.px.data(); h.w = im.w; h.h = im.h; h.rowStrideFloats = im.w * 4;
    return h;
}

inline bool runPlan(const cel::engine::Plan& plan, Img& src, Img& dst, std::string* err = nullptr,
                    const std::vector<float>* state = nullptr) {
    static const std::vector<float> empty;
    std::string e;
    bool ok = cel::engine::cpuBackend().execute(plan, state ? *state : empty, 1, host(src), host(dst),
                                                cel::engine::GpuContext{}, &e);
    if (!ok) { std::fprintf(stderr, "execute failed: %s\n", e.c_str()); if (err) *err = e; }
    return ok;
}

inline Img textured(const Img& src, const cel::tex::TextureParams& tp, bool dr = false, bool hdr = false) {
    Img s = src, d(src.w, src.h);
    runPlan(cel::tex::buildTexturePlan(src.w, src.h, tp, dr, hdr), s, d);
    return d;
}
// grain v2 density fluctuation (units: grain sigma) at a layer exposure of `stops` around 18 % grey
inline Img grainField(int w, int h, const cel::tex::TextureParams& tp, float stops = 0.f) {
    Img s(w, h), d(w, h);
    runPlan(cel::tex::buildGrainPlan(w, h, tp, stops), s, d);
    return d;
}

// ---- PNG writer (uncompressed deflate "stored" blocks; no zlib dependency) --------------------
inline uint32_t crc32(const uint8_t* d, size_t n, uint32_t c = 0xffffffffu) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t v = i;
            for (int k = 0; k < 8; ++k) v = (v & 1u) ? 0xedb88320u ^ (v >> 1) : v >> 1;
            table[i] = v;
        }
        init = true;
    }
    for (size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 255u] ^ (c >> 8);
    return c;
}
inline bool writePNG(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
    std::vector<uint8_t> raw;
    raw.reserve(size_t(h) * (size_t(w) * 3 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + size_t(y) * w * 3, rgb.begin() + size_t(y + 1) * w * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    uint32_t s1 = 1, s2 = 0;
    for (uint8_t b : raw) { s1 = (s1 + b) % 65521u; s2 = (s2 + s1) % 65521u; }
    do {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        bool last = pos + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(uint8_t(n & 255)); z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n & 255)); z.push_back(uint8_t((~n >> 8) & 255));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    } while (pos < raw.size());
    uint32_t ad = (s2 << 16) | s1;
    for (int i = 3; i >= 0; --i) z.push_back(uint8_t(ad >> (8 * i)));
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    auto be32 = [&](uint32_t v) { uint8_t b[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}; std::fwrite(b, 1, 4, f); };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        be32(uint32_t(data.size()));
        std::vector<uint8_t> td(type, type + 4);
        td.insert(td.end(), data.begin(), data.end());
        std::fwrite(td.data(), 1, td.size(), f);
        be32(crc32(td.data(), td.size()) ^ 0xffffffffu);
    };
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    std::fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr = {uint8_t(w >> 24), uint8_t(w >> 16), uint8_t(w >> 8), uint8_t(w),
                                 uint8_t(h >> 24), uint8_t(h >> 16), uint8_t(h >> 8), uint8_t(h), 8, 2, 0, 0, 0};
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    std::fclose(f);
    return true;
}

// scene-linear -> display code value through the film-like reference tone curve (preview only)
inline uint8_t enc8(double sceneLinear, double dither = 0.0) {
    double Y = cel::tex::toneMapToDisplay(std::max(0.0, sceneLinear));
    double v = std::pow(std::min(1.0, std::max(0.0, Y)), 1.0 / 2.4);
    return uint8_t(std::min(255.0, std::max(0.0, std::floor(v * 255.0 + 0.5 + dither))));
}
// triangular-PDF dither in [-1, 1] code values (hides 8-bit banding in previews)
inline double tpdf(uint32_t i) {
    auto h = [](uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; };
    return (double(h(i)) + double(h(i ^ 0x9e3779b9U))) / 4294967296.0 - 1.0;
}
inline bool writeScenePNG(const std::string& path, const Img& im) {
    std::vector<uint8_t> rgb(size_t(im.w) * im.h * 3);
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x)
            for (int c = 0; c < 3; ++c)
                rgb[(size_t(y) * im.w + x) * 3 + c] = enc8(im.at(x, y)[c], tpdf(uint32_t((y * im.w + x) * 3 + c)));
    return writePNG(path, im.w, im.h, rgb);
}

inline double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

} // namespace textest
