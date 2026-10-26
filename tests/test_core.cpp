// Host-side tests of the portable core. These do NOT touch the game; they only prove the logic in core/.
#include <cstdio>
#include <cstdlib>
#include <set>
#include "../core/weapon_core.hpp"
#include "../core/save_state.hpp"
#include "../core/model_ids.hpp"
#include "../core/image_util.hpp"
#include "../core/ifp_probe.hpp"
#include "../core/mixer_core.hpp"
#include "../core/variant_logic.hpp"
using namespace wl;

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static const FileExists kAll = [](const std::string&) { return true; };
static const FileExists kNone = [](const std::string&) { return false; };

static std::string xml(const std::string& id, const std::string& slot, const std::string& extra = "",
                       const std::string& audio = "<fire>fire.ogg</fire>") {
    return "<weapon id=\"" + id + "\"><name>" + id + "</name><slot>" + slot + "</slot>" + extra +
           "<audio>" + audio + "</audio></weapon>";
}
static WeaponDef must(const std::string& x) {
    Diags d; auto w = parseWeaponXml(x, "t/" , kAll, d);
    if (!w) { std::printf("unexpected reject: %s\n", d.empty() ? "?" : d[0].msg.c_str()); std::exit(2); }
    return *w;
}

static void test_pistol_replacement() {
    Registry reg; Diags d;
    auto deagle = must(xml("desert_eagle", "pistol"));
    auto python = must(xml("colt_python", "pistol"));
    CHECK(reg.add(deagle, d)); CHECK(reg.add(python, d));
    SlotState st(reg);
    CHECK(st.active(S_PISTOL).isOriginal);

    auto r1 = st.equip(*reg.find("desert_eagle"));           // original -> deagle
    CHECK(r1.changed && r1.replaced->id == "original_pistol");
    CHECK(st.active(S_PISTOL).id == "desert_eagle");

    auto r2 = st.equip(*reg.find("colt_python"));            // deagle -> python
    CHECK(r2.changed && r2.replaced->id == "desert_eagle");
    CHECK(st.active(S_PISTOL).id == "colt_python");

    auto r3 = st.equip(reg.original(S_PISTOL));              // python -> original again
    CHECK(r3.changed && r3.replaced->id == "colt_python");
    CHECK(st.active(S_PISTOL).isOriginal);

    auto r4 = st.equip(reg.original(S_PISTOL));              // same weapon again: no change
    CHECK(!r4.changed);
    // One occupant per slot by construction: the state holds exactly SLOT_COUNT pointers.
    CHECK(reg.forSlot(S_PISTOL).size() == 3);                // original + 2 custom share the slot
}

static void test_independent_slots() {
    Registry reg; Diags d;
    reg.add(must(xml("desert_eagle", "pistol")), d);
    reg.add(must(xml("spas12", "shotgun")), d);
    SlotState st(reg);
    st.equip(*reg.find("desert_eagle"));
    CHECK(st.active(S_SHOTGUN).isOriginal);                  // other slots untouched
    st.equip(*reg.find("spas12"));
    CHECK(st.active(S_PISTOL).id == "desert_eagle");
    CHECK(st.active(S_SHOTGUN).id == "spas12");
}

static void test_rifles_share_slot() {
    Registry reg; Diags d;
    reg.add(must(xml("m4", "ak47")), d);
    reg.add(must(xml("ruger_556", "ak47")), d);
    SlotState st(reg);
    st.equip(*reg.find("m4"));
    CHECK(st.equip(*reg.find("ruger_556")).replaced->id == "m4");
    CHECK(st.equip(reg.original(S_AK47)).replaced->id == "ruger_556");
    CHECK(reg.forSlot(S_AK47).size() == 3);
    CHECK(st.active(S_M16).isOriginal);                      // m16 is a different engine slot
}

static void test_validation() {
    Diags d;
    // duplicate id
    Registry reg;
    CHECK(reg.add(must(xml("dup", "pistol")), d));
    Diags d2; CHECK(!reg.add(must(xml("dup", "uzi")), d2)); CHECK(hasError(d2));
    // bad ids
    d.clear(); CHECK(!parseWeaponXml(xml("Bad Id", "pistol"), "t", kAll, d)); CHECK(hasError(d));
    d.clear(); CHECK(!parseWeaponXml(xml("original_pistol", "pistol"), "t", kAll, d));
    // unknown slot, disallowed slots
    d.clear(); CHECK(!parseWeaponXml(xml("a", "banana"), "t", kAll, d));
    d.clear(); CHECK(!parseWeaponXml(xml("a", "fist"), "t", kAll, d));
    d.clear(); CHECK(!parseWeaponXml(xml("a", "detonator"), "t", kAll, d));
    // category conflict
    d.clear(); CHECK(!parseWeaponXml("<weapon id=\"a\"><name>A</name><slot>pistol</slot><category>shotgun</category></weapon>", "t", kAll, d));
    // value ranges
    d.clear(); CHECK(!parseWeaponXml(xml("a", "pistol", "<stats damage=\"-5\"/>"), "t", kAll, d));
    d.clear(); CHECK(!parseWeaponXml(xml("a", "pistol", "<stats clipSize=\"0\"/>"), "t", kAll, d));
    d.clear(); CHECK(!parseWeaponXml(xml("a", "pistol", "<stats range=\"abc\"/>"), "t", kAll, d));
    d.clear(); CHECK(!parseWeaponXml(xml("a", "pistol", "<ammo maxAmmo=\"100000\"/>"), "t", kAll, d));
    // malformed xml
    d.clear(); CHECK(!parseWeaponXml("<weapon id=\"a\"><name>x</weapon>", "t", kAll, d)); CHECK(hasError(d));
    // missing name
    d.clear(); CHECK(!parseWeaponXml("<weapon id=\"a\"><slot>pistol</slot></weapon>", "t", kAll, d));
    // path traversal / absolute
    d.clear(); CHECK(!parseWeaponXml(xml("a", "pistol", "", "<fire>../x.ogg</fire>"), "t", kAll, d));
    d.clear(); CHECK(!parseWeaponXml(xml("a", "pistol", "", "<fire>/etc/x.ogg</fire>"), "t", kAll, d));
    // OGG-only: a non-.ogg audio file is a hard error (no other-format path exists)
    d.clear(); CHECK(!parseWeaponXml(xml("a", "pistol", "", "<fire>fire.mp3</fire>"), "t", kAll, d));
    d.clear(); CHECK(!parseWeaponXml(xml("a", "pistol", "", "<fire>fire.wav</fire>"), "t", kAll, d)); CHECK(hasError(d));
    // wrong asset type
    d.clear(); CHECK(!parseWeaponXml(xml("a", "pistol", "<assets><icon>i.jpg</icon></assets>"), "t", kAll, d));
}

static void test_missing_files_degrade() {
    Diags d;
    auto w = parseWeaponXml(xml("a", "pistol", "<assets><model>m.dff</model><icon>i.png</icon></assets>"), "t", kNone, d);
    CHECK(w.has_value());                       // weapon still loads
    CHECK(w->audio.fire.empty() && w->model.empty() && w->icon.empty());   // missing assets simply disabled
    CHECK(!hasError(d));
    int warns = 0; for (auto& x : d) if (x.level == Diag::Warning) ++warns;
    CHECK(warns == 3);
}

static void test_stats_resolve() {
    InfoStats base{30.0f, 250, 350, 12, 25};
    auto d = must(xml("a", "pistol", "<stats damage=\"60\" clipSize=\"7\" fireRate=\"500\" range=\"40\"/>"));
    auto r = resolveStats(base, d.stats);
    CHECK(r.damage == 60 && r.clipSize == 7 && r.fireRate == 500 && r.range == 40.0f && r.reloadTime == 350);
    auto r2 = resolveStats(base, StatOverrides{});          // no overrides: unchanged
    CHECK(r2.damage == 25 && r2.clipSize == 12);
}

static void test_xml_parser() {
    std::string e;
    auto n = XmlParser::parse("<?xml version=\"1.0\"?><!-- c --><a x='1'><b>t &amp; u</b><c/></a>", e);
    CHECK(n && n->name == "a" && *n->attr("x") == "1" && n->child("b")->text == "t & u" && n->child("c"));
    CHECK(!XmlParser::parse("<a><b></a>", e)); CHECK(e.find("mismatched") != std::string::npos);
    CHECK(!XmlParser::parse("<a x=1/>", e));
    CHECK(!XmlParser::parse("<a/><b/>", e));
}

static void test_pack_on_disk(const char* packRoot) {
    Registry reg; Diags d;
    loadPack(std::string(packRoot) + "/weapons", reg, d);
    for (auto& x : d) if (x.level == Diag::Error) std::printf("pack error: %s: %s\n", x.where.c_str(), x.msg.c_str());
    CHECK(!hasError(d));
    CHECK(reg.find("desert_eagle") && reg.find("colt_python"));
    CHECK(reg.find("m4") && reg.find("ruger_556"));
    CHECK(reg.find("desert_eagle")->slot == S_PISTOL && reg.find("colt_python")->slot == S_PISTOL);
    CHECK(reg.find("m4")->slot == S_AK47 && reg.find("ruger_556")->slot == S_AK47);
    CHECK(reg.forSlot(S_PISTOL).size() == 3);
}


static void test_spawns_and_anims() {
    Diags d;
    auto w = parseWeaponXml(xml("a", "pistol", "<animations anim1=\"WEAPON_hgun_body\" frameFire=\"0.09\" loopStart=\"0.1\"/><spawns><spawn x=\"1\" y=\"2\" z=\"3\" type=\"3\" ammo=\"12\"/><spawn x=\"4\" y=\"5\" z=\"6\"/></spawns>"), "t", kAll, d);
    CHECK(w.has_value()); CHECK(w->spawns.size() == 2); CHECK(w->spawns[0].type == 3 && w->spawns[0].ammo == 12 && w->spawns[1].type == 2 && w->spawns[1].ammo == -1);
    CHECK(w->anim.anim1 == "WEAPON_hgun_body" && w->anim.frameFire && *w->anim.frameFire == 0.09f && w->anim.any());
    d.clear(); CHECK(!parseWeaponXml(xml("a", "pistol", "<spawns><spawn x=\"1\" y=\"2\"/></spawns>"), "t", kAll, d));              // missing z
    d.clear(); CHECK(!parseWeaponXml(xml("a", "pistol", "<spawns><spawn x=\"1\" y=\"2\" z=\"3\" type=\"9\"/></spawns>"), "t", kAll, d)); // bad type
    d.clear(); CHECK(!parseWeaponXml(xml("a", "pistol", "<spawns><spawn x=\"99999\" y=\"2\" z=\"3\"/></spawns>"), "t", kAll, d));
    d.clear(); CHECK(!parseWeaponXml(xml("a", "pistol", "<animations ifp=\"x.ifp\"/>"), "t", kAll, d));                                   // ifp without block
    d.clear(); CHECK(!parseWeaponXml(xml("a", "pistol", "<animations ifp=\"x.zip\" block=\"b\"/>"), "t", kAll, d));
    d.clear(); CHECK(!parseWeaponXml(xml("a", "pistol", "<animations anim1=\"this_name_is_way_longer_than_23_chars\"/>"), "t", kAll, d));
    d.clear(); auto ok = parseWeaponXml(xml("a", "pistol", "<animations ifp=\"x.ifp\" block=\"b\"/>"), "t", kAll, d); CHECK(ok && ok->anim.ifp == "x.ifp" && ok->anim.block == "b");
    d.clear(); CHECK(!parseWeaponXml("<weapon id=\"a\"><name>A</name><slot>pistol</slot><audio volume=\"9\"/></weapon>", "t", kAll, d));
}

static void test_save_sidecar() {
    SaveSidecar s; s.stamp = {1234, 0xdeadbeefcafef00dull, true}; s.slotToWeapon = {{2, "desert_eagle"}, {5, "m4"}};
    auto txt = serializeSidecar(s); auto back = parseSidecar(txt);
    CHECK(back && back->stamp.size == 1234 && back->stamp.hash == 0xdeadbeefcafef00dull && back->slotToWeapon == s.slotToWeapon);
    CHECK(!parseSidecar("garbage")); CHECK(!parseSidecar("WLSAVE1\nsize=1\n"));                     // missing hash
    CHECK(!parseSidecar("WLSAVE1\nsize=1\nhash=2\nslot.99=x\n")); CHECK(!parseSidecar("WLSAVE1\nsize=1\nhash=2\nbogus=1\n"));
    // end-to-end against real files: sidecar valid only while the .b file is unchanged
    namespace fs = std::filesystem; auto dir = fs::temp_directory_path() / "wl_save_test"; fs::create_directories(dir);
    auto save = (dir / "GTA3sf1.b").string();
    { std::ofstream f(save, std::ios::binary); for (int i = 0; i < 5000; ++i) f.put((char)(i * 31)); }
    SaveSidecar sc; sc.stamp = stampFile(save); sc.slotToWeapon = {{2, "desert_eagle"}};
    CHECK(sc.stamp.ok && sc.stamp.size == 5000); CHECK(writeSidecar(save, sc));
    std::string why; auto r = readSidecarFor(save, &why); CHECK(r && r->at(2) == "desert_eagle");
    { std::fstream f(save, std::ios::in | std::ios::out | std::ios::binary); f.seekp(100); f.put('X'); }   // save overwritten
    r = readSidecarFor(save, &why); CHECK(!r && why.find("stale") != std::string::npos);
    fs::remove(save + ".wl"); r = readSidecarFor(save, &why); CHECK(!r && why == "no sidecar");           // original save without sidecar
    fs::remove_all(dir);
}

static void test_model_ids() {
    std::set<int> used;
    auto isFree = [&](int id) { return !used.count(id); };
    int a = allocateModelId("desert_eagle#h", isFree); CHECK(a >= kCustomIdFirst && a <= kCustomIdLast);
    CHECK(allocateModelId("desert_eagle#h", isFree) == a);                                   // deterministic
    used.insert(a); int b = allocateModelId("desert_eagle#h", isFree); CHECK(b != a && b >= kCustomIdFirst);   // probes past a taken id
    // same key -> same id regardless of what else was allocated first (other weapons use different keys)
    std::set<int> other; other.insert(allocateModelId("colt_python#h", [&](int id) { return !other.count(id); }));
    std::set<int> u2; int a2 = allocateModelId("desert_eagle#h", [&](int id) { return !u2.count(id); }); CHECK(a2 == a);
    // full range -> -1
    CHECK(allocateModelId("x", [](int) { return false; }) == -1);
    // every id for 100 distinct keys is unique when we mark them used
    std::set<int> seen; bool uniq = true; for (int i = 0; i < 100; ++i) { int id = allocateModelId("w" + std::to_string(i), [&](int x) { return !seen.count(x); }); if (id < 0 || !seen.insert(id).second) uniq = false; } CHECK(uniq);
    CHECK(modelName("desert_eagle", 'h').size() < 24 && txdName("desert_eagle").size() < 24 && modelName("a", 'h') != modelName("a", 'p'));
    CHECK(ideLine(5123, "wlh1", "wlt1") == "5123 wlh1 wlt1 1 100 0");
}

static void test_resize() {
    Image s; s.w = 2; s.h = 2; s.rgba = {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,0};
    auto r = resizeBilinear(s, 4, 4); CHECK(r.w == 4 && r.h == 4 && r.rgba.size() == 64);
    CHECK(r.rgba[0] == 255 && r.rgba[1] == 0 && r.rgba[3] == 255);             // top-left stays pure red
    auto same = resizeBilinear(s, 2, 2); CHECK(same.rgba == s.rgba);          // identity size is lossless
    CHECK(nextPow2(1) == 1 && nextPow2(100) == 128 && nextPow2(128) == 128 && nextPow2(129) == 256);
    Image z; CHECK(resizeBilinear(z, 4, 4).rgba.size() == 64);                // empty source: no crash
}

static std::vector<uint8_t> mkIfp(bool anlf, int numAnims, const char* block) {
    std::vector<uint8_t> v; auto w32 = [&](uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back((uint8_t)(x >> (8 * i))); };
    auto tag = [&](const char* t) { for (int i = 0; i < 4; ++i) v.push_back((uint8_t)t[i]); };
    if (anlf) { tag("ANLF"); w32(4); w32(1); }
    tag("ANPK"); w32(100); tag("INFO"); w32(28); w32((uint32_t)numAnims);
    char nm[24] = {0}; std::strncpy(nm, block, 23); for (char c : nm) v.push_back((uint8_t)c);
    for (int i = 0; i < 40; ++i) v.push_back(0);
    return v;
}
static void test_ifp_probe() {
    IfpInfo i; std::string e;
    auto a = mkIfp(false, 7, "deagle"); CHECK(probeIfp(a.data(), a.size(), i, e) && i.blockName == "deagle" && i.numAnims == 7);
    auto b = mkIfp(true, 3, "blk");     CHECK(probeIfp(b.data(), b.size(), i, e) && i.blockName == "blk" && i.numAnims == 3);
    auto c = mkIfp(false, 9999, "x");   CHECK(!probeIfp(c.data(), c.size(), i, e));
    auto d = mkIfp(false, 0, "x");      CHECK(!probeIfp(d.data(), d.size(), i, e));
    std::vector<uint8_t> junk(64, 7);   CHECK(!probeIfp(junk.data(), junk.size(), i, e));
    auto t = mkIfp(false, 5, "q"); t.resize(10); CHECK(!probeIfp(t.data(), t.size(), i, e));
    auto m = mkIfp(true, 2, "z"); m[8] = 2; CHECK(!probeIfp(m.data(), m.size(), i, e));  // two blocks refused
    for (int k = 0; k < 200; ++k) { auto f = mkIfp(k & 1, 5, "q"); f.resize((size_t)k % f.size()); probeIfp(f.data(), f.size(), i, e); }  // truncation never crashes
    CHECK(true);
}

static std::shared_ptr<const Pcm> sine(int ch, int rate, int frames, float f, float amp) {
    auto p = std::make_shared<Pcm>(); p->channels = ch; p->rate = rate; p->samples.resize((size_t)frames * ch);
    for (int i = 0; i < frames; ++i) for (int c = 0; c < ch; ++c) p->samples[(size_t)i * ch + c] = amp * std::sin(2.0f * 3.14159265f * f * i / rate);
    return p;
}
static void test_mixer() {
    // 22.05 kHz mono sine played into a 44.1 kHz stereo bus: duration doubles, both channels equal, amplitude preserved
    std::vector<Voice> v; v.push_back({sine(1, 22050, 2205, 440.f, 0.5f), 0.0, 1.0f});
    std::vector<float> mix(2 * 4410, 0.f); mixVoices(v, mix.data(), 4410, 44100);
    float peak = 0; bool eq = true; for (int i = 0; i < 4400; ++i) { peak = std::max(peak, std::fabs(mix[2 * i])); if (mix[2 * i] != mix[2 * i + 1]) eq = false; }
    CHECK(eq); CHECK(peak > 0.49f && peak < 0.51f);
    CHECK(v.empty());                                                    // finished voice was dropped
    // resampled frequency is preserved: count zero crossings of the 440 Hz sine over 0.1 s -> ~ 2*440*0.1 = 88
    int zc = 0; for (int i = 1; i < 4400; ++i) if ((mix[2 * (i - 1)] < 0) != (mix[2 * i] < 0)) ++zc; CHECK(zc >= 86 && zc <= 90);
    // two voices add, gain applies, stereo keeps channels apart
    auto st = std::make_shared<Pcm>(); st->channels = 2; st->rate = 44100; st->samples.assign(2 * 100, 0.f); for (int i = 0; i < 100; ++i) { st->samples[2 * i] = 0.25f; st->samples[2 * i + 1] = -0.25f; }
    std::vector<Voice> w; w.push_back({st, 0.0, 2.0f}); w.push_back({st, 0.0, 1.0f});
    std::vector<float> m2(2 * 50, 0.f); mixVoices(w, m2.data(), 50, 44100);
    CHECK(std::fabs(m2[0] - 0.75f) < 1e-5f && std::fabs(m2[1] + 0.75f) < 1e-5f); CHECK(w.size() == 2);   // still playing
    CHECK(toPcm16(2.0f) == 32767 && toPcm16(-2.0f) == -32768 && toPcm16(0.f) == 0);                      // clipping is safe
    // 6-channel input does not read out of range
    auto six = sine(6, 44100, 200, 100.f, 0.2f); std::vector<Voice> x; x.push_back({six, 0.0, 1.f}); std::vector<float> m3(2 * 64, 0.f); mixVoices(x, m3.data(), 64, 44100); CHECK(true);
}

// Simulates the hooks' decision flow (WeaponForModel -> GiveWeapon from CPickup::Update -> occupy slot, save sidecar,
// new session reset, GenericLoad pre-apply / post-verify) using the SAME functions the plugin calls.
struct FakeGame {
    Registry reg; SlotState st; std::set<int> owned; const WeaponDef* pending = nullptr; std::map<int, const WeaponDef*> pickupDef;
    explicit FakeGame(Registry&& r) : reg(std::move(r)), st(reg) {}
    void own(int s) { owned.insert(s); }
    void hookWeaponForModel(int model) { auto it = pickupDef.find(model); pending = it == pickupDef.end() ? nullptr : it->second; }
    // returns true if the slot content changed
    bool giveFromPickup(int type) { auto* v = variantForPickup(st, reg, type, pending); pending = nullptr; if (!v) return false; return st.equip(*v).changed; }
    void saveTo(const std::string& path, const std::string& content) {
        { std::ofstream f(path, std::ios::binary); f << content; }
        SaveSidecar sc; sc.slotToWeapon = collectSaveMap(st, [&](int s) { return owned.count(s) > 0; }); sc.stamp = stampFile(path); writeSidecar(path, sc);
    }
    void newSession() { st.reset(); pending = nullptr; }                       // InitialiseWeapons hook
    bool load(const std::string& path, bool engineOk = true) {                 // GenericLoad hook
        std::string why; auto m = readSidecarFor(path, &why);
        if (m) for (const WeaponDef* d : planLoad(reg, *m).apply) st.equip(*d);
        for (int s : slotsToRevert(st, engineOk, [&](int x) { return owned.count(x) > 0; })) st.equip(reg.original(s));
        return engineOk;
    }
};
static FakeGame mkGame() {
    Registry r; Diags d;
    r.add(must(xml("desert_eagle", "pistol")), d); r.add(must(xml("colt_python", "pistol")), d); r.add(must(xml("m4", "ak47")), d);
    FakeGame g(std::move(r));
    g.pickupDef[5111] = g.reg.find("desert_eagle"); g.pickupDef[5222] = g.reg.find("colt_python"); g.pickupDef[5333] = g.reg.find("m4");
    return g;
}
static void test_scenarios() {
    namespace fs = std::filesystem; auto dir = fs::temp_directory_path() / "wl_scn"; fs::create_directories(dir); std::string save = (dir / "GTA3sf1.b").string();
    FakeGame g = mkGame(); g.own(S_PISTOL);
    CHECK(g.st.active(S_PISTOL).isOriginal);
    // 1. collect the Desert Eagle (real pickup path: custom model -> WeaponForModel -> GiveWeapon)
    g.hookWeaponForModel(5111); CHECK(g.giveFromPickup(S_PISTOL)); CHECK(g.st.active(S_PISTOL).id == "desert_eagle");
    // 2. save, "quit", start a new session (engine re-inits weapon.dat), load: the Deagle is still in the pistol slot
    g.saveTo(save, "SAVEDATA-1"); g.newSession(); CHECK(g.st.active(S_PISTOL).isOriginal);
    CHECK(g.load(save)); CHECK(g.st.active(S_PISTOL).id == "desert_eagle");
    // 3. collect the Colt Python: replaces the Deagle (never both)
    g.hookWeaponForModel(5222); CHECK(g.giveFromPickup(S_PISTOL)); CHECK(g.st.active(S_PISTOL).id == "colt_python");
    // 4. collect the ORIGINAL pistol model (not a custom pickup): replaces the Python
    g.hookWeaponForModel(173); CHECK(g.giveFromPickup(S_PISTOL)); CHECK(g.st.active(S_PISTOL).isOriginal);
    // 5. original pistol again while original active: nothing to do
    g.hookWeaponForModel(173); CHECK(!g.giveFromPickup(S_PISTOL));
    // 6. a stale pending from a previous pickup must not leak into an unrelated GiveWeapon
    g.hookWeaponForModel(5111); g.pending = nullptr; g.hookWeaponForModel(173); CHECK(!g.giveFromPickup(S_PISTOL)); CHECK(g.st.active(S_PISTOL).isOriginal);
    // 7. custom pickup whose slot differs from the GiveWeapon type is ignored (defensive)
    g.hookWeaponForModel(5111); CHECK(!g.giveFromPickup(S_SHOTGUN)); CHECK(g.st.active(S_PISTOL).isOriginal);
    // 8. slots 0 (fist) and 12 (detonator) never take variants
    g.hookWeaponForModel(5111); CHECK(!g.giveFromPickup(0)); g.hookWeaponForModel(5111); CHECK(!g.giveFromPickup(12));
    // 9. two slots at once survive a save/load round trip independently
    g.own(S_AK47); g.hookWeaponForModel(5111); g.giveFromPickup(S_PISTOL); g.hookWeaponForModel(5333); g.giveFromPickup(S_AK47);
    CHECK(g.st.active(S_PISTOL).id == "desert_eagle" && g.st.active(S_AK47).id == "m4" && g.st.active(S_SHOTGUN).isOriginal);
    g.saveTo(save, "SAVEDATA-2"); g.newSession(); g.load(save); CHECK(g.st.active(S_PISTOL).id == "desert_eagle" && g.st.active(S_AK47).id == "m4");
    // 10. a slot the player does not own is never recorded or restored
    FakeGame h = mkGame(); h.own(S_AK47); h.hookWeaponForModel(5111); h.giveFromPickup(S_PISTOL);   // pistol "equipped" but not owned
    h.saveTo(save, "SAVEDATA-3"); auto m = readSidecarFor(save); CHECK(m && m->count(S_PISTOL) == 0);
    // 11. sidecar says Deagle, but after the engine load the player no longer owns a pistol -> reverted
    FakeGame k = mkGame(); k.own(S_PISTOL); k.hookWeaponForModel(5111); k.giveFromPickup(S_PISTOL); k.saveTo(save, "SAVEDATA-4"); k.newSession(); k.owned.clear(); k.load(save);
    CHECK(k.st.active(S_PISTOL).isOriginal);
    // 12. failed engine load reverts everything
    FakeGame f = mkGame(); f.own(S_PISTOL); f.hookWeaponForModel(5111); f.giveFromPickup(S_PISTOL); f.saveTo(save, "SAVEDATA-5"); f.newSession(); CHECK(!f.load(save, false)); CHECK(f.st.active(S_PISTOL).isOriginal);
    // 13. save file replaced by a different one (e.g. restored from backup): stale sidecar ignored, original kept
    FakeGame s = mkGame(); s.own(S_PISTOL); s.hookWeaponForModel(5111); s.giveFromPickup(S_PISTOL); s.saveTo(save, "SAVEDATA-6"); { std::ofstream o(save, std::ios::binary); o << "DIFFERENT"; }
    s.newSession(); s.load(save); CHECK(s.st.active(S_PISTOL).isOriginal);
    // 14. original save (no sidecar at all) loads untouched
    fs::remove(save + ".wl"); FakeGame o = mkGame(); o.own(S_PISTOL); o.load(save); CHECK(o.st.active(S_PISTOL).isOriginal);
    // 15. weapon removed from the installed pack after saving: slot falls back to the original, no crash
    FakeGame q = mkGame(); q.own(S_PISTOL); q.hookWeaponForModel(5111); q.giveFromPickup(S_PISTOL); q.saveTo(save, "SAVEDATA-7");
    Registry smaller; Diags dd; smaller.add(must(xml("colt_python", "pistol")), dd); FakeGame q2(std::move(smaller)); q2.own(S_PISTOL); q2.load(save); CHECK(q2.st.active(S_PISTOL).isOriginal);
    fs::remove_all(dir);
}

int main(int argc, char** argv) {
    test_xml_parser(); test_pistol_replacement(); test_independent_slots(); test_rifles_share_slot();
    test_validation(); test_missing_files_degrade(); test_stats_resolve();
    test_spawns_and_anims(); test_save_sidecar(); test_model_ids(); test_resize(); test_ifp_probe(); test_mixer(); test_scenarios();
    if (argc > 1) test_pack_on_disk(argv[1]);
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
