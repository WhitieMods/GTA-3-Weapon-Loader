// Header-only probe of a GTA III "ANPK"/"ANLF" IFP (animation package). The engine's loader (CAnimManager::LoadAnimFile)
// writes into FIXED static tables (2 blocks, 250 hierarchies) with no bounds checks, so the loader reads the block
// name and animation count first and refuses files that would not fit. Layout taken from re3's LoadAnimFile and
// the same code path in the supplied exe.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>

namespace wl {
struct IfpInfo { std::string blockName; int numAnims = 0; };
inline bool probeIfp(const uint8_t* d, size_t n, IfpInfo& out, std::string& err) {
    auto rd32 = [&](size_t p) { uint32_t v = 0; if (p + 4 <= n) std::memcpy(&v, d + p, 4); return v; };
    if (n < 16) { err = "file too small"; return false; }
    size_t pos = 0;
    if (!std::memcmp(d, "ANLF", 4)) {
        uint32_t sz = rd32(4); sz = (sz + 3) & ~3u;
        if (sz < 4 || 8 + (size_t)sz > n) { err = "bad ANLF header"; return false; }
        int numANPK = (int)rd32(8);
        if (numANPK != 1) { err = "IFP must contain exactly one animation block"; return false; }
        pos = 8 + sz;
    } else if (std::memcmp(d, "ANPK", 4)) { err = "not an IFP (no ANPK/ANLF)"; return false; }
    if (pos + 16 > n || std::memcmp(d + pos, "ANPK", 4)) { err = "missing ANPK chunk"; return false; }
    size_t info = pos + 8;
    if (std::memcmp(d + info, "INFO", 4)) { err = "missing INFO chunk"; return false; }
    uint32_t isz = rd32(info + 4);
    if (isz < 28 || info + 8 + 28 > n) { err = "bad INFO chunk"; return false; }
    int cnt = (int)rd32(info + 8);
    if (cnt < 1 || cnt > 250) { err = "animation count out of range"; return false; }
    char nm[25] = {0}; std::memcpy(nm, d + info + 12, 24);
    out.blockName = nm; out.numAnims = cnt;
    return !out.blockName.empty() || (err = "empty block name", false);
}
} // namespace wl
