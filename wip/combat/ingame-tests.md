# Combat stream: tests to run in PCSX2

None of this has been run in the game. The host tests prove the logic and the bytes on the
wire against a real open.mp server, using a fake copy of game memory. They do not prove that
an opcode does what its name says on this binary. That is what this list is for.

Each test says what to do and what should happen. If a test crashes the game, note its number
and the last lines of the status log: `python tools/pine.py` or however you usually read it.
Log lines from this stream start with `combat:`.

## Setup

1. Apply `wip/combat/integration.patch` (`patch -p1 --binary` from the repo root, or by hand),
   which also sets `FEAT_COMBAT` to 1 in `payload/feat.c`. Build with `tools/w tools/build.sh`.
2. Start a server running the test gamemode. This must not be the shared one. Make your own
   copy of `tools/ompsrv-run/Server` without touching the original, put `wip/combat/combat.pwn`
   in `gamemodes/`, compile it with `qawno/pawncc.exe`, and in `config.json` set `network.port`
   to a free port, `network.bind` to the WSL gateway, `announce` false, `enable_query` false,
   `pawn.main_scripts` to `["combat 1"]` and `game.lag_compensation_mode` to 1.
3. Join with the PS2 first, so that it is player 0. The class has a Desert Eagle with 100 rounds.
4. For sections C and D, start the scripted player. It joins as player 1, spawns next to you
   and repeats a 30 second routine:
   `tools/w wip/combat/run_live.sh <port> actor 600`
   (run `tools/w wip/combat/run_tests.sh` once first: it builds what this needs)

The server log shows `ST 0 ...` every 400 ms with what the server believes about you, and
`CB ...` for each callback. Keep it open: `Get-Content log.txt -Wait`.

## A. Spawn and foot sync

| # | Do | Expected in game | Expected in the server log |
|---|---|---|---|
| A1 | Join | You spawn at 100, 200, 10 facing east with a Desert Eagle in slot 2, armed with fists, then M4 in hand. Health bar at 75, armour at 25, money 1234, three wanted stars, hands up. | `CB spawn 0 count=1`, then `ST 0 state=1 ... hp=75 ar=25 weapon=31 action=10`, `slot2=24/100` |
| A2 | Press Left on the D-pad or stick, hold, release | Moves as normal | `ST` line with `lr=-127` while held, 0 after. Up gives `ud=-127` |
| A3 | Press each of Cross, Square, Circle, Triangle, L1, R1, L2, R2, L3 | Normal game actions | `CB keys 0 new=` gives 8, 32, 4, 16, 1, 128, 256, 64, 2 for the buttons in that order |
| A4 | Push the stick gently | CJ walks | `keys` has 1024 |
| A5 | Move the right stick hard left | Camera turns | `keys` has 8192. This is the PC behaviour with a pad, not a bug. Mouse look on the right stick causes it too |
| A6 | Crouch with L3 | CJ crouches | `action=1 anim=1273` |
| A7 | Stand still, then run | | `anim=1188` idle, `anim=1223` running, `anim=1245` with Cross. Velocity needs the `PED_MOVE_SPEED` fix in the patch |
| A8 | From a PC client, or a second PS2 session, watch player 0 | Walks and runs with leg animation, not sliding | |

R3 runs the next numbered step of the test list in `combat.pwn`. Each press prints the step in
chat, as `1 shotgun, 30 shells, armed`, and in the log as `CB said`.

## B. Server control of the local player (R3 steps)

| Step | Expected |
|---|---|
| 1 | Shotgun in hand, ammo 30. `ST` shows `weapon=25` |
| 2 | Ammo counter drops to 5 |
| 3 | Fists in hand, shotgun still in the weapon cycle |
| 4 | No weapons at all. Cycling L2 and R2 finds nothing |
| 5 | Health bar drops to 30. No `CB take` line: the server's own change is not damage |
| 6 | Armour bar at 80 |
| 7 | Money shows 5000 more, and `ST` shows it within a second |
| 8 | Money 0 |
| 9 | Four gold stars under the HUD, top right. No police. The position and look are a first guess |
| 10 | Stars gone |
| 11 | CJ turns to face the south |
| 12 | CJ moves about 30 metres east and lands on the ground, not in the air or under it |
| 13 | CJ is thrown upwards. Then a fall: `CB take 0 issuer=65535 ... weapon=54` |
| 14 | Pad does nothing, `ST` shows `keys=0` |
| 15 | Pad back |
| 16 | Camera fixed nearby, looking at CJ |
| 17 | Camera glides for 3 seconds and stops |
| 18 | Camera back behind CJ |
| 19 | Moved into interior 3 with the interior visible. `CB interior` must not appear after this: the server already knows |
| 20 | Outside again, world visible |
| 21 | Night and rain. Also a check that `feat_world`'s clock holds it |
| 22 | Noon, clear |
| 23 | CJ dances in a loop. It takes a moment to load |
| 24 | Dancing stops at once |
| 25 | Nothing visible. The game must not freeze or crash: this is the check on bad animation names |
| 26 | Hands up |
| 27 | Hands down, and you can move |
| 28 | Jetpack. Least certain of the set: the model has to be on the disc as id 370 |
| 29 | Jetpack off. Punch with Circle: kung fu moves |
| 30 | Police skin |
| 31 | Dies, wasted screen. See section E |
| 32 | Dies again. On respawn you go through class selection automatically and land at the spawn |
| 33 | Camera follows the actor, CJ is hidden. `ST` shows state 9 |
| 34 | Back at the spawn with the camera behind you. `CB spawn` |

## C. Remote player: what you see of the actor

| # | Second of its routine | Expected |
|---|---|---|
| C1 | 0-5 | Stands still, Desert Eagle in hand, M4 once the server gives it |
| C2 | 5-10 | Walks |
| C3 | 10-15 | Sprints |
| C4 | 15-20 | Aims north with the gun raised |
| C5 | 20-25 | Fires north: muzzle flash, sound, tracers. Your health must not change from its stray shots unless step C6 happens |
| C6 | 20-25, stand within a few metres | Every third shot hits you: M4 does 9.9, armour first. `CB take 0 issuer=1 amount=9.90 weapon=31`. Health falls by that and no more |
| C7 | 25-27 | Crouches |
| C8 | 27 | Jumps |
| C9 | Any time, while it does nothing | It does not flee, cower or react when you fire near it. If it runs away, the decision maker (`060A`, `060B`) did not take |

## D. Your attacks on the remote player

| # | Do | Expected |
|---|---|---|
| D1 | Shoot the actor with the Desert Eagle or M4, using R1 lock-on | It does not die or fall, whatever you do. `CB give 0 damaged=1 amount=... weapon=31 part=3` and `CB shot 0 weapon=31 type=1 hit=1` |
| D2 | Fire at nothing | `CB shot 0 weapon=31 type=0 hit=65535` once per shot |
| D3 | Hold R1 near the actor | `ST` shows `cam=` something other than 4 if the game uses a weapon camera for lock-on, and `front` changes as you turn. Unknown which mode this binary uses: note it |
| D4 | Punch the actor | `CB give 0 damaged=1 ... weapon=0` |
| D5 | Fire a rocket at the actor from a distance (give yourself one with `GivePlayerWeapon` in the gamemode) | It stays up, with `CB give ... weapon=35` |
| D6 | Stand next to a real PC player, have them punch you | `CB take 0 issuer=... weapon=0`, about 1.3 per punch, only if they face you within 1.8 m |

## E. Death and respawn

| # | Do | Expected |
|---|---|---|
| E1 | Stand near the actor at seconds 20-25 with low health (step 5), let it kill you | Wasted screen as normal. Server: `CB death 0 killer=1 reason=31`. Log: `combat: dead, reason 31 killer 1`. `ST` holds at state 7 until the spawn, and no foot sync flips it |
| E2 | Wait | After about 3 seconds and a fade, you are at 100, 200, 10, not at a hospital. Health and spawn weapons restored, no money lost. `combat: respawned`, then `CB spawn 0 count=2`. The screen must fade back in: if it stays black, `SET_FADE_IN_AFTER_NEXT_DEATH`'s default is not what I read |
| E3 | Fall from a height until dead | `CB death 0 killer=65535 reason=54` |
| E4 | After respawning, look at the actor | Still there and moving. If it has gone, the restart removed it. Section F's check should bring it back, so note which one you saw |
| E5 | Die while the actor is streamed in, three times in a row | Same each time. The override is one-shot and is set again at each death |

## F. Stale remote handles

1. Get the actor's handle from the status log: `game: remote 1 streamed in, skin 7 handle N`.
2. While the actor is near you, delete its ped from outside: `python tools/scmcmd.py 009B i32:N`
3. Expected: the actor disappears and comes back within a frame or two at its last position,
   weapon restored. The log shows it streaming out and in. No crash, and no freeze.
4. Repeat while it is firing, at seconds 20-25.
5. Make it die in your game, which the server has not asked for:
   `python tools/scmcmd.py 0223 i32:N i8:0`
   Expected: the body is replaced by a live ped at once, and nothing reaches the server.

## G. Things to look out for at any point

- A hitch each time the actor changes weapon or you get a new one: `LOAD_ALL_MODELS_NOW` is
  synchronous, as it is in `game_remote_add`.
- The first remote ped streaming in also loads `m_empty.ped` from disc. A short hitch is
  expected once per session. A hang means that file read is not safe from our hook.
- After a respawn, shots from remote peds must still do nothing by themselves. The local ped is
  set bullet-proof at each spawn. If stray bullets start hurting, the flag did not stick.
