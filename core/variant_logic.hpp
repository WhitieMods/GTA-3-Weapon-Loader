// Gameplay decisions of the loader, separated from the Win32/engine layer so they can be unit-tested on the host.
// The plugin calls exactly these functions from its hooks.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "weapon_core.hpp"

namespace wl {
// A GiveWeapon(type) issued by CPickup::Update. 'pending' is the custom definition WeaponForModel just mapped (or null
// when the pickup was an ORIGINAL weapon model). Returns the definition that must now occupy the slot, or null for no change.
//  * custom pickup of that slot            -> that custom weapon replaces whatever is in the slot
//  * original pickup while a custom is active -> the original weapon replaces the custom one
inline const WeaponDef* variantForPickup(const SlotState& st, const Registry& reg, int type, const WeaponDef* pending) {
    if (type < 1 || type >= SLOT_COUNT || !slotInfo(type).allowVariants) return nullptr;
    if (pending && pending->slot == type) return pending;
    if (!st.active(type).isOriginal) return &reg.original(type);
    return nullptr;
}
// Slots the sidecar must record: custom variant active AND the player actually owns that slot.
inline std::map<int, std::string> collectSaveMap(const SlotState& st, const std::function<bool(int)>& ownsSlot) {
    std::map<int, std::string> m;
    for (int s = 0; s < SLOT_COUNT; ++s) { const WeaponDef& a = st.active(s); if (!a.isOriginal && ownsSlot(s)) m[s] = a.id; }
    return m;
}
// What to re-apply before the engine loads a save. Unknown/removed weapons and wrong-slot entries fall back to originals.
struct LoadPlan { std::vector<const WeaponDef*> apply; std::vector<std::string> notes; };
inline LoadPlan planLoad(const Registry& reg, const std::map<int, std::string>& saved) {
    LoadPlan p;
    for (auto& kv : saved) {
        const WeaponDef* d = reg.find(kv.second);
        if (!d || d->isOriginal) p.notes.push_back("saved weapon '" + kv.second + "' is not installed; slot " + std::to_string(kv.first) + " keeps the original");
        else if (d->slot != kv.first) p.notes.push_back("saved weapon '" + kv.second + "' no longer belongs to slot " + std::to_string(kv.first));
        else p.apply.push_back(d);
    }
    return p;
}
// After the engine finished loading: slots whose custom variant must be reverted (load failed or slot not owned).
inline std::vector<int> slotsToRevert(const SlotState& st, bool loadOk, const std::function<bool(int)>& ownsSlot) {
    std::vector<int> r;
    for (int s = 0; s < SLOT_COUNT; ++s) if (!st.active(s).isOriginal && (!loadOk || !ownsSlot(s))) r.push_back(s);
    return r;
}
} // namespace wl
