// Portable (no Windows / no game memory) core of the GTA III weapon loader:
//   * slot table (verified against gta3.exe, see docs/REVERSE_ENGINEERING_REPORT.md)
//   * weapon.xml parsing + validation
//   * registry with same-slot replacement semantics
//
// KEY ENGINE FACT (verified by disassembly): CPed::m_aWeapons is indexed directly by eWeaponType and every
// consumer tests  m_aWeapons[i].m_eType == i.  There is no independent "slot" table. A weapon "slot" is therefore
// one of the 13 original eWeaponType values, and a custom weapon is a *variant* that lives in one of them.
#pragma once
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include "xml_mini.hpp"

namespace wl {

// ---- Slot table: values from CWeaponInfo::ms_aWeaponNames @0x603334 (decoded from gta3.exe) -------------------
enum Slot : int { S_FIST = 0, S_BAT, S_PISTOL, S_UZI, S_SHOTGUN, S_AK47, S_M16, S_SNIPER, S_ROCKET,
                  S_FLAME, S_MOLOTOV, S_GRENADE, S_DETONATOR, SLOT_COUNT };

struct SlotInfo { const char* key; const char* category; bool allowVariants; };
inline const SlotInfo& slotInfo(int s) {
    static const SlotInfo t[SLOT_COUNT] = {
        {"fist", "melee", false},           {"bat", "melee", true},
        {"pistol", "pistol", true},         {"uzi", "smg", true},
        {"shotgun", "shotgun", true},       {"ak47", "assault_rifle", true},
        {"m16", "assault_rifle", true},     {"sniper", "sniper_rifle", true},
        {"rocket", "launcher", true},       {"flamethrower", "flamethrower", true},
        {"molotov", "thrown", true},        {"grenade", "thrown", true},
        {"detonator", "detonator", false}};
    return t[s];
}
inline int slotFromKey(const std::string& k) {
    for (int i = 0; i < SLOT_COUNT; ++i) if (k == slotInfo(i).key) return i;
    return -1;
}

// ---- Diagnostics ------------------------------------------------------------------------------------------------
struct Diag { enum Level { Info, Warning, Error } level; std::string where, msg; };
using Diags = std::vector<Diag>;
inline bool hasError(const Diags& d) { for (auto& x : d) if (x.level == Diag::Error) return true; return false; }

// ---- Definition -------------------------------------------------------------------------------------------------
// Only fields that map to verified CWeaponInfo offsets are overridable (see report):
//   +0x04 range(float) +0x08 firingRate +0x0C reload +0x10 ammo-in-clip +0x14 damage
struct StatOverrides {
    std::optional<float> range;
    std::optional<int> fireRate, reloadTime, clipSize, damage;
};
struct AudioSet { std::string fire, reload, empty; };     // OGG only; paths relative to weapon dir
// Animation configuration. anim1/anim2 are names looked up in the engine's standard ped animation group
// (CAnimManager::GetAnimAssociation(ASSOCGRP_STD, name), exactly what weapon.dat uses). If 'ifp'+'block' are given,
// names not found there are looked up in that custom IFP block, which the loader appends to the std group.
struct AnimMap {
    std::string anim1, anim2, ifp, block;
    std::optional<float> loopStart, loopEnd, frameFire, frame2Fire;
    bool any() const { return !anim1.empty() || !anim2.empty() || loopStart || loopEnd || frameFire || frame2Fire; }
};
struct Spawn { float x = 0, y = 0, z = 0; int type = 2; int ammo = -1; };  // type: 2 ON_STREET (respawns), 3 ONCE, 14 ON_STREET_SLOW

struct WeaponDef {
    std::string id, name, category, dir;
    int slot = -1;
    bool isOriginal = false;
    StatOverrides stats;
    int pickupAmmo = -1;   // -1 => engine default for the slot
    int maxAmmo = 99999;   // engine hard cap is 99999 (0x1869F in CPed::GiveWeapon)
    std::string model, txd, pickupModel, icon;
    AudioSet audio;
    float volume = 1.0f;          // multiplies the game's SFX volume for this weapon's OGG sounds
    AnimMap anim;
    std::vector<Spawn> spawns;    // world pickups the loader creates for new games
};

// Snapshot of the live CWeaponInfo fields we are allowed to change.
struct InfoStats { float range = 0; int fireRate = 0, reloadTime = 0, clipSize = 0, damage = 0; };
inline InfoStats resolveStats(const InfoStats& base, const StatOverrides& o) {
    InfoStats r = base;
    if (o.range) r.range = *o.range;
    if (o.fireRate) r.fireRate = *o.fireRate;
    if (o.reloadTime) r.reloadTime = *o.reloadTime;
    if (o.clipSize) r.clipSize = *o.clipSize;
    if (o.damage) r.damage = *o.damage;
    return r;
}

// ---- Parsing / validation ---------------------------------------------------------------------------------------
using FileExists = std::function<bool(const std::string&)>;

namespace detail {
inline std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
inline bool endsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && lower(s.substr(s.size() - suf.size())) == suf;
}
inline bool validId(const std::string& id) {
    if (id.empty() || id.size() > 48) return false;
    for (char c : id) if (!(std::islower((unsigned char)c) || std::isdigit((unsigned char)c) || c == '_')) return false;
    return true;
}
inline bool parseInt(const std::string& s, int& v) {
    try { size_t p; long l = std::stol(s, &p); if (p != s.size()) return false; v = (int)l; return true; } catch (...) { return false; }
}
inline bool parseFloat(const std::string& s, float& v) {
    try { size_t p; float f = std::stof(s, &p); if (p != s.size()) return false; v = f; return true; } catch (...) { return false; }
}
inline bool safeRelPath(const std::string& p) {
    if (p.empty() || p[0] == '/' || p[0] == '\\' || (p.size() > 1 && p[1] == ':')) return false;
    return p.find("..") == std::string::npos;
}
} // namespace detail

// Parses one weapon.xml. On any Error the returned optional is empty (weapon rejected, game unaffected).
inline std::optional<WeaponDef> parseWeaponXml(const std::string& xml, const std::string& weaponDir,
                                               const FileExists& exists, Diags& diags) {
    using namespace detail;
    const std::string where = weaponDir;
    auto err = [&](const std::string& m) { diags.push_back({Diag::Error, where, m}); };
    auto warn = [&](const std::string& m) { diags.push_back({Diag::Warning, where, m}); };

    std::string perr;
    auto root = XmlParser::parse(xml, perr);
    if (!root) { err("XML parse error: " + perr); return std::nullopt; }
    if (root->name != "weapon") { err("root element must be <weapon>"); return std::nullopt; }

    WeaponDef d; d.dir = weaponDir;
    bool bad = false;
    auto fail = [&](const std::string& m) { err(m); bad = true; };

    if (auto* a = root->attr("id")) d.id = *a; else fail("missing id attribute on <weapon>");
    if (!d.id.empty() && !validId(d.id)) fail("id '" + d.id + "' invalid (use a-z, 0-9, _ ; max 48)");
    if (d.id.rfind("original_", 0) == 0) fail("id prefix 'original_' is reserved for built-in weapons");

    if (auto* n = root->child("name")) d.name = n->text;
    if (d.name.empty()) fail("missing <name>");
    else if (d.name.size() > 24) fail("<name> longer than 24 characters");

    int slot = -1;
    if (auto* s = root->child("slot")) {
        slot = slotFromKey(lower(s->text));
        if (slot < 0) fail("unknown <slot> '" + s->text + "'");
        else if (!slotInfo(slot).allowVariants) fail("slot '" + s->text + "' does not accept custom weapons");
    } else fail("missing <slot>");
    d.slot = slot;

    if (slot >= 0) {
        std::string canon = slotInfo(slot).category;
        if (auto* c = root->child("category")) {
            d.category = lower(c->text);
            if (d.category != canon)
                fail("category '" + c->text + "' conflicts with slot '" + slotInfo(slot).key + "' (category '" + canon + "')");
        } else d.category = canon;
    }

    if (auto* st = root->child("stats")) {
        auto num = [&](const char* k, auto& out, auto lo, auto hi, auto parse) {
            if (auto* a = st->attr(k)) {
                typename std::decay<decltype(*out)>::type v{};
                if (!parse(*a, v)) fail(std::string("stats@") + k + " is not a number");
                else if (v < lo || v > hi) fail(std::string("stats@") + k + " out of range");
                else out = v;
            }
        };
        num("damage", d.stats.damage, 0, 1000, parseInt);
        num("fireRate", d.stats.fireRate, 0, 60000, parseInt);
        num("reload", d.stats.reloadTime, 0, 60000, parseInt);
        num("clipSize", d.stats.clipSize, 1, 999, parseInt);   // HUD only shows clip format for 2..999
        num("range", d.stats.range, 0.1f, 500.0f, parseFloat);
    }
    if (auto* am = root->child("ammo")) {
        if (auto* a = am->attr("pickupAmmo")) { int v; if (!parseInt(*a, v) || v < 0 || v > 99999) fail("ammo@pickupAmmo invalid"); else d.pickupAmmo = v; }
        if (auto* a = am->attr("maxAmmo")) { int v; if (!parseInt(*a, v) || v < 1 || v > 99999) fail("ammo@maxAmmo invalid"); else d.maxAmmo = v; }
    }

    auto asset = [&](const XmlNode* parent, const char* tag, const char* ext, std::string& out) {
        if (!parent) return;
        auto* n = parent->child(tag); if (!n || n->text.empty()) return;
        if (!safeRelPath(n->text)) { fail(std::string(tag) + ": path must be relative with no '..'"); return; }
        if (ext && !endsWith(n->text, ext)) { fail(std::string(tag) + ": must be a " + ext + " file"); return; }
        if (!exists(weaponDir + "/" + n->text)) { warn(std::string(tag) + ": file not found '" + n->text + "' (feature disabled)"); return; }
        out = n->text;
    };
    const XmlNode* as = root->child("assets");
    asset(as, "model", ".dff", d.model);
    asset(as, "txd", ".txd", d.txd);
    asset(as, "pickupModel", ".dff", d.pickupModel);
    asset(as, "icon", ".png", d.icon);

    const XmlNode* au = root->child("audio");
    // OGG only. Anything else is an error: there is intentionally no other-format path in this code base.
    asset(au, "fire", ".ogg", d.audio.fire);
    asset(au, "reload", ".ogg", d.audio.reload);
    asset(au, "empty", ".ogg", d.audio.empty);

    if (au) if (auto* v = au->attr("volume")) { float f; if (!parseFloat(*v, f) || f < 0.f || f > 4.f) fail("audio@volume must be 0..4"); else d.volume = f; }

    if (auto* an = root->child("animations")) {
        auto name = [&](const char* k, std::string& o) {
            if (auto* a = an->attr(k)) { if (a->size() > 23) fail(std::string("animations@") + k + " longer than 23 chars"); else o = *a; }
        };
        name("anim1", d.anim.anim1); name("anim2", d.anim.anim2); name("block", d.anim.block);
        auto flt = [&](const char* k, std::optional<float>& o) {
            if (auto* a = an->attr(k)) { float f; if (!parseFloat(*a, f) || f < 0.f || f > 100000.f) fail(std::string("animations@") + k + " invalid"); else o = f; }
        };
        flt("loopStart", d.anim.loopStart); flt("loopEnd", d.anim.loopEnd); flt("frameFire", d.anim.frameFire); flt("frame2Fire", d.anim.frame2Fire);
        if (auto* f = an->attr("ifp")) {
            if (!safeRelPath(*f)) fail("animations@ifp: path must be relative with no '..'");
            else if (!endsWith(*f, ".ifp")) fail("animations@ifp must be a .ifp file");
            else if (d.anim.block.empty()) fail("animations@ifp requires animations@block (the block name stored inside the IFP)");
            else if (!exists(weaponDir + "/" + *f)) warn("animations: ifp not found '" + *f + "' (custom animations disabled)");
            else d.anim.ifp = *f;
        }
    }
    if (auto* sp = root->child("spawns")) {
        for (auto& c : sp->children) {
            if (c->name != "spawn") continue;
            if (d.spawns.size() >= 64) { fail("more than 64 <spawn> entries"); break; }
            Spawn s; bool okc = true;
            for (auto kv : {std::make_pair("x", &s.x), std::make_pair("y", &s.y), std::make_pair("z", &s.z)}) {
                auto* a = c->attr(kv.first); float f;
                if (!a || !parseFloat(*a, f) || std::fabs(f) > 10000.f) { fail(std::string("spawn@") + kv.first + " missing or out of range"); okc = false; break; }
                *kv.second = f;
            }
            if (!okc) break;
            if (auto* t = c->attr("type")) { int v; if (!parseInt(*t, v) || (v != 2 && v != 3 && v != 14)) { fail("spawn@type must be 2 (respawning), 3 (once) or 14 (slow respawn)"); break; } s.type = v; }
            if (auto* t = c->attr("ammo")) { int v; if (!parseInt(*t, v) || v < 0 || v > 99999) { fail("spawn@ammo invalid"); break; } s.ammo = v; }
            d.spawns.push_back(s);
        }
    }
    if (bad) return std::nullopt;
    return d;
}

// ---- Registry ---------------------------------------------------------------------------------------------------
class Registry {
public:
    Registry() { addOriginals(); }

    // Duplicate ids and conflicting definitions are rejected; the first definition wins.
    bool add(const WeaponDef& d, Diags& diags) {
        if (byId_.count(d.id)) {
            diags.push_back({Diag::Error, d.dir, "duplicate weapon id '" + d.id + "' (first definition kept)"});
            return false;
        }
        byId_[d.id] = defs_.size();
        defs_.push_back(d);
        return true;
    }
    const WeaponDef* find(const std::string& id) const {
        auto it = byId_.find(id); return it == byId_.end() ? nullptr : &defs_[it->second];
    }
    const WeaponDef& original(int slot) const { return *find(std::string("original_") + slotInfo(slot).key); }
    std::vector<const WeaponDef*> forSlot(int slot) const {
        std::vector<const WeaponDef*> v;
        for (auto& d : defs_) if (d.slot == slot) v.push_back(&d);
        return v;
    }
    size_t size() const { return defs_.size(); }

private:
    std::vector<WeaponDef> defs_;       // never reallocated after load (pointers handed out are used after load only)
    std::map<std::string, size_t> byId_;
    void addOriginals() {
        for (int s = 0; s < SLOT_COUNT; ++s) {
            WeaponDef d; d.id = std::string("original_") + slotInfo(s).key; d.name = slotInfo(s).key;
            d.category = slotInfo(s).category; d.slot = s; d.isOriginal = true;
            byId_[d.id] = defs_.size(); defs_.push_back(d);
        }
    }
};

// Which weapon *definition* currently occupies each engine slot. Ownership (does the player have the slot, ammo)
// stays in the game's own CPed::m_aWeapons; this only tracks "which pistol is the pistol slot".
class SlotState {
public:
    explicit SlotState(const Registry& r) : reg_(&r) { reset(); }
    void reset() { for (int s = 0; s < SLOT_COUNT; ++s) active_[s] = &reg_->original(s); }
    const WeaponDef& active(int slot) const { return *active_[slot]; }

    struct Result { bool changed; const WeaponDef* replaced; };
    // Pickup of `d` replaces whatever definition currently occupies d.slot (never accumulates).
    Result equip(const WeaponDef& d) {
        const WeaponDef* prev = active_[d.slot];
        if (prev == &d) return {false, prev};
        active_[d.slot] = &d;
        return {true, prev};
    }
private:
    const Registry* reg_;
    const WeaponDef* active_[SLOT_COUNT];
};

// ---- Pack loading -----------------------------------------------------------------------------------------------
inline void loadPack(const std::string& weaponsDir, Registry& reg, Diags& diags) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(weaponsDir, ec)) { diags.push_back({Diag::Info, weaponsDir, "no weapons directory; loader inactive"}); return; }
    std::vector<fs::path> dirs;
    for (auto& e : fs::directory_iterator(weaponsDir, ec)) if (e.is_directory()) dirs.push_back(e.path());
    std::sort(dirs.begin(), dirs.end());   // deterministic "first definition wins"
    FileExists exists = [](const std::string& p) { std::error_code e; return fs::is_regular_file(p, e); };
    for (auto& d : dirs) {
        auto xmlPath = d / "weapon.xml";
        std::ifstream f(xmlPath, std::ios::binary);
        if (!f) { diags.push_back({Diag::Warning, d.generic_string(), "no weapon.xml, skipped"}); continue; }
        std::stringstream ss; ss << f.rdbuf();
        auto def = parseWeaponXml(ss.str(), d.generic_string(), exists, diags);
        if (def) reg.add(*def, diags);
    }
}
} // namespace wl
