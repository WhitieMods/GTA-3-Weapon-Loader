// Self-contained Ogg Vorbis I decoder (no external library, and no WAV code path anywhere in this project).
// Decodes a whole file to interleaved float PCM. Floor type 1 only (what real encoders emit), residue types 0/1/2,
// mapping type 0, one logical stream, any channel count. Every index read from the file is validated, so corrupt
// or truncated input produces an error string, never an out-of-bounds access.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace wl {
struct Pcm {
    int channels = 0, rate = 0;
    std::vector<float> samples;  // interleaved
    size_t frames() const { return channels ? samples.size() / (size_t)channels : 0; }
};

namespace vorbis_detail {
inline int ilog(uint32_t x) { int n = 0; while (x) { ++n; x >>= 1; } return n; }

struct Bits {
    const uint8_t* p; size_t n; size_t bit = 0; bool eof = false;
    Bits(const uint8_t* d, size_t len) : p(d), n(len) {}
    uint32_t read(int cnt) {  // LSB-first, cnt <= 32
        uint32_t v = 0;
        for (int i = 0; i < cnt; ++i) {
            size_t by = bit >> 3;
            if (by >= n) { eof = true; return v; }
            v |= (uint32_t)((p[by] >> (bit & 7)) & 1) << i;
            ++bit;
        }
        return v;
    }
};

struct Codebook {
    int dim = 0, entries = 0, lookupType = 0;
    std::vector<uint8_t> len;      // 0 = unused entry
    std::vector<int32_t> trie;     // [node*2+bit] -> child node (>0) | -(entry+1) leaf | 0 none
    std::vector<float> vq;         // entries*dim when lookupType != 0

    bool build() {
        trie.assign(2, 0);
        uint32_t avail[33] = {0};
        bool first = true; int used = 0;
        for (int i = 0; i < entries; ++i) {
            int L = len[i];
            if (!L) continue;
            ++used;
            uint32_t code;
            if (first) {
                first = false; code = 0;
                for (int k = 1; k <= L; ++k) avail[k] = 1u << (32 - k);
            } else {
                int z = L;
                while (z > 0 && !avail[z]) --z;
                if (z == 0) return false;  // over-subscribed tree
                code = avail[z]; avail[z] = 0;
                for (int y = L; y > z; --y) avail[y] = code + (1u << (32 - y));
            }
            int node = 0;
            for (int b = 0; b < L; ++b) {
                int bit = (int)((code >> (31 - b)) & 1);
                size_t idx = (size_t)node * 2 + (size_t)bit;
                if (b == L - 1) {
                    if (trie[idx] != 0) return false;
                    trie[idx] = -(i + 1);
                } else {
                    if (trie[idx] < 0) return false;
                    if (trie[idx] == 0) {
                        trie[idx] = (int32_t)(trie.size() / 2);
                        trie.push_back(0); trie.push_back(0);
                    }
                    node = trie[idx];
                }
            }
        }
        return used > 0;
    }
    int decode(Bits& br) const {
        int node = 0;
        for (int guard = 0; guard < 33; ++guard) {
            int bit = (int)br.read(1);
            if (br.eof) return -1;
            int32_t nx = trie[(size_t)node * 2 + (size_t)bit];
            if (nx < 0) return -nx - 1;
            if (nx == 0) return -1;
            node = nx;
        }
        return -1;
    }
};

struct Floor1 {
    int partitions = 0, multiplier = 0, rangebits = 0, nvals = 0;
    std::vector<int> partClass, classDim, classSub, classMaster;
    std::vector<std::vector<int>> subBooks;
    std::vector<int> X, order, lowN, highN;
};
struct Residue {
    int type = 0, begin = 0, end = 0, psize = 0, classes = 0, classbook = 0;
    std::vector<std::array<int, 8>> books;  // [class][pass], -1 = unused
};
struct Mapping {
    int submaps = 1;
    std::vector<int> mux, subFloor, subRes, cMag, cAng;
};
struct Mode { int blockflag = 0, mapping = 0; };

inline float float32Unpack(uint32_t x) {
    double mant = (double)(x & 0x1fffff);
    int exp = (int)((x & 0x7fe00000) >> 21);
    if (x & 0x80000000u) mant = -mant;
    return (float)std::ldexp(mant, exp - 788);
}

struct Decoder {
    int channels = 0, rate = 0, bs[2] = {0, 0};
    std::vector<Codebook> books; std::vector<Floor1> floors; std::vector<Residue> residues;
    std::vector<Mapping> maps; std::vector<Mode> modes;
    std::vector<float> dBtable;
    std::string err;

    bool fail(const char* m) { if (err.empty()) err = m; return false; }

    bool parseId(const std::vector<uint8_t>& pk) {
        if (pk.size() < 30 || pk[0] != 1 || std::memcmp(&pk[1], "vorbis", 6) != 0) return fail("bad identification header");
        uint32_t ver = pk[7] | (pk[8] << 8) | (pk[9] << 16) | ((uint32_t)pk[10] << 24);
        if (ver != 0) return fail("unsupported vorbis version");
        channels = pk[11];
        rate = (int)(pk[12] | (pk[13] << 8) | (pk[14] << 16) | ((uint32_t)pk[15] << 24));
        int b = pk[28];
        bs[0] = 1 << (b & 15); bs[1] = 1 << (b >> 4);
        if (channels < 1 || rate < 1000 || rate > 400000) return fail("bad channel count / sample rate");
        if ((b & 15) < 6 || (b >> 4) > 13 || (b & 15) > (b >> 4)) return fail("bad block sizes");
        if (!(pk[29] & 1)) return fail("missing framing bit");
        return true;
    }

    bool parseSetup(const std::vector<uint8_t>& pk) {
        if (pk.size() < 8 || pk[0] != 5 || std::memcmp(&pk[1], "vorbis", 6) != 0) return fail("bad setup header");
        Bits br(pk.data() + 7, pk.size() - 7);
        int nb = (int)br.read(8) + 1;
        books.resize((size_t)nb);
        for (auto& c : books) {
            if (br.read(24) != 0x564342) return fail("bad codebook sync");
            c.dim = (int)br.read(16); c.entries = (int)br.read(24);
            if (c.dim < 1 || c.entries < 1 || c.entries > (1 << 20)) return fail("bad codebook size");
            if ((size_t)c.entries > pk.size() * 8) return fail("codebook larger than header");
            c.len.assign((size_t)c.entries, 0);
            bool ordered = br.read(1);
            if (ordered) {
                int cur = 0, L = (int)br.read(5) + 1;
                while (cur < c.entries) {
                    int num = (int)br.read(ilog((uint32_t)(c.entries - cur)));
                    if (br.eof || L > 32 || cur + num > c.entries) return fail("bad ordered codebook");
                    for (int k = 0; k < num; ++k) c.len[(size_t)(cur + k)] = (uint8_t)L;
                    cur += num; ++L;
                }
            } else {
                bool sparse = br.read(1);
                for (int i = 0; i < c.entries; ++i) {
                    if (sparse && !br.read(1)) { c.len[(size_t)i] = 0; continue; }
                    c.len[(size_t)i] = (uint8_t)(br.read(5) + 1);
                }
            }
            if (br.eof) return fail("truncated setup");
            if (!c.build()) return fail("bad huffman tree");
            c.lookupType = (int)br.read(4);
            if (c.lookupType > 2) return fail("bad lookup type");
            if (c.lookupType) {
                float minv = float32Unpack(br.read(32)), delta = float32Unpack(br.read(32));
                int vbits = (int)br.read(4) + 1; bool seq = br.read(1);
                size_t nvals;
                if (c.lookupType == 1) {
                    long r = (long)std::floor(std::pow((double)c.entries, 1.0 / c.dim));
                    while (std::pow((double)(r + 1), (double)c.dim) <= (double)c.entries) ++r;
                    while (r > 0 && std::pow((double)r, (double)c.dim) > (double)c.entries) --r;
                    nvals = (size_t)r;
                } else nvals = (size_t)c.entries * (size_t)c.dim;
                if (nvals > pk.size() * 8) return fail("lookup table larger than header");
                std::vector<uint32_t> mult(nvals);
                for (auto& m : mult) m = br.read(vbits);
                if (br.eof) return fail("truncated lookup table");
                c.vq.assign((size_t)c.entries * (size_t)c.dim, 0.f);
                for (int e = 0; e < c.entries; ++e) {
                    float last = 0; size_t idiv = 1;
                    for (int d = 0; d < c.dim; ++d) {
                        size_t moff = c.lookupType == 1 ? (size_t)((e / idiv) % nvals) : (size_t)e * (size_t)c.dim + (size_t)d;
                        float v = (float)mult[moff] * delta + minv + last;
                        c.vq[(size_t)e * (size_t)c.dim + (size_t)d] = v;
                        if (seq) last = v;
                        if (c.lookupType == 1) idiv *= nvals;
                    }
                }
            }
        }
        int nt = (int)br.read(6) + 1;
        for (int i = 0; i < nt; ++i) if (br.read(16) != 0) return fail("bad time-domain transform");
        int nf = (int)br.read(6) + 1; floors.resize((size_t)nf);
        for (auto& f : floors) {
            if (br.read(16) != 1) return fail("unsupported floor type (only floor1)");
            f.partitions = (int)br.read(5);
            f.partClass.resize((size_t)f.partitions); int maxc = -1;
            for (auto& pc : f.partClass) { pc = (int)br.read(4); maxc = std::max(maxc, pc); }
            f.classDim.resize((size_t)maxc + 1); f.classSub.resize((size_t)maxc + 1); f.classMaster.assign((size_t)maxc + 1, 0);
            f.subBooks.resize((size_t)maxc + 1);
            for (int c = 0; c <= maxc; ++c) {
                f.classDim[(size_t)c] = (int)br.read(3) + 1; f.classSub[(size_t)c] = (int)br.read(2);
                if (f.classSub[(size_t)c]) { f.classMaster[(size_t)c] = (int)br.read(8); if (f.classMaster[(size_t)c] >= nb) return fail("bad floor masterbook"); }
                f.subBooks[(size_t)c].resize((size_t)1 << f.classSub[(size_t)c]);
                for (auto& sb : f.subBooks[(size_t)c]) { sb = (int)br.read(8) - 1; if (sb >= nb) return fail("bad floor subbook"); }
            }
            f.multiplier = (int)br.read(2) + 1; f.rangebits = (int)br.read(4);
            f.X = {0, 1 << f.rangebits};
            for (int p = 0; p < f.partitions; ++p)
                for (int d = 0; d < f.classDim[(size_t)f.partClass[(size_t)p]]; ++d) f.X.push_back((int)br.read(f.rangebits));
            f.nvals = (int)f.X.size();
            if (f.nvals > 65 + 2 || br.eof) return fail("bad floor");
            f.order.resize((size_t)f.nvals);
            for (int i = 0; i < f.nvals; ++i) f.order[(size_t)i] = i;
            std::sort(f.order.begin(), f.order.end(), [&](int a, int b) { return f.X[(size_t)a] < f.X[(size_t)b]; });
            for (int i = 1; i < f.nvals; ++i) if (f.X[(size_t)f.order[(size_t)i]] == f.X[(size_t)f.order[(size_t)i - 1]]) return fail("duplicate floor X");
            f.lowN.assign((size_t)f.nvals, 0); f.highN.assign((size_t)f.nvals, 1);
            for (int i = 2; i < f.nvals; ++i) {
                int lo = -1, hi = -1;
                for (int j = 0; j < i; ++j) {
                    if (f.X[(size_t)j] < f.X[(size_t)i] && (lo < 0 || f.X[(size_t)j] > f.X[(size_t)lo])) lo = j;
                    if (f.X[(size_t)j] > f.X[(size_t)i] && (hi < 0 || f.X[(size_t)j] < f.X[(size_t)hi])) hi = j;
                }
                if (lo < 0 || hi < 0) return fail("bad floor neighbours");
                f.lowN[(size_t)i] = lo; f.highN[(size_t)i] = hi;
            }
        }
        int nr = (int)br.read(6) + 1; residues.resize((size_t)nr);
        for (auto& r : residues) {
            r.type = (int)br.read(16);
            if (r.type > 2) return fail("bad residue type");
            r.begin = (int)br.read(24); r.end = (int)br.read(24); r.psize = (int)br.read(24) + 1;
            r.classes = (int)br.read(6) + 1; r.classbook = (int)br.read(8);
            if (r.classbook >= nb || r.end < r.begin) return fail("bad residue");
            std::vector<int> cascade((size_t)r.classes);
            for (auto& c : cascade) { int lo = (int)br.read(3); int hi = br.read(1) ? (int)br.read(5) : 0; c = hi * 8 + lo; }
            r.books.assign((size_t)r.classes, std::array<int, 8>{-1, -1, -1, -1, -1, -1, -1, -1});
            for (int c = 0; c < r.classes; ++c) for (int j = 0; j < 8; ++j)
                if (cascade[(size_t)c] & (1 << j)) {
                    int b = (int)br.read(8); if (b >= nb || books[(size_t)b].lookupType == 0) return fail("bad residue book");
                    r.books[(size_t)c][(size_t)j] = b;
                }
            if (books[(size_t)r.classbook].entries < 1) return fail("bad classbook");
        }
        int nm = (int)br.read(6) + 1; maps.resize((size_t)nm);
        for (auto& m : maps) {
            if (br.read(16) != 0) return fail("bad mapping type");
            m.submaps = br.read(1) ? (int)br.read(4) + 1 : 1;
            if (br.read(1)) {
                int steps = (int)br.read(8) + 1; m.cMag.resize((size_t)steps); m.cAng.resize((size_t)steps);
                for (int s = 0; s < steps; ++s) {
                    m.cMag[(size_t)s] = (int)br.read(ilog((uint32_t)channels - 1)); m.cAng[(size_t)s] = (int)br.read(ilog((uint32_t)channels - 1));
                    if (m.cMag[(size_t)s] >= channels || m.cAng[(size_t)s] >= channels || m.cMag[(size_t)s] == m.cAng[(size_t)s]) return fail("bad coupling");
                }
            }
            if (br.read(2) != 0) return fail("bad mapping reserved bits");
            m.mux.assign((size_t)channels, 0);
            if (m.submaps > 1) for (auto& x : m.mux) { x = (int)br.read(4); if (x >= m.submaps) return fail("bad mux"); }
            m.subFloor.resize((size_t)m.submaps); m.subRes.resize((size_t)m.submaps);
            for (int s = 0; s < m.submaps; ++s) {
                br.read(8);
                m.subFloor[(size_t)s] = (int)br.read(8); m.subRes[(size_t)s] = (int)br.read(8);
                if (m.subFloor[(size_t)s] >= nf || m.subRes[(size_t)s] >= nr) return fail("bad submap");
            }
        }
        int nmo = (int)br.read(6) + 1; modes.resize((size_t)nmo);
        for (auto& mo : modes) {
            mo.blockflag = (int)br.read(1);
            if (br.read(16) != 0 || br.read(16) != 0) return fail("bad mode");
            mo.mapping = (int)br.read(8); if (mo.mapping >= nm) return fail("bad mode mapping");
        }
        if (!br.read(1) || br.eof) return fail("bad setup framing");
        // inverse dB table: geometric ramp from 1.0649863e-07 (index 0) to 1.0 (index 255)
        dBtable.resize(256);
        double step = std::log(1.0 / 1.0649863e-07) / 255.0;
        for (int i = 0; i < 256; ++i) dBtable[(size_t)i] = (float)std::exp(-(255 - i) * step);
        return true;
    }

    // ---- per-stream decode state ----
    std::vector<float> prev; int prevN = 0; bool havePrev = false;
    std::vector<std::vector<float>> cosTab;  // per blocksize index: table of 4n entries

    const std::vector<float>& cosTable(int n) {
        std::vector<float>& t = cosTab[n == bs[0] ? 0 : 1];
        if (t.empty()) { t.resize((size_t)n * 4); for (size_t m = 0; m < t.size(); ++m) t[m] = (float)std::cos(2.0 * M_PI * (double)m / (double)(n * 4)); }
        return t;
    }

    static int renderPoint(int x0, int y0, int x1, int y1, int X) {
        int dy = y1 - y0, adx = x1 - x0, ady = std::abs(dy), err = ady * (X - x0), off = err / adx;
        return dy < 0 ? y0 - off : y0 + off;
    }
    static void renderLine(int x0, int y0, int x1, int y1, std::vector<int>& v, int limit) {
        int dy = y1 - y0, adx = x1 - x0; if (adx <= 0) return;
        int ady = std::abs(dy), base = dy / adx, x = x0, y = y0, err = 0;
        int sy = dy < 0 ? base - 1 : base + 1; ady -= std::abs(base) * adx;
        if (x < limit) v[(size_t)x] = y;
        for (++x; x < x1 && x < limit; ++x) {
            err += ady;
            if (err >= adx) { err -= adx; y += sy; } else y += base;
            v[(size_t)x] = y;
        }
    }

    bool decodeFloor(const Floor1& f, Bits& br, int half, std::vector<float>& out, bool& nonzero) {
        nonzero = br.read(1) != 0;
        if (br.eof) return fail("truncated packet");
        if (!nonzero) return true;
        static const int ranges[4] = {256, 128, 86, 64};
        int range = ranges[f.multiplier - 1];
        std::vector<int> Y((size_t)f.nvals, 0);
        Y[0] = (int)br.read(ilog((uint32_t)range - 1)); Y[1] = (int)br.read(ilog((uint32_t)range - 1));
        int off = 2;
        for (int p = 0; p < f.partitions; ++p) {
            int cls = f.partClass[(size_t)p], cdim = f.classDim[(size_t)cls], cbits = f.classSub[(size_t)cls];
            int csub = (1 << cbits) - 1, cval = 0;
            if (cbits) { cval = books[(size_t)f.classMaster[(size_t)cls]].decode(br); if (cval < 0) return fail("bad floor class"); }
            for (int j = 0; j < cdim; ++j) {
                int bk = f.subBooks[(size_t)cls][(size_t)(cval & csub)]; cval >>= cbits;
                if (bk >= 0) { int v = books[(size_t)bk].decode(br); if (v < 0) return fail("bad floor value"); Y[(size_t)(off + j)] = v; }
                else Y[(size_t)(off + j)] = 0;
            }
            off += cdim;
        }
        std::vector<int> fy((size_t)f.nvals, 0); std::vector<char> st((size_t)f.nvals, 0);
        fy[0] = Y[0]; fy[1] = Y[1]; st[0] = st[1] = 1;
        for (int i = 2; i < f.nvals; ++i) {
            int lo = f.lowN[(size_t)i], hi = f.highN[(size_t)i];
            int pred = renderPoint(f.X[(size_t)lo], fy[(size_t)lo], f.X[(size_t)hi], fy[(size_t)hi], f.X[(size_t)i]);
            int val = Y[(size_t)i], highroom = range - pred, lowroom = pred;
            int room = (highroom < lowroom ? highroom : lowroom) * 2;
            if (val) {
                st[(size_t)lo] = st[(size_t)hi] = st[(size_t)i] = 1;
                if (val >= room) fy[(size_t)i] = highroom > lowroom ? val - lowroom + pred : pred - val + highroom - 1;
                else fy[(size_t)i] = (val & 1) ? pred - ((val + 1) >> 1) : pred + (val >> 1);
            } else { st[(size_t)i] = 0; fy[(size_t)i] = pred; }
        }
        std::vector<int> line((size_t)half, 0);
        int lx = 0, ly = std::clamp(fy[(size_t)f.order[0]], 0, range - 1) * f.multiplier, hx = 0, hy = ly;
        for (int i = 1; i < f.nvals; ++i) {
            int idx = f.order[(size_t)i];
            if (st[(size_t)idx]) {
                hy = std::clamp(fy[(size_t)idx], 0, range - 1) * f.multiplier; hx = f.X[(size_t)idx];
                renderLine(lx, ly, hx, hy, line, half); lx = hx; ly = hy;
            }
        }
        for (int x = hx; x < half; ++x) line[(size_t)x] = hy;
        out.resize((size_t)half);
        for (int i = 0; i < half; ++i) out[(size_t)i] = dBtable[(size_t)std::clamp(line[(size_t)i], 0, 255)];
        return true;
    }

    bool decodeResidue(const Residue& r, Bits& br, int half, std::vector<std::vector<float>*>& vecs, const std::vector<char>& dnd) {
        int ch = (int)vecs.size();
        int actual = r.type == 2 ? half * ch : half;
        int begin = std::min(r.begin, actual), end = std::min(r.end, actual);
        const Codebook& cb = books[(size_t)r.classbook];
        int cwords = cb.dim, nread = end - begin, nparts = nread > 0 ? nread / r.psize : 0;
        if (nparts <= 0) return true;
        int nch = r.type == 2 ? 1 : ch;
        std::vector<char> skip((size_t)nch, 0);
        if (r.type == 2) { bool all = true; for (char d : dnd) if (!d) all = false; skip[0] = all; }
        else for (int j = 0; j < ch; ++j) skip[(size_t)j] = dnd[(size_t)j];
        std::vector<std::vector<float>> work;
        std::vector<float*> tgt((size_t)nch);
        if (r.type == 2) { work.assign(1, std::vector<float>((size_t)actual, 0.f)); tgt[0] = work[0].data(); }
        else for (int j = 0; j < ch; ++j) tgt[(size_t)j] = vecs[(size_t)j]->data();
        std::vector<std::vector<int>> cls((size_t)nch, std::vector<int>((size_t)nparts + (size_t)cwords, 0));
        for (int pass = 0; pass < 8; ++pass) {
            int pc = 0;
            while (pc < nparts) {
                if (pass == 0) for (int j = 0; j < nch; ++j) if (!skip[(size_t)j]) {
                    int temp = cb.decode(br); if (temp < 0) return fail("bad residue class codeword");
                    for (int i = cwords - 1; i >= 0; --i) { cls[(size_t)j][(size_t)(pc + i)] = temp % r.classes; temp /= r.classes; }
                }
                for (int i = 0; i < cwords && pc < nparts; ++i, ++pc) for (int j = 0; j < nch; ++j) if (!skip[(size_t)j]) {
                    int vc = cls[(size_t)j][(size_t)pc], bk = r.books[(size_t)vc][(size_t)pass];
                    if (bk < 0) continue;
                    const Codebook& vb = books[(size_t)bk]; int offset = begin + pc * r.psize;
                    if (r.type == 0) {
                        int step = r.psize / vb.dim;
                        for (int i2 = 0; i2 < step; ++i2) {
                            int e = vb.decode(br); if (e < 0) return fail("bad residue value");
                            for (int k = 0; k < vb.dim; ++k) { size_t pos = (size_t)(offset + i2 + k * step); if (pos < (size_t)actual) tgt[(size_t)j][pos] += vb.vq[(size_t)e * (size_t)vb.dim + (size_t)k]; }
                        }
                    } else {
                        for (int i2 = 0; i2 < r.psize; i2 += vb.dim) {
                            int e = vb.decode(br); if (e < 0) return fail("bad residue value");
                            for (int k = 0; k < vb.dim; ++k) { size_t pos = (size_t)(offset + i2 + k); if (pos < (size_t)actual) tgt[(size_t)j][pos] += vb.vq[(size_t)e * (size_t)vb.dim + (size_t)k]; }
                        }
                    }
                }
            }
        }
        if (r.type == 2) for (int j = 0; j < ch; ++j) for (int i = 0; i < half; ++i) (*vecs[(size_t)j])[(size_t)i] = work[0][(size_t)i * (size_t)ch + (size_t)j];
        return true;
    }

    // returns false only on a hard error; 'out' receives finished PCM frames (may be empty)
    bool decodePacket(const std::vector<uint8_t>& pk, std::vector<float>& out) {
        Bits br(pk.data(), pk.size());
        if (br.read(1) != 0) return true;  // not an audio packet: ignore
        int mnum = (int)br.read(ilog((uint32_t)modes.size() - 1));
        if (br.eof || mnum >= (int)modes.size()) return fail("bad mode number");
        const Mode& mode = modes[(size_t)mnum]; const Mapping& mp = maps[(size_t)mode.mapping];
        int n = bs[mode.blockflag], half = n / 2, prevFlag = 1, nextFlag = 1;
        if (mode.blockflag) { prevFlag = (int)br.read(1); nextFlag = (int)br.read(1); }
        std::vector<std::vector<float>> spec((size_t)channels), flo((size_t)channels);
        std::vector<char> nz((size_t)channels, 0), floorUsed((size_t)channels, 0);
        for (int c = 0; c < channels; ++c) {
            bool nonzero = false;
            if (!decodeFloor(floors[(size_t)mp.subFloor[(size_t)mp.mux[(size_t)c]]], br, half, flo[(size_t)c], nonzero)) return false;
            nz[(size_t)c] = nonzero; floorUsed[(size_t)c] = nonzero; spec[(size_t)c].assign((size_t)half, 0.f);
        }
        for (size_t s = 0; s < mp.cMag.size(); ++s) if (nz[(size_t)mp.cMag[s]] || nz[(size_t)mp.cAng[s]]) nz[(size_t)mp.cMag[s]] = nz[(size_t)mp.cAng[s]] = 1;
        for (int sm = 0; sm < mp.submaps; ++sm) {
            std::vector<std::vector<float>*> vecs; std::vector<char> dnd;
            for (int c = 0; c < channels; ++c) if (mp.mux[(size_t)c] == sm) { vecs.push_back(&spec[(size_t)c]); dnd.push_back(!nz[(size_t)c]); }
            if (vecs.empty()) continue;
            if (!decodeResidue(residues[(size_t)mp.subRes[(size_t)sm]], br, half, vecs, dnd)) return false;
        }
        for (int s = (int)mp.cMag.size() - 1; s >= 0; --s) {
            auto& M = spec[(size_t)mp.cMag[(size_t)s]]; auto& A = spec[(size_t)mp.cAng[(size_t)s]];
            for (int i = 0; i < half; ++i) {
                float m = M[(size_t)i], a = A[(size_t)i], nm, na;
                if (m > 0) { if (a > 0) { nm = m; na = m - a; } else { na = m; nm = m + a; } }
                else { if (a > 0) { nm = m; na = m + a; } else { na = m; nm = m - a; } }
                M[(size_t)i] = nm; A[(size_t)i] = na;
            }
        }
        // window (spec 1.3.2 power-sine slopes)
        int ls = 0, le = half, lN = half, rs = half, re = n, rN = half;
        if (mode.blockflag && !prevFlag) { ls = n / 4 - bs[0] / 4; le = n / 4 + bs[0] / 4; lN = bs[0] / 2; }
        if (mode.blockflag && !nextFlag) { rs = n * 3 / 4 - bs[0] / 4; re = n * 3 / 4 + bs[0] / 4; rN = bs[0] / 2; }
        std::vector<std::vector<float>> td((size_t)channels, std::vector<float>((size_t)n, 0.f));
        const std::vector<float>& ct = cosTable(n);
        for (int c = 0; c < channels; ++c) {
            if (!nz[(size_t)c] || !floorUsed[(size_t)c]) continue;  // spec: a channel without a floor outputs silence even if coupling made its residue non-zero
            std::vector<float> x((size_t)half);
            for (int i = 0; i < half; ++i) x[(size_t)i] = spec[(size_t)c][(size_t)i] * flo[(size_t)c][(size_t)i];
            // direct IMDCT: y[i] = sum_k x[k] cos(pi/(2n) * (2i+1+n/2)(2k+1)); product index modulo 4n via lookup
            for (int i = 0; i < n; ++i) {
                uint64_t a = (uint64_t)(2 * i + 1 + n / 2); float acc = 0.f;
                for (int k = 0; k < half; ++k) {
                    if (x[(size_t)k] == 0.f) continue;
                    acc += x[(size_t)k] * ct[(size_t)((a * (uint64_t)(2 * k + 1)) % (uint64_t)(4 * n))];
                }
                float w;
                if (i < ls || i >= re) w = 0.f;
                else if (i < le) { double s = std::sin(M_PI / 2.0 * (i - ls + 0.5) / lN); w = (float)std::sin(M_PI / 2.0 * s * s); }
                else if (i < rs) w = 1.f;
                else { double s = std::sin(M_PI / 2.0 + M_PI / 2.0 * (i - rs + 0.5) / rN); w = (float)std::sin(M_PI / 2.0 * s * s); }
                td[(size_t)c][(size_t)i] = acc * w;
            }
        }
        if (havePrev) {
            int outLen = prevN / 4 + n / 4;
            size_t base = out.size(); out.resize(base + (size_t)outLen * (size_t)channels, 0.f);
            for (int c = 0; c < channels; ++c) for (int t = 0; t < outLen; ++t) {
                float s = 0.f; int pi = prevN / 2 + t, ci = t + n / 4 - prevN / 4;
                if (pi < prevN) s += prev[(size_t)c * (size_t)prevN + (size_t)pi];
                if (ci >= 0 && ci < n) s += td[(size_t)c][(size_t)ci];
                out[base + (size_t)t * (size_t)channels + (size_t)c] = s;
            }
        }
        prev.assign((size_t)channels * (size_t)n, 0.f);
        for (int c = 0; c < channels; ++c) std::copy(td[(size_t)c].begin(), td[(size_t)c].end(), prev.begin() + (ptrdiff_t)((size_t)c * (size_t)n));
        prevN = n; havePrev = true;
        return true;
    }
};
}  // namespace vorbis_detail

// Decode a complete .ogg (Vorbis) file in memory. maxSeconds bounds memory use.
inline bool decodeOgg(const uint8_t* d, size_t n, Pcm& pcm, std::string& err, double maxSeconds = 30.0) {
    using namespace vorbis_detail;
    std::vector<std::vector<uint8_t>> packets; std::vector<uint8_t> cur; bool haveCur = false;
    uint32_t serial = 0; bool haveSerial = false; int64_t lastGranule = -1; size_t pos = 0;
    while (pos + 27 <= n) {
        if (std::memcmp(d + pos, "OggS", 4) != 0) { err = "lost Ogg sync"; return false; }
        uint8_t htype = d[pos + 5]; int64_t gran = 0; std::memcpy(&gran, d + pos + 6, 8);
        uint32_t ser = d[pos + 14] | (d[pos + 15] << 8) | (d[pos + 16] << 16) | ((uint32_t)d[pos + 17] << 24);
        int nseg = d[pos + 26];
        if (pos + 27 + (size_t)nseg > n) { err = "truncated Ogg page"; return false; }
        size_t body = pos + 27 + (size_t)nseg, total = 0;
        for (int i = 0; i < nseg; ++i) total += d[pos + 27 + (size_t)i];
        if (body + total > n) { err = "truncated Ogg page"; return false; }
        if (!haveSerial) { serial = ser; haveSerial = true; }
        if (ser == serial) {
            if (!(htype & 1) && haveCur) { packets.push_back(cur); cur.clear(); haveCur = false; }
            size_t off = body;
            for (int i = 0; i < nseg; ++i) {
                uint8_t lace = d[pos + 27 + (size_t)i];
                cur.insert(cur.end(), d + off, d + off + lace); off += lace; haveCur = true;
                if (lace < 255) { packets.push_back(cur); cur.clear(); haveCur = false; }
            }
            if (gran >= 0) lastGranule = gran;
        }
        pos = body + total;
        if (packets.size() > 2000000) { err = "too many packets"; return false; }
    }
    if (packets.size() < 4) { err = "not a Vorbis stream (too few packets)"; return false; }
    Decoder dec;
    if (!dec.parseId(packets[0]) || !dec.parseSetup(packets[2])) { err = dec.err; return false; }
    dec.cosTab.assign(2, {});
    pcm.channels = dec.channels; pcm.rate = dec.rate; pcm.samples.clear();
    size_t maxFrames = (size_t)(maxSeconds * dec.rate);
    for (size_t i = 3; i < packets.size(); ++i) {
        if (!dec.decodePacket(packets[i], pcm.samples)) { err = dec.err; return false; }
        if (pcm.frames() > maxFrames + 8192) { err = "audio longer than limit"; return false; }
    }
    if (lastGranule >= 0 && (size_t)lastGranule < pcm.frames()) pcm.samples.resize((size_t)lastGranule * (size_t)pcm.channels);
    return true;
}
}  // namespace wl
