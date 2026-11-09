#!/usr/bin/env python3
"""Checks the private server's log after run_live.sh: what the server's own callbacks and
getters say about the scripted PS2 client. check_live.py <path to log.txt>"""
import re
import sys

log = open(sys.argv[1], errors='replace').read()
# only the last run
log = log[log.rfind('Legacy Network started'):]
lines = [re.sub(r'^\[[^]]*\] \[Info\] ', '', l) for l in log.splitlines()]

WANT = [
    ('first spawn', r'CB spawn 0 count=1'),
    ('sprint key and forward stick', r'ST 0 state=1 keys=8 ud=-127 lr=0'),
    ('key change callback', r'CB keys 0 new=8 old=0'),
    ('aim key', r'CB keys 0 new=128 old=8'),
    ('aim camera mode and front vector', r'ST 0 .*keys=128 .*cam=53 front=1\.0'),
    ('fire key', r'CB keys 0 new=132 old=128'),
    ('server health and armour arrive', r'ST 0 state=1 .*hp=75 ar=25'),
    ('GivePlayerWeapon reaches the sync', r'ST 0 state=1 .*weapon=31 '),
    ('class weapon reaches 204', r'ST 0 .*slot2=24/100'),
    ('money through 205', r'ST 0 .*money=1234'),
    ('special action', r'ST 0 .*action=10 '),
    ('crouch', r'ST 0 .*action=1 anim=1273'),
    ('idle animation id', r'ST 0 .*anim=1188'),
    ('missed shot', r'CB shot 0 weapon=31 type=0 hit=65535'),
    ('fall damage', r'CB take 0 issuer=65535 amount=10\.00 weapon=54 part=3'),
    ('our hit, reported', r'CB give 0 damaged=1 amount=40\.00 weapon=31 part=3'),
    ('our hit, as a bullet', r'CB shot 0 weapon=31 type=1 hit=1'),
    ('other client takes it from the relayed bullet', r'CB take 1 issuer=0 amount=9\.8\d weapon=31 part=3'),
    ('incoming bullet', r'CB take 0 issuer=1 amount=46\.20 weapon=24 part=3'),
    ('death names killer and weapon', r'CB death 0 killer=1 reason=24'),
    ('respawn', r'CB spawn 0 count=2'),
    ('back on foot after respawn', r'ST 0 state=1 .*hp=75 ar=25'),
]

bad = 0
pos = 0
for name, pattern in WANT:
    if not any(re.search(pattern, l) for l in lines):
        print('MISSING %s: %s' % (name, pattern))
        bad += 1
# no foot sync while wasted: the state must hold at 7 between death and spawn
i = next((n for n, l in enumerate(lines) if 'CB death 0' in l), None)
j = next((n for n, l in enumerate(lines) if 'CB spawn 0 count=2' in l), None)
if i is not None and j is not None:
    for l in lines[i + 1:j]:
        if re.match(r'ST 0 state=(?!7 )', l):
            print('state changed while wasted: ' + l)
            bad += 1
            break
    takes = [l for l in lines[i + 1:j] if 'CB take 0' in l]
    if takes:
        print('damage taken while wasted: ' + takes[0])
        bad += 1
print('%d checks, %d bad' % (len(WANT) + 2, bad))
sys.exit(1 if bad else 0)
