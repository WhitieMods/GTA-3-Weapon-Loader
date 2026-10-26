// Runtime of the GTA III 1.0 weapon loader. Everything here sits on verified addresses (gta3_addrs.hpp).
// STATUS: compiled-syntax-checked only in the authoring environment; never run inside the game (see docs/TESTING_REPORT.md).
#pragma once
#include <atomic>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "../core/ifp_probe.hpp"
#include "../core/model_ids.hpp"
#include "../core/ogg_vorbis.hpp"
#include "../core/png_decode.hpp"
#include "../core/save_state.hpp"
#include "../core/variant_logic.hpp"
#include "../core/weapon_core.hpp"
#include "audio_win.hpp"
#include "engine.hpp"

namespace wl { namespace rt {
using namespace eng;

struct Config {
    bool spawnPickups = true; float audioVolume = 1.f; int spawnHereKey = 0;
    std::map<int, std::string> giveKeys;               // debug "instant pickup" keys
};
struct BaseInfo { InfoStats s; int model = -1, anim1 = -1, anim2 = -1; float loopStart = 0, loopEnd = 0, frameFire = 0, frame2Fire = 0; };
struct Rt {                                              // per custom definition, filled by loadAssets()
    int handModel = -1, pickupModel = -1, txdSlot = -1;
    void* icon = nullptr; int anim1 = -1, anim2 = -1;
    std::shared_ptr<const Pcm> fire, reload, empty;      // guarded by audioCs while the decoder thread runs
};

inline Config cfg;
inline Registry* reg = nullptr;
inline SlotState* state = nullptr;
inline BaseInfo base[SLOT_COUNT];
inline std::map<std::string, Rt> rts;
inline std::map<int, const WeaponDef*> pickupDef;          // custom pickup model id -> definition
inline void* iconOrig[SLOT_COUNT] = {};
inline void* iconOurs[SLOT_COUNT] = {};
inline Mixer mixer; inline bool mixerOk = false;
inline CRITICAL_SECTION audioCs; inline bool audioCsInit = false;
inline const WeaponDef* pending = nullptr;                 // set by WeaponForModel for a custom pickup model
inline bool assetsLoaded = false, needSpawn = false, loadedSave = false;
inline int framesSinceInit = 0;
inline uint32_t lastEmptyMs = 0;
inline void* (WL_THISCALL* origGive)(void*, int, unsigned) = nullptr;   // trampoline (set in plugin.cpp)

inline const Rt* rtFor(const WeaponDef& d) { auto it = rts.find(d.id); return it == rts.end() ? nullptr : &it->second; }
inline bool inPickupUpdate(void* ra) { uint32_t a = u32(ra); return a >= gta3::CPickup_Update_begin && a < gta3::CPickup_Update_end; }

// ---- helpers --------------------------------------------------------------------------------------------------------
inline bool readFile(const std::string& path, std::vector<uint8_t>& out, size_t maxBytes) {
    std::ifstream f(path, std::ios::binary | std::ios::ate); if (!f) return false;
    std::streamoff n = f.tellg(); if (n < 0 || (size_t)n > maxBytes) return false;
    out.resize((size_t)n); f.seekg(0); f.read(reinterpret_cast<char*>(out.data()), n); return (bool)f;
}
inline void snapshotBase() {
    for (int s = 0; s < SLOT_COUNT; ++s) {
        uint32_t a = infoAddr(s); using namespace gta3::winfo; BaseInfo& b = base[s];
        b.s = {at<float>(a + range), at<int>(a + fireRate), at<int>(a + reload), at<int>(a + clipAmmo), at<int>(a + damage)};
        b.model = at<int>(a + modelId); b.anim1 = at<int>(a + anim1); b.anim2 = at<int>(a + anim2);
        b.loopStart = at<float>(a + loopStart); b.loopEnd = at<float>(a + loopEnd);
        b.frameFire = at<float>(a + frameFire); b.frame2Fire = at<float>(a + frame2Fire);
    }
}

// ---- STAGE 1: DFF / TXD / pickup models ---------------------------------------------------------------------------------
inline std::set<int> claimedIds;

// Registers one simple model through the engine's own IDE path (CFileLoader::LoadObject), then reads the DFF into it
// with the weapon's TXD current, exactly like the streamer does for normal models.
inline int registerModel(const WeaponDef& d, char kind, int txdSlot, const std::string& txd, const std::string& dffRel) {
    if (simpleStoreCount() >= gta3::kSimpleModelStoreSize) { logf("[%s] model store full (%d/%d)", d.id.c_str(), simpleStoreCount(), gta3::kSimpleModelStoreSize); return -1; }
    int id = allocateModelId(d.id + "#" + kind, [&](int i) { return modelSlotFree(i) && !claimedIds.count(i); });
    if (id < 0) { logf("[%s] no free custom model id", d.id.c_str()); return -1; }
    std::string name = modelName(d.id, kind);
    if (modelNameTaken(name)) { logf("[%s] model name '%s' already exists", d.id.c_str(), name.c_str()); return -1; }
    registerObject(ideLine(id, name, txd));
    if (!modelInfo(id)) { logf("[%s] engine did not create model info for id %d", d.id.c_str(), id); return -1; }
    claimedIds.insert(id);
    if (!loadDffInto(id, txdSlot, d.dir + "/" + dffRel)) { logf("[%s] DFF load failed: %s (model id %d left unused)", d.id.c_str(), dffRel.c_str(), id); return -1; }
    markStreamLoaded(id); markStreamLoaded(gta3::kStreamTxdOffset + txdSlot);
    logf("[%s] %s model '%s' id=%d txdSlot=%d loaded from %s", d.id.c_str(), kind == 'h' ? "hand" : "pickup", name.c_str(), id, txdSlot, dffRel.c_str());
    return id;
}

// ---- STAGE 2: PNG icon ----------------------------------------------------------------------------------------------------
inline void* loadIcon(const WeaponDef& d) {
    std::vector<uint8_t> bytes; if (!readFile(d.dir + "/" + d.icon, bytes, 4u << 20)) { logf("[%s] icon unreadable", d.id.c_str()); return nullptr; }
    Image img; std::string err;
    if (!decodePng(bytes.data(), bytes.size(), img, err)) { logf("[%s] icon PNG rejected: %s", d.id.c_str(), err.c_str()); return nullptr; }
    void* tex = makeTexture(img, "wlicon");
    logf("[%s] icon %dx%d -> %s", d.id.c_str(), img.w, img.h, tex ? "texture created" : "RenderWare texture creation FAILED");
    return tex;
}
inline void setIcon(int slot, void* tex) {
    void*& cur = at<void*>(gta3::CHud_Sprites + 4u * (uint32_t)slot);
    if (tex) { if (cur != iconOurs[slot]) iconOrig[slot] = cur; iconOurs[slot] = tex; cur = tex; }
    else if (iconOurs[slot]) { if (cur == iconOurs[slot] && iconOrig[slot]) cur = iconOrig[slot]; iconOurs[slot] = nullptr; iconOrig[slot] = nullptr; }
}

// ---- STAGE 3: OGG audio (decoded on a worker thread; played by the mixer) ----------------------------------------------------
struct AudioJob { std::string id; int which; std::string path; };   // which: 0 fire, 1 reload, 2 empty
inline std::vector<AudioJob> audioJobs;
inline DWORD WINAPI audioWorker(LPVOID) {
    for (auto& j : audioJobs) {
        std::vector<uint8_t> bytes; auto pcm = std::make_shared<Pcm>(); std::string err;
        if (!readFile(j.path, bytes, 8u << 20) || !decodeOgg(bytes.data(), bytes.size(), *pcm, err, 20.0)) { logf("[%s] OGG '%s' failed: %s", j.id.c_str(), j.path.c_str(), err.empty() ? "unreadable" : err.c_str()); continue; }
        EnterCriticalSection(&audioCs);
        auto it = rts.find(j.id);
        if (it != rts.end()) (j.which == 0 ? it->second.fire : j.which == 1 ? it->second.reload : it->second.empty) = pcm;
        LeaveCriticalSection(&audioCs);
        logf("[%s] OGG decoded: %s (%d ch, %d Hz, %.2fs)", j.id.c_str(), j.path.c_str(), pcm->channels, pcm->rate, (double)pcm->frames() / pcm->rate);
    }
    return 0;
}
inline std::shared_ptr<const Pcm> sound(const WeaponDef& d, int which) {
    const Rt* r = rtFor(d); if (!r) return nullptr;
    EnterCriticalSection(&audioCs);
    auto p = which == 0 ? r->fire : which == 1 ? r->reload : r->empty;
    LeaveCriticalSection(&audioCs); return p;
}
inline void playSound(const WeaponDef& d, int which) {
    auto s = sound(d, which); if (!s || !mixerOk) return;
    float sfx = (float)at<int>(gta3::Prefs_SfxVolume) / 127.f;
    mixer.play(s, sfx * d.volume * cfg.audioVolume);
}

// ---- STAGE 5: animations ------------------------------------------------------------------------------------------------------
struct Wanted { const WeaponDef* def; int which; std::string name; };

// Appends custom animations to the engine's standard ped animation group. Engine limits (fixed static tables:
// 2 animation blocks, 250 hierarchies) are checked against the IFP header BEFORE the engine reads the file.
inline bool addCustomAnimations(const std::vector<Wanted>& wanted) {
    using namespace gta3;
    std::map<std::string, void*> blockOf; std::vector<std::pair<const Wanted*, void*>> hiers;
    for (auto& w : wanted) {
        const std::string path = w.def->dir + "/" + w.def->anim.ifp;
        if (!blockOf.count(path)) {
            std::vector<uint8_t> bytes; IfpInfo info; std::string err;
            if (!readFile(path, bytes, 4u << 20) || !probeIfp(bytes.data(), bytes.size(), info, err)) { logf("[%s] IFP rejected: %s", w.def->id.c_str(), err.c_str()); continue; }
            if (at<int>(ms_numAnimBlocks) >= kNumAnimBlocks) { logf("[%s] IFP refused: engine already has %d/%d animation blocks", w.def->id.c_str(), at<int>(ms_numAnimBlocks), kNumAnimBlocks); continue; }
            if (at<int>(ms_numAnimations) + info.numAnims > kNumAnimations) { logf("[%s] IFP refused: %d + %d animations would exceed %d", w.def->id.c_str(), at<int>(ms_numAnimations), info.numAnims, kNumAnimations); continue; }
            fn<void(WL_CDECL*)(const char*)>(CAnimManager_LoadAnimFile)(path.c_str());
            void* blk = fn<void* (WL_CDECL*)(const char*)>(CAnimManager_GetAnimationBlock)(info.blockName.c_str());
            if (!blk) { logf("[%s] IFP loaded but block '%s' not found", w.def->id.c_str(), info.blockName.c_str()); continue; }
            blockOf[path] = blk; logf("[%s] IFP loaded: block '%s', %d animations", w.def->id.c_str(), info.blockName.c_str(), info.numAnims);
        }
        auto b = blockOf.find(path); if (b == blockOf.end()) continue;
        void* h = fn<void* (WL_CDECL*)(const char*, void*)>(CAnimManager_GetAnimationByBlock)(w.name.c_str(), b->second);
        if (!h) { logf("[%s] animation '%s' not found in IFP block", w.def->id.c_str(), w.name.c_str()); continue; }
        hiers.push_back({&w, h});
    }
    if (hiers.empty()) return false;
    uint32_t grp = at<uint32_t>(CAnimManager_ms_aAnimAssocGroups); if (!grp) return false;   // element 0 = ASSOCGRP_STD
    uint32_t oldList = at<uint32_t>(grp); int oldN = at<int>(grp + 4);
    int newN = oldN + 1 + (int)hiers.size();   // +1 filler so no custom id equals ANIM_STD_NUM (the engine's "no second anim" value)
    uint32_t mem = fn<uint32_t(WL_CDECL*)(unsigned)>(operator_new_array)((unsigned)newN * kAnimAssocSize + 4);
    if (!mem) return false;
    at<uint32_t>(mem) = (uint32_t)newN;                              // MSVC array cookie, so the engine's delete[] stays valid
    uint32_t arr = mem + 4; std::memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(arr)), reinterpret_cast<void*>(static_cast<uintptr_t>(oldList)), (size_t)oldN * kAnimAssocSize);
    void* mi = modelInfo(kAssocClumpModel); if (!mi) return false;
    void* clump = fn<void* (WL_THISCALL*)(void*)>(at<uint32_t*>(u32(mi))[3])(mi);   // vtable slot 3 = CreateInstance
    if (!clump) return false;
    fn<void(WL_CDECL*)(void*)>(RpAnimBlendClumpInit)(clump);
    auto ctor = fn<void(WL_THISCALL*)(void*)>(CAnimBlendAssociation_ctor);
    auto init = fn<void(WL_THISCALL*)(void*, void*, void*)>(CAnimBlendAssociation_Init_clump);
    ctor(reinterpret_cast<void*>(static_cast<uintptr_t>(arr + (uint32_t)oldN * kAnimAssocSize)));   // filler
    for (size_t i = 0; i < hiers.size(); ++i) {
        const Wanted& w = *hiers[i].first; int id = oldN + 1 + (int)i;
        uint32_t el = arr + (uint32_t)id * kAnimAssocSize; ctor(reinterpret_cast<void*>(static_cast<uintptr_t>(el)));
        init(reinterpret_cast<void*>(static_cast<uintptr_t>(el)), clump, hiers[i].second);
        at<int>(el + assoc::animId) = id;
        int baseAnim = base[w.def->slot].anim1;                      // inherit the replaced animation's playback flags
        at<int>(el + assoc::flags) = (baseAnim >= 0 && baseAnim < oldN) ? at<int>(oldList + (uint32_t)baseAnim * kAnimAssocSize + assoc::flags) : 0;
        Rt& r = rts[w.def->id]; (w.which == 1 ? r.anim1 : r.anim2) = id;
        logf("[%s] custom animation '%s' -> std animId %d", w.def->id.c_str(), w.name.c_str(), id);
    }
    fn<void(WL_CDECL*)(void*)>(RpClumpDestroy)(clump);
    at<uint32_t>(grp) = arr; at<int>(grp + 4) = newN;                // old array intentionally leaked (never destructed)
    return true;
}
inline void resolveAnimations() {
    std::vector<Wanted> wanted;
    for (int s = 0; s < SLOT_COUNT; ++s) for (const WeaponDef* d : reg->forSlot(s)) {
        if (d->isOriginal || !d->anim.any()) continue;
        Rt& r = rts[d->id];
        for (int which = 1; which <= 2; ++which) {
            const std::string& nm = which == 1 ? d->anim.anim1 : d->anim.anim2; if (nm.empty()) continue;
            int id = stdAnimId(nm.c_str());
            if (id >= 0) { (which == 1 ? r.anim1 : r.anim2) = id; logf("[%s] animation '%s' = std animId %d", d->id.c_str(), nm.c_str(), id); }
            else if (!d->anim.ifp.empty()) wanted.push_back({d, which, nm});
            else logf("[%s] animation '%s' not found in the standard ped animations (ignored)", d->id.c_str(), nm.c_str());
        }
    }
    if (!wanted.empty()) addCustomAnimations(wanted);
}

// ---- loading everything (once; the game re-runs InitialiseWeapons on every new game / load) -----------------------------------
inline void loadAssets() {
    if (assetsLoaded) return;
    assetsLoaded = true;
    for (int s = 0; s < SLOT_COUNT; ++s) for (const WeaponDef* d : reg->forSlot(s)) {
        if (d->isOriginal) continue;
        Rt rt;
        if (!d->model.empty() || !d->pickupModel.empty()) {
            std::string tn = txdName(d->id); int slot = findTxdSlot(tn.c_str()); if (slot < 0) slot = addTxdSlot(tn.c_str());
            if (slot >= 0) {
                rt.txdSlot = slot;
                if (!d->txd.empty() && !loadTxd(slot, d->dir + "/" + d->txd)) logf("[%s] TXD load failed: %s", d->id.c_str(), d->txd.c_str());
                if (!d->model.empty()) rt.handModel = registerModel(*d, 'h', slot, tn, d->model);
                if (!d->pickupModel.empty()) rt.pickupModel = registerModel(*d, 'p', slot, tn, d->pickupModel);
                if (rt.pickupModel < 0) rt.pickupModel = rt.handModel;      // a hand model alone also serves as the pickup
            } else logf("[%s] could not create a TXD slot", d->id.c_str());
        }
        if (!d->icon.empty()) rt.icon = loadIcon(*d);
        for (int w = 0; w < 3; ++w) { const std::string& f = w == 0 ? d->audio.fire : w == 1 ? d->audio.reload : d->audio.empty; if (!f.empty()) audioJobs.push_back({d->id, w, d->dir + "/" + f}); }
        rts[d->id] = rt;
    }
    for (auto& kv : rts) if (kv.second.pickupModel >= 0) pickupDef[kv.second.pickupModel] = reg->find(kv.first);
    resolveAnimations();
    if (!audioJobs.empty()) { mixerOk = mixer.start(); logf("audio mixer: %s", mixerOk ? "started (waveOut)" : "FAILED to open an audio device"); CreateThread(nullptr, 0, &audioWorker, nullptr, 0, nullptr); }
}

// ---- STAGE 4/6: variants ----------------------------------------------------------------------------------------------------------
// Writes the definition's data into the engine's per-type CWeaponInfo (global: see README limitation) and swaps the HUD icon.
inline void applyInfo(const WeaponDef& d) {
    using namespace gta3::winfo; const int s = d.slot; const BaseInfo& b = base[s]; const Rt* r = rtFor(d); uint32_t a = infoAddr(s);
    InfoStats st = resolveStats(b.s, d.stats);
    at<float>(a + range) = st.range; at<int>(a + fireRate) = st.fireRate; at<int>(a + reload) = st.reloadTime;
    at<int>(a + clipAmmo) = st.clipSize; at<int>(a + damage) = st.damage;
    at<int>(a + modelId) = (r && r->handModel >= 0) ? r->handModel : b.model;
    at<int>(a + anim1) = (r && r->anim1 >= 0) ? r->anim1 : b.anim1;
    at<int>(a + anim2) = (r && r->anim2 >= 0) ? r->anim2 : b.anim2;
    at<float>(a + loopStart) = d.anim.loopStart.value_or(b.loopStart); at<float>(a + loopEnd) = d.anim.loopEnd.value_or(b.loopEnd);
    at<float>(a + frameFire) = d.anim.frameFire.value_or(b.frameFire); at<float>(a + frame2Fire) = d.anim.frame2Fire.value_or(b.frame2Fire);
    setIcon(s, d.isOriginal ? nullptr : (r ? r->icon : nullptr));
}
inline void resetAll() { if (!state) return; for (int s = 0; s < SLOT_COUNT; ++s) { setIcon(s, nullptr); } state->reset(); }

// The single code path for "this weapon definition now occupies its slot": real pickups, debug keys and save loading.
inline void occupySlot(const WeaponDef& d) {
    auto res = state->equip(d); applyInfo(d);
    if (void* ped = player()) {
        if (ownsSlot(ped, d.slot)) {
            int& total = at<int>(pedWeapon(ped, d.slot) + gta3::weapon::totalAmmo);
            if (total > d.maxAmmo) total = d.maxAmmo;
            if (currentSlot(ped) == d.slot) makeChangesForNewWeapon(ped, d.slot);   // swap held model + refresh clip/anims now
        }
    }
    logf("slot %d: '%s' replaced '%s'%s", d.slot, d.id.c_str(), res.replaced ? res.replaced->id.c_str() : "?", res.changed ? "" : " (no change)");
}

inline void debugPickup(const WeaponDef& d) {
    void* ped = player(); if (!ped) return;
    unsigned ammo = d.pickupAmmo >= 0 ? (unsigned)d.pickupAmmo : 30u;
    origGive(ped, d.slot, ammo);                       // engine inventory work, same call CPickup::Update makes
    occupySlot(d);
    at<uint8_t>(u32(ped) + gta3::ped_selectedSlot) = (uint8_t)d.slot;
}

// ---- STAGE 1b: world pickups ------------------------------------------------------------------------------------------------------
inline bool pickupExists(int model, float x, float y, float z) {
    for (uint32_t i = 0; i < gta3::kNumGeneralPickups; ++i) {
        uint32_t a = gta3::CPickups_aPickUps + i * gta3::kPickupSize;
        if (at<uint8_t>(a + gta3::pickup::type) && at<int16_t>(a + gta3::pickup::model) == model &&
            std::fabs(at<float>(a + gta3::pickup::pos) - x) < 0.5f && std::fabs(at<float>(a + gta3::pickup::pos + 4) - y) < 0.5f && std::fabs(at<float>(a + gta3::pickup::pos + 8) - z) < 1.5f) return true;
    }
    return false;
}
inline int createPickup(float x, float y, float z, int model, int type, int ammo) {
    return fn<int(WL_CDECL*)(float, float, float, unsigned, unsigned, unsigned)>(gta3::CPickups_GenerateNewOne)(x, y, z, (unsigned)model, (unsigned)type, (unsigned)(ammo < 0 ? 0 : ammo));
}
inline void spawnConfigured() {
    int n = 0;
    for (auto& kv : rts) {
        const WeaponDef* d = reg->find(kv.first); const Rt& r = kv.second;
        if (!d || d->spawns.empty()) continue;
        if (r.pickupModel < 0) { logf("[%s] has <spawn> entries but no loadable model: cannot create world pickups", d->id.c_str()); continue; }
        for (auto& s : d->spawns) {
            if (pickupExists(r.pickupModel, s.x, s.y, s.z)) continue;
            createPickup(s.x, s.y, s.z, r.pickupModel, s.type, s.ammo >= 0 ? s.ammo : d->pickupAmmo); ++n;
        }
    }
    logf("spawned %d configured pickup(s)", n);
}
inline void spawnHere() {
    void* p = player(); if (!p) return; float pos[3] = {0, 0, 0};
    fn<float* (WL_CDECL*)(float*)>(gta3::FindPlayerCoors)(pos);
    int i = 0;
    for (auto& kv : rts) { const WeaponDef* d = reg->find(kv.first); if (!d || kv.second.pickupModel < 0) continue;
        createPickup(pos[0] + 1.5f * (float)(++i), pos[1] + 1.5f, pos[2] + 0.3f, kv.second.pickupModel, 3 /*PICKUP_ONCE*/, d->pickupAmmo); }
    logf("debug: created %d pickup(s) at the player", i);
}

// ---- STAGE 4: persistence -------------------------------------------------------------------------------------------------------------
inline void writeSidecarAfterSave(const std::string& savePath) {
    SaveSidecar sc; void* ped = player();
    if (ped && state) sc.slotToWeapon = collectSaveMap(*state, [&](int s) { return ownsSlot(ped, s); });
    sc.stamp = stampFile(savePath);
    if (!sc.stamp.ok) { logf("sidecar: cannot stat '%s'", savePath.c_str()); return; }
    logf("sidecar %s: %zu custom slot(s) -> %s", savePath.c_str(), sc.slotToWeapon.size(), writeSidecar(savePath, sc) ? "written" : "WRITE FAILED");
}
// Called right before the engine's GenericLoad: re-establish saved variants so the ped/pool load sees the right models.
inline void preApplyFromSidecar(const std::string& savePath) {
    std::string why; auto m = readSidecarFor(savePath, &why);
    if (!m) { logf("load '%s': no variants restored (%s)", savePath.c_str(), why.c_str()); return; }
    LoadPlan plan = planLoad(*reg, *m);
    for (auto& n : plan.notes) logf("load: %s", n.c_str());
    for (const WeaponDef* d : plan.apply) { state->equip(*d); applyInfo(*d); logf("load: restoring slot %d = '%s'", d->slot, d->id.c_str()); }
}
inline void verifyAfterLoad(bool ok) {
    void* ped = player();
    for (int s : slotsToRevert(*state, ok, [&](int slot) { return ped && ownsSlot(ped, slot); })) {
        logf("load: slot %d not owned after load; reverting to the original", s); state->equip(reg->original(s)); applyInfo(reg->original(s));
    }
}

// ---- Init / per-frame ----------------------------------------------------------------------------------------------------------------------
inline void init(const std::string& weaponsDir) {
    if (!audioCsInit) { InitializeCriticalSection(&audioCs); audioCsInit = true; }
    delete state; delete reg; reg = new Registry(); Diags diags;
    loadPack(weaponsDir, *reg, diags);
    for (auto& x : diags) logf("[%s] %s: %s", x.level == Diag::Error ? "ERROR" : x.level == Diag::Warning ? "warn" : "info", x.where.c_str(), x.msg.c_str());
    state = new SlotState(*reg); snapshotBase();
    logf("registry: %u definitions (%u custom)", (unsigned)reg->size(), (unsigned)(reg->size() - gta3::kNumWeaponSlots));
}
inline void onWeaponInit() {                       // after CWeaponInfo::Initialise re-read weapon.dat
    for (int s = 0; s < SLOT_COUNT; ++s) setIcon(s, nullptr);
    static bool first = true;
    if (first) { first = false; init("weaponloader/weapons"); loadAssets(); }
    else { snapshotBase(); if (state) state->reset(); }       // engine stats are original again; variants reset (a load re-applies them)
    needSpawn = true; loadedSave = false; framesSinceInit = 0; pending = nullptr;
}
inline void tick() {
    if (!reg || !state) return;
    for (auto& kv : cfg.giveKeys) {
        static std::map<int, bool> was; bool down = (GetAsyncKeyState(kv.first) & 0x8000) != 0;
        if (down && !was[kv.first]) { if (auto* d = reg->find(kv.second)) debugPickup(*d); else logf("debug key %02X: unknown weapon '%s'", kv.first, kv.second.c_str()); }
        was[kv.first] = down;
    }
    if (cfg.spawnHereKey) { static bool was = false; bool down = (GetAsyncKeyState(cfg.spawnHereKey) & 0x8000) != 0; if (down && !was) spawnHere(); was = down; }
    if (needSpawn && player() && ++framesSinceInit > 90) { needSpawn = false; if (cfg.spawnPickups && !loadedSave) spawnConfigured(); }
}

inline void readIni() {
    const char* ini = ".\\weaponloader\\loader.ini";
    cfg.spawnPickups = GetPrivateProfileIntA("General", "SpawnPickups", 1, ini) != 0;
    char b[64]; GetPrivateProfileStringA("General", "AudioVolume", "1.0", b, sizeof b, ini); cfg.audioVolume = (float)std::atof(b);
    if (!(cfg.audioVolume >= 0.f && cfg.audioVolume <= 4.f)) cfg.audioVolume = 1.f;
    GetPrivateProfileStringA("Debug", "SpawnHereKey", "0", b, sizeof b, ini); cfg.spawnHereKey = (int)std::strtol(b, nullptr, 0);
    char sec[8192]; GetPrivateProfileSectionA("Debug", sec, sizeof sec, ini);
    for (const char* p = sec; *p; p += std::strlen(p) + 1) {
        std::string line = p; auto eq = line.find('='); if (eq == std::string::npos || line.rfind("0x", 0) != 0) continue;
        long vk = std::strtol(line.substr(0, eq).c_str(), nullptr, 0); if (vk > 0 && vk < 256) cfg.giveKeys[(int)vk] = line.substr(eq + 1);
    }
}
}} // namespace wl::rt
