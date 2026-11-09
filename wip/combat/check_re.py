#!/usr/bin/env python3
"""Reads each instruction the combat code rests on straight out of the ELF, so an address
or offset in payload/feat_combat_game.c cannot drift from what was read. This is the
evidence list: each row is one claim. Run from the repo root:
    python wip/combat/check_re.py
It says nothing about behaviour at runtime."""
import struct
import sys

BASE, OFF = 0x100000, 0x180
elf = open('private/SLES_529.27', 'rb').read()


def word(a):
    return struct.unpack_from('<I', elf, a - BASE + OFF)[0]


CLAIMS = [
    # gp, from crt0: lui a0,0x67; addiu a0,a0,-20112; move gp,a0 gives 0x66B170
    (0x100198, 0x3C040067, 'gp high'),
    (0x1001AC, 0x2484B170, 'gp low'),
    (0x1001C0, 0x0080E025, 'gp set'),
    # CPool: objects +0, flags +4, size +8, handle (slot<<8)|flag, ped stride 0x810
    (0x1FFBB0, 0x8C820004, 'GetAt reads flags at +4'),
    (0x1FFBB4, 0x00053203, 'GetAt: slot = handle >> 8'),
    (0x1FFBB8, 0x30A300FF, 'GetAt: flag = handle & 0xFF'),
    (0x1FFBCC, 0x8C820000, 'GetAt reads objects at +0'),
    (0x1FFBD0, 0x000619C0, 'GetAt: slot << 7'),
    (0x1FFBD8, 0x00031900, 'GetAt: (slot*129) << 4 = slot*0x810'),
    (0x13988C, 0x8E320008, 'ped pool loop reads size at +8'),
    (0x1FF294, 0x8F840828, 'IS_CHAR_DEAD: ped pool at gp+0x828'),
    # ped fields
    (0x1FF2A0, 0x8C430568, 'IS_CHAR_DEAD: state at +0x568'),
    (0x1FF2A4, 0x2462FFCA, 'IS_CHAR_DEAD: state - 54'),
    (0x1FF2B4, 0x24020038, 'IS_CHAR_DEAD: or state 56'),
    (0x5047DC, 0xE4400044, 'SET_CHAR_VELOCITY: move speed x at +0x44, not 0x40'),
    (0x5047E4, 0xE4400048, 'SET_CHAR_VELOCITY: y at +0x48'),
    (0x5047EC, 0xE440004C, 'SET_CHAR_VELOCITY: z at +0x4C'),
    (0x30DB28, 0x90450042, 'SET_CHAR_PROOFS: flags, so +0x40 is no speed'),
    (0x3B90A8, 0x904204A7, 'IS_CHAR_DUCKING: byte +0x4A7'),
    (0x3B90AC, 0x0002177C, 'IS_CHAR_DUCKING: bit 2'),
    (0x30F768, 0x904204A6, 'IS_CHAR_SHOOTING: byte +0x4A6'),
    (0x331DB8, 0x80460758, 'GET_CURRENT_CHAR_WEAPON: slot at +0x758, GetAt result unchecked'),
    (0x331DD8, 0x8C4205E0, 'GET_CURRENT_CHAR_WEAPON: type at +0x5E0 + slot*0x1C'),
    (0x330924, 0x8C4305EC, 'GET_AMMO_IN_CHAR_WEAPON: total ammo at weapon +0xC'),
    (0x33093C, 0x2442001C, 'GET_AMMO_IN_CHAR_WEAPON: stride 0x1C'),
    (0x330934, 0x28A3000D, 'GET_AMMO_IN_CHAR_WEAPON: 13 slots'),
    (0x310158, 0x826207A0, 'HAS_CHAR_BEEN_DAMAGED_BY_WEAPON: last weapon +0x7A0, signed'),
    (0x331BD8, 0xA20207A0, 'CLEAR_CHAR_LAST_WEAPON_DAMAGE: stores -1 there'),
    (0x3A843C, 0x8E2407A4, 'HAS_CHAR_BEEN_DAMAGED_BY_CHAR: source at +0x7A4'),
    (0x2A1EE0, 0xC6210580, 'restart reads health at +0x580'),
    (0x2A1F44, 0xAE200588, 'restart clears armour at +0x588'),
    (0x1FD38C, 0x904204A5, 'in-car flag +0x4A5'),
    (0x1FD3A0, 0x8E5105CC, 'vehicle at +0x5CC'),
    # DELETE_CHAR takes a null ped
    (0x30B2C0, 0x1220003D, 'DELETE_CHAR body: beqz ped'),
    # health 0 raises the die event, any other value is a plain store
    (0x30C914, 0x14400020, 'SET_CHAR_HEALTH: bnez value skips the die event'),
    # camera, TheCamera at 0x6FE5B0
    (0x49D2E4, 0x9026E609, 'active cam at +0x59'),
    (0x49D2EC, 0x2442E5B0, 'TheCamera low half'),
    (0x49D310, 0x8C620310, 'Source at +0x310 + active*0x238'),
    (0x49D360, 0x2529E8C0, 'GET_ACTIVE_CAMERA_POINT_AT: Source, 0x6FE8C0'),
    (0x49D364, 0x2508E8B4, 'GET_ACTIVE_CAMERA_POINT_AT: Front, 0x6FE8B4'),
    (0x134CC8, 0x8423E730, 'Mode of Cams[0], short at 0x6FE730 = +0x180'),
    # pad
    (0x2491D0, 0x9483006A, 'walk left/right getter: reads the pad mode'),
    (0x249208, 0x84820000, 'walk left/right: left stick x'),
    (0x249210, 0x84830014, 'walk left/right: D-pad left'),
    (0x249300, 0x9483006A, 'walk up/down getter: reads the pad mode'),
    (0x249340, 0x84830010, 'walk up/down: D-pad up'),
    # the game's restart
    (0x2A05A8, 0x93840AB4, 'CGameLogic::Update: state at gp+0xAB4'),
    (0x2A07B0, 0xA3830AB4, 'CGameLogic::Update: 1 on wasted'),
    (0x2A0984, 0xA3800AB4, 'CGameLogic::Update: 0 on restart'),
    (0x2A0900, 0x2E010BB9, 'CGameLogic::Update: restart after 3000 ms'),
    (0x29FBC8, 0x93830A8C, 'FindClosestHospitalRestartPoint: override flag'),
    (0x29FC04, 0xA3800A8C, 'and it is one-shot'),
    (0x524BA4, 0xA382BBA8, 'SWITCH_DEATH_PENALTIES stores the flag'),
    (0x2A0928, 0x9382BBA8, 'that CGameLogic::Update tests'),
    (0x1FEFDC, 0x8CA400E4, 'ADD_SCORE: money at player +0xE4'),
    # animation tables
    (0x1079F8, 0x3C100067, 'block table high'),
    (0x1079FC, 0x26107E30, 'block table 0x677E30'),
    (0x1079E8, 0x8F8200B8, 'block count at gp+0xB8'),
    (0x107A38, 0x26100020, 'stride 0x20'),
    (0x107A9C, 0x0C13588C, 'GetAnimation hashes the name'),
    (0x107AC0, 0x8CA70014, 'then scans from block +0x14'),
    (0x3A73AC, 0x90430010, 'HAS_ANIMATION_LOADED: byte +0x10'),
    (0x5006E8, 0x24060018, 'TASK_PLAY_ANIM: 24-byte name buffer'),
    (0x5006F8, 0x24060010, 'TASK_PLAY_ANIM: 16-byte file buffer'),
    # models
    (0x331FF4, 0x3C02007B, 'IS_MODEL_AVAILABLE high'),
    (0x331FFC, 0x24428800, 'IS_MODEL_AVAILABLE: table 0x7A8800'),
    (0x501D40, 0x0C0B0520, 'IS_MODEL_IN_CDIMAGE calls 0x2C1480'),
    (0x4D4A90, 0x8C42000C, 'GET_WEAPONTYPE_MODEL: info +0xC'),
    # script thread: the compare flag and mission flag fit in scm.c's 0x100 bytes
    (0x3078B8, 0xA08500C5, 'compare flag at thread +0xC5'),
    (0x1FD344, 0x920200C6, 'mission flag at thread +0xC6'),
]

bad = 0
for addr, want, what in CLAIMS:
    got = word(addr)
    if got != want:
        print('BAD %06X: %08X, expected %08X: %s' % (addr, got, want, what))
        bad += 1
if elf[0x625670 - BASE + OFF:][:11] != b'm_empty.ped':
    print('BAD: decision maker table')
    bad += 1
print('%d claims, %d bad' % (len(CLAIMS) + 1, bad))
sys.exit(1 if bad else 0)
