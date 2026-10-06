// ofxhost - image buffers, pixel format conversion, file IO and test patterns.
//
// Conventions:
//   * RGBAF is the host's interchange format: float RGBA, row-major, TOP-DOWN rows
//     (like image files). OFX images are bottom-up; conversion happens when an
//     OFX image is filled from / read back into an RGBAF.
//   * .f32 files are raw little-endian float32 RGBA, top-down, no header
//     (width/height come from the command line / report).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ofxhost {

struct RGBAF {
    int w = 0, h = 0;
    std::vector<float> px;   // w*h*4
    void resize(int W, int H, float fill = 0.f) { w = W; h = H; px.assign(size_t(W) * size_t(H) * 4u, fill); }
    float* at(int x, int y) { return px.data() + (size_t(y) * size_t(w) + size_t(x)) * 4u; }
    const float* at(int x, int y) const { return px.data() + (size_t(y) * size_t(w) + size_t(x)) * 4u; }
    bool empty() const { return w <= 0 || h <= 0; }
};

// Pixel layout of an OFX image as seen by the plug-in.
struct PixelFormat {
    std::string components;  // kOfxImageComponentRGBA / RGB / Alpha
    std::string depth;       // kOfxBitDepthFloat / Half / Short / Byte
    int nComps() const;
    int bytesPerComp() const;
    int bytesPerPixel() const { return nComps() * bytesPerComp(); }
};

// Memory for one OFX image. Bounds are in pixel coordinates (bottom-up).
struct ImageMemory {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    PixelFormat fmt;
    size_t rowBytes = 0;
    std::vector<unsigned char> storage;   // over-allocated for 64-byte alignment
    unsigned char* data = nullptr;
    void allocate(int bx1, int by1, int bx2, int by2, const PixelFormat& f);
    int width() const { return x2 - x1; }
    int height() const { return y2 - y1; }
    size_t bytes() const { return rowBytes * size_t(height()); }
};

// Fill an OFX image (bottom-up, bounds anywhere) from a top-down RGBAF that covers
// pixel window (0,0)-(src.w,src.h) in the same coordinate system.
void fillImageFromRGBA(ImageMemory& dst, const RGBAF& src);
// Read an OFX image back into a top-down RGBAF of size (W,H) covering (0,0)-(W,H).
// Pixels outside the image bounds become 0. RGB -> alpha 1; Alpha -> grey.
void readImageToRGBA(const ImageMemory& src, RGBAF& dst, int W, int H);
// Fill an image with a sentinel value (quiet NaN for float, 0xDEAD pattern otherwise).
void fillSentinel(ImageMemory& img);

// Area-weighted resample (exact for integer factors, box filter in general).
RGBAF resampleArea(const RGBAF& src, int W, int H);

// Half-float conversion.
uint16_t floatToHalf(float f);
float halfToFloat(uint16_t h);

// File IO
bool readF32(const std::string& path, int w, int h, RGBAF& out, std::string& err);
bool writeF32(const std::string& path, const RGBAF& img, std::string& err);

// Test patterns: "ramp", "hramp", "vramp", "bars", "grey[:v]", "checker[:n]", "noise[:seed]",
// "gradient", "zone" (log2 exposure wedge around 0.18), "black", "white".
bool makePattern(const std::string& spec, int w, int h, double time, RGBAF& out, std::string& err);

struct ImageStats {
    double min[4], max[4], mean[4];
    long long nan = 0, inf = 0;
};
ImageStats computeStats(const RGBAF& img);

} // namespace ofxhost
