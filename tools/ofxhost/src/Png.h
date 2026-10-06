// ofxhost - self-contained PNG writer (zlib/deflate with LZ77 + fixed Huffman,
// adaptive per-row PNG filters, CRC32/Adler32). No external dependencies.
#pragma once

#include "Image.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ofxhost {

struct PngOptions {
    bool sixteenBit = false;   // 16 bits per channel instead of 8
    bool alpha = false;        // write RGBA instead of RGB
    std::vector<std::pair<std::string, std::string>> text;  // tEXt chunks
};

// Values are clamped to [0,1] and quantised (no transfer function is applied:
// the plug-in output is already display encoded). NaN -> 0.
bool writePng(const std::string& path, const RGBAF& img, const PngOptions& opt, std::string& err);

// Exposed for tests.
std::vector<uint8_t> zlibCompress(const uint8_t* data, size_t n);
uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc = 0);
uint32_t adler32(const uint8_t* data, size_t n);

} // namespace ofxhost
