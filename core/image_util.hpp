// Bilinear RGBA resize, used to match the raster size the renderer asks for (power-of-two textures).
#pragma once
#include <algorithm>
#include <cmath>
#include "png_decode.hpp"
namespace wl {
inline int nextPow2(int v) { int p = 1; while (p < v) p <<= 1; return p; }
inline Image resizeBilinear(const Image& s, int nw, int nh) {
    Image d; d.w = nw; d.h = nh; d.rgba.assign((size_t)nw * nh * 4, 0);
    if (s.w <= 0 || s.h <= 0 || nw <= 0 || nh <= 0) return d;
    for (int y = 0; y < nh; ++y) {
        float fy = (y + 0.5f) * s.h / nh - 0.5f; int y0 = (int)std::floor(fy); float ty = fy - y0;
        int ya = std::clamp(y0, 0, s.h - 1), yb = std::clamp(y0 + 1, 0, s.h - 1);
        for (int x = 0; x < nw; ++x) {
            float fx = (x + 0.5f) * s.w / nw - 0.5f; int x0 = (int)std::floor(fx); float tx = fx - x0;
            int xa = std::clamp(x0, 0, s.w - 1), xb = std::clamp(x0 + 1, 0, s.w - 1);
            for (int c = 0; c < 4; ++c) {
                float p00 = s.rgba[((size_t)ya * s.w + xa) * 4 + c], p10 = s.rgba[((size_t)ya * s.w + xb) * 4 + c];
                float p01 = s.rgba[((size_t)yb * s.w + xa) * 4 + c], p11 = s.rgba[((size_t)yb * s.w + xb) * 4 + c];
                float v = (p00 * (1 - tx) + p10 * tx) * (1 - ty) + (p01 * (1 - tx) + p11 * tx) * ty;
                d.rgba[((size_t)y * nw + x) * 4 + c] = (uint8_t)std::clamp((int)std::lround(v), 0, 255);
            }
        }
    }
    return d;
}
} // namespace wl
