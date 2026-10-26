#!/usr/bin/env python3
"""Checks gta3.exe against every call site / prologue / table the loader patches or reads (see plugin/gta3_addrs.hpp).
usage: verify_exe.py path/to/gta3.exe      exit code 0 = everything matches GTA III 1.0"""
import struct, sys
exe = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', exe, 0x3c)[0]
nsec = struct.unpack_from('<H', exe, pe + 6)[0]; so = pe + 24 + struct.unpack_from('<H', exe, pe + 20)[0]
secs = [struct.unpack_from('<8sIIII', exe, so + i * 40) for i in range(nsec)]
def rd(ea, n):
    for nm, vs, va, rs, rp in secs:
        if 0x400000 + va <= ea < 0x400000 + va + rs: o = ea - 0x400000 - va; return exe[rp + o: rp + o + n]
    raise SystemExit('address %x not in file' % ea)
fails = 0
def check(name, ok, extra=''):
    global fails; print(('OK       ' if ok else 'MISMATCH ') + name + (' ' + extra if extra and not ok else '')); fails += (not ok)
ts = struct.unpack_from('<I', exe, pe + 8)[0]
check('PE TimeDateStamp 0x3CCECE14 (GTA III 1.0)', ts == 0x3CCECE14, hex(ts))
def call_target(site):
    b = rd(site, 5); return (site + 5 + struct.unpack('<i', b[1:])[0]) if b[0] == 0xE8 else None
for name, site, tgt in [('CWeaponInfo::Initialise call in InitialiseWeapons', 0x55C2D0, 0x564EA0), ('CWeapon::UpdateWeapons call in CGame::Process', 0x48C940, 0x55C310),
                        ('C_PcSave::SaveSlot call in menu', 0x485202, 0x591EC0), ('GenericLoad call in InitialiseWhenRestarting', 0x48C7CC, 0x590A00)]:
    check(name, call_target(site) == tgt, str(call_target(site)))
for name, ea, sig in [('CPed::GiveWeapon prologue', 0x4CF9B0, '53 56 55 8b 6c 24 10'), ('CPickups::WeaponForModel prologue', 0x4306F0, '0f b7 05 14 5b 5f 00'),
                      ('cDMAudio::PlayOneShot prologue', 0x57C840, '83 ec 08 8b 54 24 10'), ('CWeapon::Fire prologue', 0x55C380, '53 56 57 55 83 ec 28'),
                      ('CWeaponInfo::GetWeaponInfo (aWeaponInfo=0x6503EC, stride 0x54)', 0x564FD0, '8b 44 24 04 6b c0 54 05')]:
    check(name, rd(ea, len(bytes.fromhex(sig))) == bytes.fromhex(sig), rd(ea, len(bytes.fromhex(sig))).hex(' '))
def cs(a): return rd(a, 24).split(b'\0')[0].decode('latin1')
names = [cs(struct.unpack('<I', rd(0x603334 + 4 * i, 4))[0]) for i in range(13)]
check('eWeaponType names @0x603334', names == ['Unarmed','BaseballBat','Colt45','Uzi','Shotgun','AK47','M16','SniperRifle','RocketLauncher','FlameThrower','Molotov','Grenade','Detonator'], str(names))
d0 = struct.unpack('<6I', rd(0x5EB724, 0x18))
check('std animation group def[0] = man/ped, model 1, 173 anims', (cs(d0[0]), cs(d0[1]), d0[2], d0[3]) == ('man', 'ped', 1, 173), str(d0))
print('\nALL OK - this executable matches what the loader expects.' if not fails else '\n%d MISMATCH(ES) - do not use the loader with this executable.' % fails)
sys.exit(1 if fails else 0)
