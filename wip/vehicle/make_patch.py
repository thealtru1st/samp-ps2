#!/usr/bin/env python3
"""Rebuilds wip/vehicle/integration.patch from the current payload files. Run from the
repo root. The patch is what the final report lists, and live_run.sh and
check_integration.sh build with it applied, so those prove the patch itself."""
import os, shutil, subprocess
d = 'wip/vehicle/patched/'
os.makedirs(d, exist_ok=True)
SRC = {'samp.c': 'payload/samp.c', 'samp.h': 'payload/samp.h', 'main.c': 'payload/main.c',
       'game.c': 'payload/game.c', 'feat.c': 'payload/feat.c', 'hostmain.c': 'host/hostmain.c'}
for f, p in SRC.items():
    shutil.copy(p, d + f)

def sub(f, a, b):
    s = open(d + f, newline='').read()
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    assert s.count(a) >= 1, (f, a)
    s = s.replace(a, b, 1)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(d + f, 'w', newline='').write(s)

sub('samp.h', 'void samp_plat_local(samp_local_t *out);\n',
    'void samp_plat_local(samp_local_t *out);\nint  samp_plat_in_vehicle(void); /* true while vehicle sync replaces foot sync */\n')
sub('samp.c', 'now - last_sync >= SYNC_MS) {', 'now - last_sync >= SYNC_MS &&\n        !samp_plat_in_vehicle()) {')
sub('hostmain.c', 'void samp_plat_player_add(', 'int samp_plat_in_vehicle(void) { return 0; }\nvoid samp_plat_player_add(')

sub('main.c', '#include "feat.h"\n', '#include "feat.h"\n#include "feat_vehicle.h"\n')
sub('main.c', 'void samp_plat_local(samp_local_t *l) { game_read_local(l); }\n',
    'void samp_plat_local(samp_local_t *l) { game_read_local(l); }\nint  samp_plat_in_vehicle(void) { return feat_vehicle_in_car(); }\n')
sub('main.c', 'void samp_plat_player_sync(unsigned id, const float *p, const float *q, unsigned keys, int moving) { game_remote_sync(id, p, q, moving); }\n',
    'void samp_plat_player_sync(unsigned id, const float *p, const float *q, unsigned keys, int moving)\n{\n    feat_vehicle_foot_sync(id);\n    game_remote_sync(id, p, q, moving);\n}\n')
sub('main.c', 'void samp_plat_set_pos(const float *p) { game_teleport(p[0], p[1], p[2]); }\n',
    'void samp_plat_set_pos(const float *p)\n{\n    feat_vehicle_eject(p);\n    game_teleport(p[0], p[1], p[2]);\n}\n')
sub('main.c', '    started = 0;\n    game_remote_clear();\n    ui_status("");\n',
    '    started = 0;\n    feat_vehicle_reset();\n    game_remote_clear();\n    ui_status("");\n')
sub('main.c', '    teleported = 0;\n    game_remote_clear();\n', '    teleported = 0;\n    feat_vehicle_reset();\n    game_remote_clear();\n')
sub('main.c', '    samp_disconnect();\n    game_remote_clear();\n', '    samp_disconnect();\n    feat_vehicle_reset();\n    game_remote_clear();\n')

sub('game.c', '#define PED_MOVE_SPEED 0x040\n', '#define PED_MOVE_SPEED 0x044 /* 083C SET_CHAR_VELOCITY writes +0x44 */\n')
sub('game.c', '    int used, id, handle, moving;\n', '    int used, id, handle, moving, held;\n')
sub('game.c', '    r->moving = 0;\n    r->x = r->tx = p[0];\n', '    r->moving = 0;\n    r->held = 0;\n    r->x = r->tx = p[0];\n')
sub('game.c', """void game_remote_clear(void)
""", """/* While on, feat_vehicle.c has the ped: game_remote_update leaves it alone. */
void game_remote_hold(unsigned id, int on)
{
    remote_t *r = find_remote(id);
    if (r)
        r->held = on;
}

void game_remote_clear(void)
""")
sub('game.c', '        if (!r->used)\n            continue;\n        dx =', '        if (!r->used || r->held)\n            continue;\n        dx =')

# The wire quaternion is the conjugate of the plain one: see re-vehicle.md. Both ends together.
sub('game.c', """    if (uz - ay < 0.0f) q[1] = -q[1];
    if (ax - rz < 0.0f) q[2] = -q[2];
    if (ry - ux < 0.0f) q[3] = -q[3];
""", """    if (ay - uz < 0.0f) q[1] = -q[1];
    if (rz - ax < 0.0f) q[2] = -q[2];
    if (ux - ry < 0.0f) q[3] = -q[3];
""")
sub('game.c', """    /* forward vector of quaternion (w,x,y,z), then GTA heading in degrees */
    fx = 2.0f * (q[1] * q[2] - q[0] * q[3]);
""", """    /* forward vector of the wire quaternion, which is conjugated, then GTA heading in degrees */
    fx = 2.0f * (q[1] * q[2] + q[0] * q[3]);
""")

sub('feat.c', '#define FEAT_VEHICLE 0\n', '#define FEAT_VEHICLE 1\n')

# Bytes throughout: these sources are CRLF, and the patch has to match them. Apply with
# `patch -p1 --binary`.
with open('wip/vehicle/integration.patch', 'wb') as out:
    for f, p in SRC.items():
        r = subprocess.run(['diff', '-u', '--label', 'a/' + p, '--label', 'b/' + p, p, d + f],
                           capture_output=True)
        out.write(r.stdout)
