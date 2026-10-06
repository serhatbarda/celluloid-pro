// ofxhost - image buffers, conversion, IO, patterns.
#include "Image.h"

#include "ofxImageEffect.h"
#include "ofxGPURender.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>

namespace ofxhost {

int PixelFormat::nComps() const {
    if (components == kOfxImageComponentRGBA) return 4;
    if (components == kOfxImageComponentRGB) return 3;
    if (components == kOfxImageComponentAlpha) return 1;
    return 4;
}
int PixelFormat::bytesPerComp() const {
    if (depth == kOfxBitDepthFloat) return 4;
    if (depth == kOfxBitDepthHalf) return 2;
    if (depth == kOfxBitDepthShort) return 2;
    if (depth == kOfxBitDepthByte) return 1;
    return 4;
}

void ImageMemory::allocate(int bx1, int by1, int bx2, int by2, const PixelFormat& f) {
    x1 = bx1; y1 = by1; x2 = std::max(bx1, bx2); y2 = std::max(by1, by2);
    fmt = f;
    rowBytes = size_t(width()) * size_t(fmt.bytesPerPixel());
    storage.assign(bytes() + 64, 0);
    uintptr_t p = reinterpret_cast<uintptr_t>(storage.data());
    data = storage.data() + ((64 - (p & 63)) & 63);
}

// ---------------------------------------------------------------- half
uint16_t floatToHalf(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000u;
    uint32_t mant = x & 0x007fffffu;
    int exp = int((x >> 23) & 0xffu);
    if (exp == 255) return uint16_t(sign | 0x7c00u | (mant ? 0x200u : 0u));  // inf / nan
    int e = exp - 127 + 15;
    if (e >= 31) return uint16_t(sign | 0x7c00u);  // overflow -> inf
    if (e <= 0) {
        if (e < -10) return uint16_t(sign);
        mant |= 0x00800000u;
        int shift = 14 - e;
        uint32_t hm = mant >> shift;
        uint32_t rem = mant & ((1u << shift) - 1u);
        uint32_t half = 1u << (shift - 1);
        if (rem > half || (rem == half && (hm & 1u))) ++hm;
        return uint16_t(sign | hm);
    }
    uint32_t hm = mant >> 13;
    uint32_t rem = mant & 0x1fffu;
    uint32_t out = sign | (uint32_t(e) << 10) | hm;
    if (rem > 0x1000u || (rem == 0x1000u && (hm & 1u))) ++out;  // may carry into exponent: correct
    return uint16_t(out);
}

float halfToFloat(uint16_t h) {
    uint32_t sign = uint32_t(h & 0x8000u) << 16;
    int exp = (h >> 10) & 0x1f;
    uint32_t mant = h & 0x3ffu;
    uint32_t out;
    if (exp == 0) {
        if (mant == 0) out = sign;
        else {
            int e = -1;
            do { ++e; mant <<= 1; } while (!(mant & 0x400u));
            mant &= 0x3ffu;
            out = sign | (uint32_t(127 - 15 - e) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        out = sign | 0x7f800000u | (mant << 13);
    } else {
        out = sign | (uint32_t(exp - 15 + 127) << 23) | (mant << 13);
    }
    float f;
    std::memcpy(&f, &out, 4);
    return f;
}

// ---------------------------------------------------------------- conversion
static inline void storeComp(unsigned char* p, const std::string& depth, float v) {
    if (depth == kOfxBitDepthFloat) { std::memcpy(p, &v, 4); return; }
    if (depth == kOfxBitDepthHalf) { uint16_t h = floatToHalf(v); std::memcpy(p, &h, 2); return; }
    float c = std::isnan(v) ? 0.f : std::min(1.f, std::max(0.f, v));
    if (depth == kOfxBitDepthShort) { uint16_t s = uint16_t(std::lround(c * 65535.f)); std::memcpy(p, &s, 2); return; }
    *p = static_cast<unsigned char>(std::lround(c * 255.f));
}
static inline float loadComp(const unsigned char* p, const std::string& depth) {
    if (depth == kOfxBitDepthFloat) { float v; std::memcpy(&v, p, 4); return v; }
    if (depth == kOfxBitDepthHalf) { uint16_t h; std::memcpy(&h, p, 2); return halfToFloat(h); }
    if (depth == kOfxBitDepthShort) { uint16_t s; std::memcpy(&s, p, 2); return float(s) / 65535.f; }
    return float(*p) / 255.f;
}

void fillImageFromRGBA(ImageMemory& dst, const RGBAF& src) {
    const int nc = dst.fmt.nComps();
    const int bc = dst.fmt.bytesPerComp();
    const bool fastFloatRGBA = dst.fmt.depth == kOfxBitDepthFloat && nc == 4;
    for (int y = dst.y1; y < dst.y2; ++y) {
        unsigned char* row = dst.data + size_t(y - dst.y1) * dst.rowBytes;
        int sy = src.h - 1 - y;  // bottom-up -> top-down
        bool rowIn = sy >= 0 && sy < src.h;
        if (fastFloatRGBA && rowIn && dst.x1 >= 0 && dst.x2 <= src.w) {
            std::memcpy(row, src.at(dst.x1, sy), size_t(dst.width()) * 16u);
            continue;
        }
        for (int x = dst.x1; x < dst.x2; ++x) {
            unsigned char* p = row + size_t(x - dst.x1) * size_t(nc * bc);
            float c[4] = {0, 0, 0, 0};
            if (rowIn && x >= 0 && x < src.w) std::memcpy(c, src.at(x, sy), 16);
            if (nc == 4) for (int k = 0; k < 4; ++k) storeComp(p + k * bc, dst.fmt.depth, c[k]);
            else if (nc == 3) for (int k = 0; k < 3; ++k) storeComp(p + k * bc, dst.fmt.depth, c[k]);
            else storeComp(p, dst.fmt.depth, c[3]);
        }
    }
}

void readImageToRGBA(const ImageMemory& src, RGBAF& dst, int W, int H) {
    dst.resize(W, H, 0.f);
    const int nc = src.fmt.nComps();
    const int bc = src.fmt.bytesPerComp();
    for (int y = std::max(0, src.y1); y < std::min(H, src.y2); ++y) {
        const unsigned char* row = src.data + size_t(y - src.y1) * src.rowBytes;
        int dy = H - 1 - y;
        for (int x = std::max(0, src.x1); x < std::min(W, src.x2); ++x) {
            const unsigned char* p = row + size_t(x - src.x1) * size_t(nc * bc);
            float* o = dst.at(x, dy);
            if (nc == 4) for (int k = 0; k < 4; ++k) o[k] = loadComp(p + k * bc, src.fmt.depth);
            else if (nc == 3) { for (int k = 0; k < 3; ++k) o[k] = loadComp(p + k * bc, src.fmt.depth); o[3] = 1.f; }
            else { float a = loadComp(p, src.fmt.depth); o[0] = o[1] = o[2] = o[3] = a; }
        }
    }
}

void fillSentinel(ImageMemory& img) {
    if (img.fmt.depth == kOfxBitDepthFloat) {
        const float q = std::numeric_limits<float>::quiet_NaN();
        size_t n = img.bytes() / 4;
        float* f = reinterpret_cast<float*>(img.data);
        for (size_t i = 0; i < n; ++i) f[i] = q;
    } else if (img.fmt.depth == kOfxBitDepthHalf) {
        size_t n = img.bytes() / 2;
        uint16_t* h = reinterpret_cast<uint16_t*>(img.data);
        for (size_t i = 0; i < n; ++i) h[i] = 0x7e00u;
    } else {
        std::memset(img.data, 0xA5, img.bytes());
    }
}

RGBAF resampleArea(const RGBAF& src, int W, int H) {
    RGBAF out;
    out.resize(W, H);
    if (src.empty() || W <= 0 || H <= 0) return out;
    const double sx = double(src.w) / W, sy = double(src.h) / H;
    for (int y = 0; y < H; ++y) {
        double y0 = y * sy, y1 = (y + 1) * sy;
        for (int x = 0; x < W; ++x) {
            double x0 = x * sx, x1 = (x + 1) * sx;
            double acc[4] = {0, 0, 0, 0}, wsum = 0;
            for (int yy = int(std::floor(y0)); yy < int(std::ceil(y1)) && yy < src.h; ++yy) {
                double wy = std::min(y1, yy + 1.0) - std::max(y0, double(yy));
                if (wy <= 0) continue;
                for (int xx = int(std::floor(x0)); xx < int(std::ceil(x1)) && xx < src.w; ++xx) {
                    double wx = std::min(x1, xx + 1.0) - std::max(x0, double(xx));
                    if (wx <= 0) continue;
                    const float* p = src.at(xx, yy);
                    double w = wx * wy;
                    for (int k = 0; k < 4; ++k) acc[k] += w * p[k];
                    wsum += w;
                }
            }
            float* o = out.at(x, y);
            for (int k = 0; k < 4; ++k) o[k] = float(wsum > 0 ? acc[k] / wsum : 0.0);
        }
    }
    return out;
}

// ---------------------------------------------------------------- IO
bool readF32(const std::string& path, int w, int h, RGBAF& out, std::string& err) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { err = "cannot open " + path; return false; }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    size_t want = size_t(w) * size_t(h) * 16u;
    if (w <= 0 || h <= 0) {
        std::fclose(f);
        err = "--width/--height are required for raw .f32 input";
        return false;
    }
    if (sz < 0 || size_t(sz) != want) {
        std::fclose(f);
        err = path + ": size " + std::to_string(sz) + " bytes does not match " + std::to_string(w) + "x" +
              std::to_string(h) + " RGBA float32 (" + std::to_string(want) + " bytes)";
        return false;
    }
    out.resize(w, h);
    size_t n = std::fread(out.px.data(), 1, want, f);
    std::fclose(f);
    if (n != want) { err = "short read on " + path; return false; }
    return true;
}

bool writeF32(const std::string& path, const RGBAF& img, std::string& err) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { err = "cannot write " + path; return false; }
    size_t n = std::fwrite(img.px.data(), 4, img.px.size(), f);
    bool ok = n == img.px.size() && std::fclose(f) == 0;
    if (!ok) err = "write failed on " + path;
    return ok;
}

// ---------------------------------------------------------------- patterns
static std::vector<std::string> splitColon(const std::string& s) {
    std::vector<std::string> r;
    std::string cur;
    for (char c : s) { if (c == ':') { r.push_back(cur); cur.clear(); } else cur += c; }
    r.push_back(cur);
    return r;
}

bool makePattern(const std::string& spec, int w, int h, double time, RGBAF& out, std::string& err) {
    if (w <= 0 || h <= 0) { err = "pattern input needs --width and --height"; return false; }
    auto parts = splitColon(spec);
    const std::string& name = parts[0];
    auto argD = [&](size_t i, double d) { return parts.size() > i && !parts[i].empty() ? std::atof(parts[i].c_str()) : d; };
    out.resize(w, h, 0.f);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) out.at(x, y)[3] = 1.f;
    if (name == "black") return true;
    if (name == "white" || name == "grey" || name == "gray") {
        float v = name == "white" ? 1.f : float(argD(1, 0.18));
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) { float* p = out.at(x, y); p[0] = p[1] = p[2] = v; }
        return true;
    }
    if (name == "ramp" || name == "hramp") {
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            float v = w > 1 ? float(x) / float(w - 1) : 0.f;
            float* p = out.at(x, y); p[0] = p[1] = p[2] = v;
        }
        return true;
    }
    if (name == "vramp") {
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            float v = h > 1 ? float(y) / float(h - 1) : 0.f;
            float* p = out.at(x, y); p[0] = p[1] = p[2] = v;
        }
        return true;
    }
    if (name == "gradient") {
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            float* p = out.at(x, y);
            p[0] = w > 1 ? float(x) / float(w - 1) : 0.f;
            p[1] = h > 1 ? float(y) / float(h - 1) : 0.f;
            p[2] = 0.5f;
            p[3] = 0.25f + 0.75f * p[0];
        }
        return true;
    }
    if (name == "bars") {
        static const float bars[8][3] = {{1, 1, 1}, {1, 1, 0}, {0, 1, 1}, {0, 1, 0}, {1, 0, 1}, {1, 0, 0}, {0, 0, 1}, {0, 0, 0}};
        double level = argD(1, 0.75);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            int b = std::min(7, x * 8 / w);
            float* p = out.at(x, y);
            for (int k = 0; k < 3; ++k) p[k] = float(bars[b][k] * level);
        }
        return true;
    }
    if (name == "checker") {
        int n = std::max(1, int(argD(1, 8)));
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            float v = (((x * n / w) + (y * n / h)) & 1) ? 0.9f : 0.1f;
            float* p = out.at(x, y); p[0] = p[1] = p[2] = v;
        }
        return true;
    }
    if (name == "noise") {
        unsigned seed = unsigned(argD(1, 1)) + unsigned(std::lround(time * 7919.0));
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> U(0.f, 1.f);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            float* p = out.at(x, y); p[0] = U(rng); p[1] = U(rng); p[2] = U(rng);
        }
        return true;
    }
    if (name == "zone") {
        // horizontal exposure wedge: -8..+8 stops around argD(1) (default 0.18), linear values
        double mid = argD(1, 0.18);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            double st = -8.0 + 16.0 * (w > 1 ? double(x) / (w - 1) : 0.0);
            float v = float(mid * std::pow(2.0, st));
            float* p = out.at(x, y); p[0] = p[1] = p[2] = v;
        }
        return true;
    }
    err = "unknown pattern '" + name + "' (ramp, vramp, gradient, bars[:level], grey[:v], white, black, checker[:n], noise[:seed], zone[:mid])";
    return false;
}

ImageStats computeStats(const RGBAF& img) {
    ImageStats s;
    double sum[4] = {0, 0, 0, 0};
    long long cnt[4] = {0, 0, 0, 0};
    for (int k = 0; k < 4; ++k) { s.min[k] = std::numeric_limits<double>::infinity(); s.max[k] = -s.min[k]; }
    const size_t n = img.px.size() / 4;
    for (size_t i = 0; i < n; ++i) {
        for (int k = 0; k < 4; ++k) {
            float v = img.px[i * 4 + size_t(k)];
            if (std::isnan(v)) { s.nan++; continue; }
            if (std::isinf(v)) { s.inf++; continue; }
            s.min[k] = std::min(s.min[k], double(v));
            s.max[k] = std::max(s.max[k], double(v));
            sum[k] += v;
            cnt[k]++;
        }
    }
    for (int k = 0; k < 4; ++k) {
        s.mean[k] = cnt[k] ? sum[k] / double(cnt[k]) : 0.0;
        if (!cnt[k]) s.min[k] = s.max[k] = 0.0;
    }
    return s;
}

} // namespace ofxhost
