# Vehicle tests to run in PCSX2

None of this has run in the game. The wire side is proven against a real server with a stand-in for
the game (`live_run.sh`). These steps are what's left: whether the addresses and opcodes in
`re-vehicle.md` do what they should. Run them in order, because a failure early on usually explains
later ones.

## Setup

1. Apply the integration: `patch -p1 --binary < wip/vehicle/integration.patch` from the repo root. If
   the payload moved on and a hunk fails, `python wip/vehicle/make_patch.py` rebuilds it first. The
   patch sets `FEAT_VEHICLE 1`.
2. Build and load the payload the usual way.
3. Start the private server. It stays clear of your 7777 server:
   `powershell -ExecutionPolicy Bypass -File wip/vehicle/srv_restart.ps1 ingame`
   If `wip/vehicle/srv/gamemodes/vehtest.amx` is out of date, `bash wip/vehicle/live_all.sh` rebuilds it.
4. In the game's menu join the address you use for 7777, with port **7793**.

Keys: F is Triangle (enter and exit), the Right arrow is D-pad right (enter as passenger), T is chat.
Server replies start `CB` and appear in chat. To read the client's log:
`python tools/pine.py` (the `veh:` lines). You spawn on the LS airport apron, around 1900, -2490.

When finished: `powershell -ExecutionPolicy Bypass -File wip/vehicle/srv_restart.ps1 stop`

## Tests

### 1. Cars appear

Spawn, then look around. Wait about 3 seconds.

**Expect:** within 45 m east of you an Infernus, a Linerunner, a trailer, a police car and an NRG, and
a Sultan just west of you with a paintjob, nitro and non-stock wheels. All sit on their wheels.
The log has `veh: 1 live, model 411 handle ...` lines. The Infernus faces west, the police car south.
The Infernus plate reads `PS2 CAR`.

**If not:** a hang at this point means `CREATE_CAR` or the pool address: check `gp` and the pool
pointers. Cars buried or floating: `vg_teleport` or the +0.25 lift. Wrong colours: `0229`. No plate:
PS2 may not render custom plates, which is worth knowing but harmless.

### 2. Getting in

Walk to the Infernus and press F.

**Expect:** the normal animation. As he starts, `CB enter 1 0`. Once seated, `CB state 2 from 1 seat 0
car 1`, and log `veh: in 1 seat 0`.

**If not:** no `CB enter` means `vg_local_entering` (tasks 700, 701 at ped+0x4B4). `CB enter` but no
state 2 means `vg_ped_car` or the sync. State flipping 1 and 2 means the `samp.c` hunk isn't in.

### 3. Driving

Drive west (W), brake, steer. Watch `CB update` every 700 ms.

**Expect:** position follows you. `angle` near 90 going west, 180 south, 270 east, 0 north. `health`
falls when you hit something. This is the first real test of the matrix to quaternion code.

**If not:** angles mirrored (270 going west) means the sign in `vm_mat_to_quat`.

### 4. Damage reports

Hit a wall hard enough to bend something.

**Expect:** `CB damage 1 <panels> <doors> <lights> <tyres>` within a second, values not all 0.

### 5. Server orders while driving

Type each in chat, while in the Infernus:

| Command | Expect |
|---|---|
| `/vvel` | the car jumps forward and up slightly |
| `/vspin` | the car starts to rotate |
| `/vhealth` | `CB update` shows health 500 |
| `/vrepair` | all dents and doors fixed, `CB update` health 1000 |
| `/vpos` | car teleports 50 m north, keeping heading, you stay in it |
| `/vengineoff` | engine stops, and comes back off if you get out and in again |

### 6. Getting out

Press F.

**Expect:** the normal animation, `CB exit 1` as it starts, then `CB state 1 from 2 seat -1 car 0`.
You can walk, and the server keeps seeing you move.

### 7. Orders on a car nearby

Stand by the Sultan and type:

| Command | Expect |
|---|---|
| `/vcolour` | colours change at once, to 44 and 55 |
| `/vmod` | nitro gone, hydraulics added. Check with L3 inside: the car bounces |
| `/vdamage` | left front wing bent, a door hanging, bonnet and one more door state, headlights out, two tyres flat |
| `/vparams` | engine on with lights off, alarm sounding, bonnet open |
| `/vangle` | the car turns to heading 123 |
| `/vinterior` | the car vanishes: it is in interior 3 now, which is correct |

**If not:** a crash on `/vdamage` is the first place to look at the node table in
`feat_vehicle_game.c`. Doors open on the wrong side: `vg_set_door` numbering.

### 8. Passenger

Stand by the police car and press the Right arrow.

**Expect:** he gets in the passenger side. `CB enter 4 1`, then `CB state 3 from 1 seat 1 car 4`. With
no driver the server also logs `CB unoccupied 4 seat 1 ...`. F gets you out.

### 9. Locked doors

Type `/vlock`, press F at the Infernus. **Expect:** he won't get in. `/vunlock`, F. **Expect:** he does.

### 10. Being moved by the server

| Command | Expect |
|---|---|
| `/vcar` | straight into the Infernus, camera behind you |
| `/vremove` | he climbs out, no `CB exit` |
| `/vbike` | onto the NRG |
| `/vputtruck` | into the Linerunner from anywhere, even from another car |
| `/vpassenger` | into its passenger seat |
| `/vfar` | out of the car and 1500 m east, over the sea |
| `/vback` | back on the apron, with every car back where it was left, dents included |

Log after `/vfar` should show no crash. Check `/vback` brings back any damage you did.

### 11. Trailer

`/vputtruck`, reverse to the trailer or type `/vattach`. Drive off.

**Expect:** the trailer hitches and follows. `CB trailer 3` repeats and `CB update` ends in `trailer 3`.
`/vdetach` drops it, and `trailer 0`.

### 12. Pushing an empty car

Get out and drive something into the Sultan, or walk into it hard.

**Expect:** `CB unoccupied 6 seat 0 at ...` while it moves, none once it stops.

### 13. Someone else driving

Run this once to build the bot: `bash wip/vehicle/live_all.sh`. Then the server needs restarting with
`srv_restart.ps1 ingame`, because that script leaves it on the gateway address. Join from the PS2 and
stand beside the Infernus, then: `tools/w wip/vehicle/bot.sh 7793`

The bot is a stand-in with no physics, so a few things look odd: at about 7 seconds it rises into the
air, because it obeys `/vvel` with no gravity. That's the bot, not a bug.

**Expect:** a ped walks to the Infernus and gets in, at about 2 to 4 seconds. The car then drives west
at a steady speed for 4 seconds, with the ped visible at the wheel, smoothly and with no stutter or
jumps. It jumps once at about 9 seconds (`/vpos`). Then the ped gets out. The Sultan moves north for
3 seconds at about 10 seconds. The bot then takes the truck, and hooks the trailer.

**If not:** the ped stays outside: `game_remote_hold` or `vg_warp_driver`, check the `game.c` hunks.
A car that shakes: raise `CATCH_UP`'s smoothing, or the driver's AI is fighting the writes, which
is the main unknown in this design. Note whether wheels turn: `re-vehicle.md` section 6 expects not.

### 14. Too many cars

Type `/vmany`, stand still and wait 15 seconds, then `/vclear`.

**Expect:** 40 cars are made on the server, but only the closest appear, and no more than 24 of the
total at once. Frame rate holds, apart from short pauses as each loads. Walk into the block: cars
behind you go, cars ahead appear. After `/vclear` they're all gone, the first six are unaffected.

**If not:** a crash or hang here with cars missing means memory, so lower `MAX_MODELS` in
`feat_vehicle.c` first and `MAX_LIVE` second.

### 15. Respawn and destruction

Type `/vrespawn`: the Infernus returns to its first place, as new. Shoot or crash a car until it
explodes. **Expect:** `CB death <id> by <you>`, then a new car 10 seconds later (`vehicle_respawn_time`).

### 16. Leaving

Disconnect from the menu. **Expect:** every server car disappears at once, and if you were driving
you are left standing. Reconnect: they all return.

## Please report

For each test, pass or what happened instead, the output of `python tools/pine.py` for any failure,
and two things this design is unsure about: how a remote car looks in motion (test 13), and whether
the frame rate suffers in test 14.
