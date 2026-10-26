// Self-contained PNG decoder -> RGBA8. No zlib/libpng dependency (own inflate), so the ASI has no external DLLs.
// Supports: non-interlaced PNG, color types 0/2/3/4/6, bit depths 1/2/4/8/16 (16 is reduced to 8), tRNS.
// Rejects (with a message): interlaced images, truncated/corrupt streams, CRC-less garbage, absurd sizes.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace wl {
struct Image { int w = 0, h = 0; std::vector<uint8_t> rgba; };

namespace png_detail {
struct BitReader {
    const uint8_t* p; size_t n, pos = 0; uint32_t bitbuf = 0; int bitcnt = 0; bool err = false;
    BitReader(const uint8_t* d, size_t len) : p(d), n(len) {}
    uint32_t bits(int need) {
        while (bitcnt < need) {
            if (pos >= n) { err = true; return 0; }
            bitbuf |= (uint32_t)p[pos++] << bitcnt; bitcnt += 8;
        }
        uint32_t v = bitbuf & ((1u << need) - 1);
        bitbuf >>= need; bitcnt -= need; return v;
    }
};
struct Huff { uint16_t count[16]; uint16_t symbol[288]; };
inline bool build(Huff& h, const uint8_t* len, int n) {
    std::memset(h.count, 0, sizeof h.count);
    for (int i = 0; i < n; ++i) h.count[len[i]]++;
    if (h.count[0] == n) return true; // no codes (allowed for distance tree)
    int left = 1;
    for (int l = 1; l < 16; ++l) { left <<= 1; left -= h.count[l]; if (left < 0) return false; }
    uint16_t offs[16]; offs[1] = 0;
    for (int l = 1; l < 15; ++l) offs[l + 1] = offs[l] + h.count[l];
    for (int i = 0; i < n; ++i) if (len[i]) h.symbol[offs[len[i]]++] = (uint16_t)i;
    return true;
}
inline int decode(BitReader& br, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; ++l) {
        code |= (int)br.bits(1); if (br.err) return -1;
        int c = h.count[l];
        if (code - c < first) return h.symbol[index + (code - first)];
        index += c; first += c; first <<= 1; code <<= 1;
    }
    return -1;
}
// Raw DEFLATE. maxOut guards against decompression bombs.
inline bool inflate(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, size_t maxOut, std::string& err) {
    static const uint16_t lbase[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const uint16_t lext[] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const uint16_t dbase[] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const uint16_t dext[] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    BitReader br(src, srcLen);
    int last;
    do {
        last = (int)br.bits(1); int type = (int)br.bits(2);
        if (br.err) { err = "truncated deflate stream"; return false; }
        if (type == 0) {
            br.bitbuf = 0; br.bitcnt = 0;
            if (br.pos + 4 > br.n) { err = "truncated stored block"; return false; }
            unsigned len = br.p[br.pos] | (br.p[br.pos + 1] << 8), nlen = br.p[br.pos + 2] | (br.p[br.pos + 3] << 8);
            br.pos += 4;
            if ((len ^ 0xFFFF) != nlen) { err = "bad stored block"; return false; }
            if (br.pos + len > br.n) { err = "truncated stored block"; return false; }
            if (out.size() + len > maxOut) { err = "image too large"; return false; }
            out.insert(out.end(), br.p + br.pos, br.p + br.pos + len); br.pos += len;
        } else if (type == 1 || type == 2) {
            Huff lit, dist;
            if (type == 1) {
                uint8_t l[288]; int i = 0;
                for (; i < 144; ++i) l[i] = 8;
                for (; i < 256; ++i) l[i] = 9;
                for (; i < 280; ++i) l[i] = 7;
                for (; i < 288; ++i) l[i] = 8;
                build(lit, l, 288);
                uint8_t d[30]; for (i = 0; i < 30; ++i) d[i] = 5; build(dist, d, 30);
            } else {
                int nlen = (int)br.bits(5) + 257, ndist = (int)br.bits(5) + 1, ncode = (int)br.bits(4) + 4;
                if (br.err || nlen > 286 || ndist > 30) { err = "bad dynamic header"; return false; }
                static const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
                uint8_t lengths[320] = {0};
                for (int i = 0; i < ncode; ++i) lengths[order[i]] = (uint8_t)br.bits(3);
                Huff lencode; if (!build(lencode, lengths, 19)) { err = "bad code lengths"; return false; }
                int idx = 0; uint8_t ll[320] = {0};
                while (idx < nlen + ndist) {
                    int sym = decode(br, lencode); if (sym < 0) { err = "bad code"; return false; }
                    if (sym < 16) ll[idx++] = (uint8_t)sym;
                    else {
                        int rep, val = 0;
                        if (sym == 16) { if (idx == 0) { err = "bad repeat"; return false; } val = ll[idx - 1]; rep = 3 + (int)br.bits(2); }
                        else if (sym == 17) rep = 3 + (int)br.bits(3); else rep = 11 + (int)br.bits(7);
                        if (idx + rep > nlen + ndist) { err = "bad repeat length"; return false; }
                        while (rep--) ll[idx++] = (uint8_t)val;
                    }
                    if (br.err) { err = "truncated"; return false; }
                }
                if (ll[256] == 0) { err = "no end-of-block code"; return false; }
                if (!build(lit, ll, nlen) || !build(dist, ll + nlen, ndist)) { err = "bad huffman tables"; return false; }
            }
            for (;;) {
                int sym = decode(br, lit); if (sym < 0 || br.err) { err = "corrupt deflate data"; return false; }
                if (sym < 256) { if (out.size() >= maxOut) { err = "image too large"; return false; } out.push_back((uint8_t)sym); }
                else if (sym == 256) break;
                else {
                    sym -= 257; if (sym >= 29) { err = "bad length symbol"; return false; }
                    int len = lbase[sym] + (int)br.bits(lext[sym]);
                    int ds = decode(br, dist); if (ds < 0 || ds >= 30) { err = "bad distance symbol"; return false; }
                    size_t d = dbase[ds] + br.bits(dext[ds]);
                    if (br.err || d > out.size()) { err = "bad distance"; return false; }
                    if (out.size() + len > maxOut) { err = "image too large"; return false; }
                    size_t from = out.size() - d; for (int i = 0; i < len; ++i) out.push_back(out[from + i]);
                }
            }
        } else { err = "bad block type"; return false; }
    } while (!last);
    return true;
}
inline uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
} // namespace png_detail

// Returns false and sets err on any problem. Never throws, never reads out of bounds.
inline bool decodePng(const uint8_t* d, size_t n, Image& img, std::string& err) {
    using namespace png_detail;
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (n < 33 || std::memcmp(d, sig, 8) != 0) { err = "not a PNG file"; return false; }
    size_t pos = 8; uint32_t w = 0, h = 0; int depth = 0, ctype = 0, interlace = 0;
    std::vector<uint8_t> idat, plte, trns; bool gotIhdr = false, gotIend = false;
    while (pos + 12 <= n && !gotIend) {
        uint32_t len = be32(d + pos); const uint8_t* t = d + pos + 4;
        if (len > n - pos - 12) { err = "truncated chunk"; return false; }
        const uint8_t* body = d + pos + 8;
        if (!std::memcmp(t, "IHDR", 4)) {
            if (len != 13) { err = "bad IHDR"; return false; }
            w = be32(body); h = be32(body + 4); depth = body[8]; ctype = body[9]; interlace = body[12]; gotIhdr = true;
        } else if (!std::memcmp(t, "PLTE", 4)) plte.assign(body, body + len);
        else if (!std::memcmp(t, "tRNS", 4)) trns.assign(body, body + len);
        else if (!std::memcmp(t, "IDAT", 4)) idat.insert(idat.end(), body, body + len);
        else if (!std::memcmp(t, "IEND", 4)) gotIend = true;
        pos += 12 + len;
    }
    if (!gotIhdr || idat.size() < 2) { err = "missing IHDR/IDAT"; return false; }
    if (w == 0 || h == 0 || w > 4096 || h > 4096) { err = "unsupported size (max 4096x4096)"; return false; }
    if (interlace) { err = "interlaced PNG not supported (re-save without interlacing)"; return false; }
    int chans; switch (ctype) { case 0: chans = 1; break; case 2: chans = 3; break; case 3: chans = 1; break; case 4: chans = 2; break; case 6: chans = 4; break; default: err = "bad color type"; return false; }
    if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) { err = "bad bit depth"; return false; }
    if ((ctype == 2 || ctype == 4 || ctype == 6) && depth < 8) { err = "bad depth for color type"; return false; }
    if (ctype == 3 && (plte.empty() || depth == 16)) { err = "palette image without PLTE"; return false; }
    size_t bpp_bits = (size_t)chans * depth, stride = (w * bpp_bits + 7) / 8, bpp = (bpp_bits + 7) / 8;
    size_t expect = (stride + 1) * h;
    std::vector<uint8_t> raw; raw.reserve(expect);
    if ((idat[0] & 0x0F) != 8) { err = "bad zlib header"; return false; }
    if (!inflate(idat.data() + 2, idat.size() - 2, raw, expect, err)) return false;
    if (raw.size() != expect) { err = "pixel data size mismatch"; return false; }
    std::vector<uint8_t> px(stride * h);
    for (uint32_t y = 0; y < h; ++y) {
        uint8_t f = raw[y * (stride + 1)]; const uint8_t* s = &raw[y * (stride + 1) + 1];
        uint8_t* cur = &px[y * stride]; const uint8_t* prev = y ? &px[(y - 1) * stride] : nullptr;
        for (size_t i = 0; i < stride; ++i) {
            int a = i >= bpp ? cur[i - bpp] : 0, b = prev ? prev[i] : 0, c = (prev && i >= bpp) ? prev[i - bpp] : 0, v = s[i];
            switch (f) {
                case 0: break; case 1: v += a; break; case 2: v += b; break; case 3: v += (a + b) / 2; break;
                case 4: { int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                          v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); break; }
                default: err = "bad filter type"; return false;
            }
            cur[i] = (uint8_t)v;
        }
    }
    img.w = (int)w; img.h = (int)h; img.rgba.assign((size_t)w * h * 4, 255);
    auto sample = [&](const uint8_t* row, uint32_t x, int ch) -> int {   // returns raw sample value (up to 16 bit)
        if (depth == 8) return row[x * chans + ch];
        if (depth == 16) return (row[(x * chans + ch) * 2] << 8) | row[(x * chans + ch) * 2 + 1];
        size_t bit = (size_t)x * depth; return (row[bit / 8] >> (8 - depth - (bit % 8))) & ((1 << depth) - 1);
    };
    int maxv = (1 << depth) - 1;
    for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) {
        const uint8_t* row = &px[y * stride]; uint8_t* o = &img.rgba[((size_t)y * w + x) * 4];
        auto to8 = [&](int v) { return (uint8_t)(depth == 16 ? v >> 8 : (depth == 8 ? v : v * 255 / maxv)); };
        if (ctype == 3) {
            size_t idx = (size_t)sample(row, x, 0);
            if (idx * 3 + 2 >= plte.size()) { err = "palette index out of range"; return false; }
            o[0] = plte[idx * 3]; o[1] = plte[idx * 3 + 1]; o[2] = plte[idx * 3 + 2]; o[3] = idx < trns.size() ? trns[idx] : 255;
        } else if (ctype == 0 || ctype == 4) {
            int g = sample(row, x, 0); o[0] = o[1] = o[2] = to8(g);
            if (ctype == 4) o[3] = to8(sample(row, x, 1));
            else if (trns.size() >= 2 && g == ((trns[0] << 8) | trns[1])) o[3] = 0;
        } else {
            int r = sample(row, x, 0), g = sample(row, x, 1), b = sample(row, x, 2);
            o[0] = to8(r); o[1] = to8(g); o[2] = to8(b);
            if (ctype == 6) o[3] = to8(sample(row, x, 3));
            else if (trns.size() >= 6 && r == ((trns[0] << 8) | trns[1]) && g == ((trns[2] << 8) | trns[3]) && b == ((trns[4] << 8) | trns[5])) o[3] = 0;
        }
    }
    return true;
}
} // namespace wl
