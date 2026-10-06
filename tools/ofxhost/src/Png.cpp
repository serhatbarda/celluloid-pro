// ofxhost - PNG writer.
#include "Png.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ofxhost {

// ---------------------------------------------------------------- checksums
uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
    return ~crc;
}

uint32_t adler32(const uint8_t* data, size_t n) {
    uint32_t a = 1, b = 0;
    const uint32_t MOD = 65521u;
    while (n > 0) {
        size_t chunk = std::min<size_t>(n, 5552);
        n -= chunk;
        for (size_t i = 0; i < chunk; ++i) { a += data[i]; b += a; }
        data += chunk;
        a %= MOD; b %= MOD;
    }
    return (b << 16) | a;
}

// ---------------------------------------------------------------- deflate
namespace {

struct BitWriter {
    std::vector<uint8_t> out;
    uint64_t acc = 0;
    int nbits = 0;
    void put(uint32_t bits, int n) {  // LSB-first
        acc |= uint64_t(bits) << nbits;
        nbits += n;
        while (nbits >= 8) { out.push_back(uint8_t(acc & 0xffu)); acc >>= 8; nbits -= 8; }
    }
    void putRev(uint32_t code, int n) {  // Huffman code, MSB-first
        uint32_t r = 0;
        for (int i = 0; i < n; ++i) r |= ((code >> i) & 1u) << (n - 1 - i);
        put(r, n);
    }
    void flush() { if (nbits > 0) { out.push_back(uint8_t(acc & 0xffu)); acc = 0; nbits = 0; } }
};

const int kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const int kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void putLiteral(BitWriter& bw, int v) {
    if (v < 144) bw.putRev(uint32_t(0x30 + v), 8);
    else if (v < 256) bw.putRev(uint32_t(0x190 + (v - 144)), 9);
    else if (v < 280) bw.putRev(uint32_t(v - 256), 7);
    else bw.putRev(uint32_t(0xC0 + (v - 280)), 8);
}

void putMatch(BitWriter& bw, int len, int dist) {
    int li = 28;
    while (li > 0 && kLenBase[li] > len) --li;
    if (len == 258) li = 28;
    else if (li == 28) li = 27;  // 258 has its own code; 227..257 use code 284
    putLiteral(bw, 257 + li);
    if (kLenExtra[li]) bw.put(uint32_t(len - kLenBase[li]), kLenExtra[li]);
    int di = 29;
    while (di > 0 && kDistBase[di] > dist) --di;
    bw.putRev(uint32_t(di), 5);
    if (kDistExtra[di]) bw.put(uint32_t(dist - kDistBase[di]), kDistExtra[di]);
}

} // namespace

std::vector<uint8_t> zlibCompress(const uint8_t* data, size_t n) {
    BitWriter bw;
    bw.out.reserve(n / 2 + 64);
    bw.out.push_back(0x78);
    bw.out.push_back(0x01);
    bw.put(1, 1);  // BFINAL
    bw.put(1, 2);  // BTYPE = fixed Huffman
    const int WIN = 32768, HBITS = 15, HSIZE = 1 << HBITS, MAXCHAIN = 48, MAXLEN = 258;
    std::vector<int> head(HSIZE, -1), prev(WIN, -1);
    auto hash3 = [&](size_t i) {
        uint32_t v = uint32_t(data[i]) | (uint32_t(data[i + 1]) << 8) | (uint32_t(data[i + 2]) << 16);
        return int((v * 2654435761u) >> (32 - HBITS));
    };
    size_t i = 0;
    auto insert = [&](size_t pos) {
        if (pos + 2 >= n) return;
        int h = hash3(pos);
        prev[pos & (WIN - 1)] = head[size_t(h)];
        head[size_t(h)] = int(pos);
    };
    while (i < n) {
        int bestLen = 0, bestDist = 0;
        if (i + 2 < n) {
            int h = hash3(i);
            int cand = head[size_t(h)];
            int chain = 0;
            size_t maxLen = std::min<size_t>(MAXLEN, n - i);
            while (cand >= 0 && chain < MAXCHAIN) {
                size_t dist = i - size_t(cand);
                if (dist > size_t(WIN - 1) || dist == 0) break;
                if (data[size_t(cand) + size_t(bestLen)] == data[i + size_t(bestLen)]) {
                    size_t l = 0;
                    while (l < maxLen && data[size_t(cand) + l] == data[i + l]) ++l;
                    if (int(l) > bestLen) { bestLen = int(l); bestDist = int(dist); if (l == maxLen) break; }
                }
                int nx = prev[size_t(cand) & (WIN - 1)];
                if (nx >= cand) break;
                cand = nx;
                ++chain;
            }
        }
        if (bestLen >= 3) {
            putMatch(bw, bestLen, bestDist);
            for (int k = 0; k < bestLen; ++k) insert(i + size_t(k));
            i += size_t(bestLen);
        } else {
            putLiteral(bw, data[i]);
            insert(i);
            ++i;
        }
    }
    putLiteral(bw, 256);
    bw.flush();
    uint32_t ad = adler32(data, n);
    bw.out.push_back(uint8_t(ad >> 24));
    bw.out.push_back(uint8_t(ad >> 16));
    bw.out.push_back(uint8_t(ad >> 8));
    bw.out.push_back(uint8_t(ad));
    return bw.out;
}

// ---------------------------------------------------------------- PNG
namespace {

void be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24)); v.push_back(uint8_t(x >> 16)); v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x));
}

void chunk(FILE* f, const char* type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> buf;
    be32(buf, uint32_t(payload.size()));
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), payload.begin(), payload.end());
    buf.insert(buf.end(), body.begin(), body.end());
    be32(buf, crc32(body.data(), body.size()));
    std::fwrite(buf.data(), 1, buf.size(), f);
}

inline int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

} // namespace

bool writePng(const std::string& path, const RGBAF& img, const PngOptions& opt, std::string& err) {
    if (img.empty()) { err = "empty image"; return false; }
    const int ch = opt.alpha ? 4 : 3;
    const int bpc = opt.sixteenBit ? 2 : 1;
    const size_t bpp = size_t(ch * bpc);
    const size_t stride = size_t(img.w) * bpp;
    std::vector<uint8_t> raw(stride * size_t(img.h));
    for (int y = 0; y < img.h; ++y) {
        uint8_t* row = raw.data() + size_t(y) * stride;
        for (int x = 0; x < img.w; ++x) {
            const float* p = img.at(x, y);
            for (int k = 0; k < ch; ++k) {
                float v = p[k];
                if (!(v >= 0.f)) v = 0.f;  // NaN and negatives
                if (v > 1.f) v = 1.f;
                if (bpc == 1) {
                    row[size_t(x) * bpp + size_t(k)] = uint8_t(std::floor(double(v) * 255.0 + 0.5));
                } else {
                    uint16_t s = uint16_t(std::floor(double(v) * 65535.0 + 0.5));
                    row[size_t(x) * bpp + size_t(k) * 2] = uint8_t(s >> 8);
                    row[size_t(x) * bpp + size_t(k) * 2 + 1] = uint8_t(s & 0xffu);
                }
            }
        }
    }
    // adaptive filtering: per row, pick the filter with the minimum sum of |signed residual|
    std::vector<uint8_t> filtered;
    filtered.reserve((stride + 1) * size_t(img.h));
    std::vector<uint8_t> cand[5];
    for (auto& c : cand) c.resize(stride);
    std::vector<uint8_t> zero(stride, 0);
    for (int y = 0; y < img.h; ++y) {
        const uint8_t* cur = raw.data() + size_t(y) * stride;
        const uint8_t* up = y > 0 ? raw.data() + size_t(y - 1) * stride : zero.data();
        long best = -1;
        int bestF = 0;
        for (int f = 0; f < 5; ++f) {
            long score = 0;
            for (size_t i = 0; i < stride; ++i) {
                int a = i >= bpp ? cur[i - bpp] : 0;
                int b = up[i];
                int c = i >= bpp ? up[i - bpp] : 0;
                int pred = 0;
                switch (f) {
                case 0: pred = 0; break;
                case 1: pred = a; break;
                case 2: pred = b; break;
                case 3: pred = (a + b) / 2; break;
                case 4: pred = paeth(a, b, c); break;
                }
                uint8_t r = uint8_t(cur[i] - pred);
                cand[f][i] = r;
                score += r < 128 ? r : 256 - r;
            }
            if (best < 0 || score < best) { best = score; bestF = f; }
        }
        filtered.push_back(uint8_t(bestF));
        filtered.insert(filtered.end(), cand[bestF].begin(), cand[bestF].end());
    }
    std::vector<uint8_t> z = zlibCompress(filtered.data(), filtered.size());

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { err = "cannot write " + path; return false; }
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    std::fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr;
    be32(ihdr, uint32_t(img.w));
    be32(ihdr, uint32_t(img.h));
    ihdr.push_back(uint8_t(bpc * 8));
    ihdr.push_back(opt.alpha ? 6 : 2);
    ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk(f, "IHDR", ihdr);
    for (auto& kv : opt.text) {
        std::vector<uint8_t> t(kv.first.begin(), kv.first.end());
        t.push_back(0);
        t.insert(t.end(), kv.second.begin(), kv.second.end());
        chunk(f, "tEXt", t);
    }
    chunk(f, "IDAT", z);
    chunk(f, "IEND", {});
    if (std::fclose(f) != 0) { err = "write failed on " + path; return false; }
    return true;
}

} // namespace ofxhost
