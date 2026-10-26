// Deterministic model-id allocation for custom weapon models.
// IDs are derived from a hash of the weapon id (not from load order), so the same weapon gets the same model id
// every run regardless of what else is installed. This matters because GTA III's own save file stores the model
// index of any pickup that exists in the world.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wl {
constexpr int kModelInfoSize = 5500;      // CModelInfo::ms_modelInfoPtrs entries (verified: cmp ecx,0x157C)
constexpr int kCustomIdFirst = 5100;      // custom range [5100, 5499]
constexpr int kCustomIdLast  = kModelInfoSize - 1;

inline uint32_t fnv1a32(const std::string& s) { uint32_t h = 2166136261u; for (unsigned char c : s) { h ^= c; h *= 16777619u; } return h; }

// 'isFree(id)' reports whether ms_modelInfoPtrs[id] is empty and not already claimed. Returns -1 when the range is full.
inline int allocateModelId(const std::string& key, const std::function<bool(int)>& isFree) {
    const int span = kCustomIdLast - kCustomIdFirst + 1;
    int start = (int)(fnv1a32(key) % (uint32_t)span);
    for (int i = 0; i < span; ++i) { int id = kCustomIdFirst + (start + i) % span; if (isFree(id)) return id; }
    return -1;
}
// Engine object names are char[24]; keep generated names short, unique per key and stable.
inline std::string modelName(const std::string& key, char kind /* 'h' hand model, 'p' pickup */) {
    char b[24]; std::snprintf(b, sizeof b, "wl%c%08x", kind, fnv1a32(key)); return b;
}
inline std::string txdName(const std::string& key) { char b[24]; std::snprintf(b, sizeof b, "wlt%08x", fnv1a32(key)); return b; }
inline std::string ideLine(int id, const std::string& name, const std::string& txd) {
    // "%d %s %s %d %f %d" as parsed by CFileLoader::LoadObject: id, model, txd, one LOD, draw distance, flags
    return std::to_string(id) + " " + name + " " + txd + " 1 100 0";
}
} // namespace wl
