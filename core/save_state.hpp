// Persistence of the active weapon variant per slot, stored in a sidecar file next to the game's own save
// (GTA3sfN.b -> GTA3sfN.b.wl). The vanilla .b file is never modified, so saves stay loadable without the loader
// and original saves stay loadable with it. The sidecar records the size and FNV-1a hash of the .b file it belongs
// to; if the .b file was replaced/restored/overwritten, the stale sidecar is ignored.
#pragma once
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace wl {
inline uint64_t fnv1a64(const uint8_t* d, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= d[i]; h *= 1099511628211ull; }
    return h;
}
struct FileStamp { uint64_t size = 0, hash = 0; bool ok = false; };
inline FileStamp stampFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    return {d.size(), fnv1a64(d.data(), d.size()), true};
}
struct SaveSidecar {
    FileStamp stamp;
    std::map<int, std::string> slotToWeapon;   // engine slot -> custom weapon id (only non-original entries)
};
inline std::string serializeSidecar(const SaveSidecar& s) {
    std::ostringstream o;
    o << "WLSAVE1\nsize=" << s.stamp.size << "\nhash=" << std::hex << s.stamp.hash << std::dec << "\n";
    for (auto& kv : s.slotToWeapon) o << "slot." << kv.first << "=" << kv.second << "\n";
    return o.str();
}
// Strict parser: unknown/malformed input yields nullopt (treated as "no persisted variants").
inline std::optional<SaveSidecar> parseSidecar(const std::string& text) {
    std::istringstream in(text); std::string line;
    if (!std::getline(in, line) || line.rfind("WLSAVE1", 0) != 0) return std::nullopt;
    SaveSidecar s; bool gotSize = false, gotHash = false;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        auto eq = line.find('='); if (eq == std::string::npos) return std::nullopt;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        try {
            if (k == "size") { s.stamp.size = std::stoull(v); gotSize = true; }
            else if (k == "hash") { s.stamp.hash = std::stoull(v, nullptr, 16); gotHash = true; }
            else if (k.rfind("slot.", 0) == 0) {
                int slot = std::stoi(k.substr(5)); if (slot < 0 || slot > 12 || v.empty() || v.size() > 48) return std::nullopt;
                s.slotToWeapon[slot] = v;
            } else return std::nullopt;
        } catch (...) { return std::nullopt; }
    }
    if (!gotSize || !gotHash) return std::nullopt;
    s.stamp.ok = true; return s;
}
inline bool writeSidecar(const std::string& savePath, const SaveSidecar& s) {
    std::ofstream f(savePath + ".wl", std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << serializeSidecar(s);
    return (bool)f;
}
// Returns the persisted slot->weapon map iff a sidecar exists AND belongs to the current save file contents.
inline std::optional<std::map<int, std::string>> readSidecarFor(const std::string& savePath, std::string* whyNot = nullptr) {
    std::ifstream f(savePath + ".wl", std::ios::binary);
    if (!f) { if (whyNot) *whyNot = "no sidecar"; return std::nullopt; }
    std::string text((std::istreambuf_iterator<char>(f)), {});
    auto sc = parseSidecar(text); if (!sc) { if (whyNot) *whyNot = "sidecar unreadable"; return std::nullopt; }
    FileStamp now = stampFile(savePath);
    if (!now.ok || now.size != sc->stamp.size || now.hash != sc->stamp.hash) { if (whyNot) *whyNot = "sidecar is stale (save file changed)"; return std::nullopt; }
    return sc->slotToWeapon;
}
} // namespace wl
