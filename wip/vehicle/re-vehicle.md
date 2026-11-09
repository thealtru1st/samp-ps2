# Vehicle reverse engineering

Binary: `private/SLES_529.27` (SLES-52927 v1.00). Static analysis only: **none of this has run in
the game yet.** `opmap.py` here decodes the opcode dispatch so any handler can be found:
`python wip/vehicle/opmap.py 0227 00A5`. The 27 group handlers are listed at 0x5F9238, one every 16
bytes with the function at +8. Each is a `li v0,N; beq a1,v0,case` chain or a jump table.

Confidence: **High** is read in the code with a second site agreeing. **Medium** fits the code but
part of it is inferred, usually from the PC layout.

## 1. Things that differ from what we assumed

1. **Ped move speed is +0x44, not +0x40.** `083C SET_CHAR_VELOCITY` (0x504788) stores to 68(v0). The
   CPhysical block is the same as PC: move speed +0x44, turn speed +0x50, mass +0x8C (0x49D748).
   `game.c` reads +0x40 today, so its velocity is shifted by one float. Fixed in `integration.patch`.
2. **Quaternions on the wire are the conjugate of what `game.c` builds.** Read in open.mp (`GTAQuat`
   negates Euler angles, `Components/NPCs/NPC/npc.cpp:525`) and confirmed live: quaternion
   (0.707, 0, 0, -0.707) gives `GetVehicleZAngle` 90.00 and `GetPlayerFacingAngle` 90.00. `mat_to_quat`
   and `game_remote_sync` in `game.c` both have the other sign, so they agree with each other but not
   with a PC client. Fixed in `integration.patch`.
3. **The vehicle pool has 70 slots** (PC 110) and the ped pool 74 (PC 140): `li a1,70` at 0x266050.
4. **Opcode handlers trust their handle.** `GET_CAR_HEALTH` loads through whatever the pool returned.
   A stale handle dereferences 0, so `feat_vehicle_game.c` validates every handle first.
5. **`SET_CAR_COORDINATES` and the NO_OFFSET form both call PlaceOnRoadProperly** for cars (0x1C51F0,
   0x1C5200) and `CREATE_CAR` does too outside a mission thread (0x1C4ED0). Neither is usable for
   exact placement or per-frame sync. We call the car's Teleport virtual directly instead.
6. **`POP_CAR_PANEL`/`FIX_CAR_PANEL` must not get panels 2 or 3.** The panel-to-node table at 0x162E50
   gives -1 for the rear wings and both functions then index the node array with it.

## 2. Globals

| What | Address | Evidence | Conf. |
|---|---|---|---|
| `gp` | 0x66B170 | 0x100198 sets a0, 0x1001C0 `move gp,a0`; `tools/state.py` uses the same | High |
| Ped pool pointer | gp+0x828 | written at 0x266038, read by every char opcode | High |
| Vehicle pool pointer | gp+0x82C | written at 0x26605C, read by every car opcode | High |
| CModelInfo table | 0x7A8800 | 0x49C494, 0x176E54 | High |
| CStreamingInfo load state | 0x7C3CF6, stride 12, low 3 bits, 1 = loaded | HAS_MODEL_LOADED 0x30D020 | High |
| Script params | 0x6FE030 | CollectParameters 0x1F77D0, StoreParameters 0x1F7D20 | High |

Pool layout from the constructor at 0x266A10: objects +0, flags +4, size +8, top +12. Handle is
`index << 8 | flag byte`, and `GetAt` (0x1FFB70, 0x1FFBB0) compares the flag byte. Vehicle stride is
0xA80 and ped stride 0x810, from the multiplies in those two functions.

## 3. CVehicle

Up to about 0x500 the layout is PC + 8. Further on it is PC + 0x3C.

| Field | Offset | Evidence | Conf. |
|---|---|---|---|
| Matrix pointer | +0x14 | 0x1FDAC4; position at matrix+0x30, else +4 | High |
| Flags, bit 2 static | +0x1C | 0x501C50 tests it before waking | High |
| Model | +0x22 (s16) | GET_CAR_MODEL 0x330E50 | High |
| Area | +0x2F | 0840 at 0x504A08 | High |
| Status (top 5 bits) | +0x36 | IS_CAR_DEAD 0x1FF320: 5 is wrecked; 4 is set by create | High |
| Move speed, turn speed | +0x44, +0x50 | 06A2 at 0x49D6A8; 083C; PC layout for turn | High, Medium |
| Handling pointer | +0x38C | 0x156BAC | High |
| Engine on | +0x430 bit 4 | SET_CAR_ENGINE_ON 0x525DC8 | High |
| Drowned | +0x433 bit 6 | IS_CAR_DEAD 0x1FF340 | Medium |
| Engine broken, siren | +0x435 bits 1, 7 | 0x525DD4, SWITCH_CAR_SIREN 0x313E2C | High |
| Colours | +0x43C, +0x43D | CHANGE_CAR_COLOUR 0x30CB20 | High |
| Alarm (u16) | +0x464 | PC +8; lhu sites at 0x14C628, 0x14EB58 | Medium |
| Driver | +0x468 | 0x30B2EC, 0x49D13C | High |
| Passengers | +0x46C, array | 0432 at 0x330D08 | High |
| Seats (u8) | +0x490 | 0x330CA4 | High |
| Health (float) | +0x4C8 | GET_CAR_HEALTH 0x30CAD8, SET 0x30CA74 | High |
| Lights override | +0x4B0 bits 3-4 | FORCE_CAR_LIGHTS 0x49C9C4 | High |
| Towing cab | +0x4CC | IS_TRAILER_ATTACHED 0x4D557C | High |
| Towed trailer | +0x4D0 | 0x4D55A8, 0x4D5640 | High |
| Door lock | +0x500 | LOCK_CAR_DOORS 0x30C52C | High |
| Class | +0x5CC | 0 car, 5 boat, 9 bike, 11 trailer: 0x1C514C, 0x1C51C4 | High |
| Subclass | +0x5D0 | 3, 4, 6 tested at 0x1C5038 | Medium |
| CDamageManager | +0x5E0 | 0x156E70 | High |
| Car nodes (RwFrame*) | +0x688 | 0x156EEC | High |
| Bike wheels (u8 x2) | +0x698 | 0x3324A8 | High |

CDamageManager is the PC layout: wheels +5, doors +9 (6 bytes), lights +16 (2 bits each), panels +20
(4 bits each). Accessors at 0x162520-0x1626AC. Wire format is the game's raw panels, the 4 low door
bytes (bonnet, boot, front left, front right) and the low byte of lights. Tyre bits are reversed:
bit 3 is wheel 0, or for bikes bit 1 is wheel 0. That mapping comes from the reference client.

Virtuals, offsets into the vtable, in PC order with a two-word header: dtor +8, SetIsStatic +24,
Teleport +64 (`this, CVector*, reset rotation`), BurstTyre +184, Fix +208, SetTowLink +252, BreakTowLink
+256. All-int arguments, so callable from N32.

## 4. CPed

| Field | Offset | Evidence | Conf. |
|---|---|---|---|
| In a vehicle | +0x4A5 bit 0 | 0x5035F4 | High |
| Intelligence | +0x4B4 | 0x503618; task manager is at +4 | High |
| Vehicle | +0x5CC | 0x503604 | High |

`GET_CAR_CHAR_IS_USING` (0x5035D8) shows how to see a ped on his way to a car:
`FindActiveTaskByType` 0x45FBC0 with 701 (as driver) then 700 (as passenger), vehicle at task +12.
704 is LEAVE_CAR: `GetTaskType` at 0x433A90 returns it for the task built in 0x42C5F0.

## 5. Opcodes checked against their handlers

| Op | Handler | Finding |
|---|---|---|
| 00A5 CREATE_CAR | 0x1FD740 | Calls 0x1C4C00. Needs the model loaded and a free slot: nothing checks either |
| 00A6 DELETE_CAR | 0x1FD7A8 | Safe on 0. CWorld::Remove, remove references, delete. Does not deal with occupants |
| 009B DELETE_CHAR | 0x1FD320 | 0x30B2B0 takes the ped out of its car first, so this is safe in a car |
| 00A1 SET_CHAR_COORDINATES | 0x1FD468 | In a car it moves the car (0x1FF6B8). SA-MP's SetPlayerPos must eject first |
| 0173 SET_CHAR_HEADING | 0x317F18 | Skipped in a car (0x317FB0) |
| 0175 SET_CAR_HEADING | 0x318140 | Fine |
| 036A WARP_CHAR_INTO_CAR | 0x312E40 | Flushes tasks, runs CTaskSimpleCarSetPedInAsDriver with the warp flag |
| 0430 AS_PASSENGER | 0x330BF0 | Seat index from 0, -1 for any |
| 0362 WARP_FROM_CAR_TO_COORD | 0x3129C0 | Flushes tasks, which takes the ped out, then teleports |
| 05CA, 05CB ENTER | 0x3BAB60, 0x3BAC80 | Time -2 is no timeout, -1 the default |
| 05CD, 0633 LEAVE | 0x3BAD70, 0x3BD6D8 | As PC |
| 0674 CUSTOM_PLATE | 0x49C430 | Only if the model is loaded. Applies to the next car of that model. `_` and NUL become spaces |
| 06E7, 06E8 MODS | 0x49F570, 0x49F5E0 | 3 and 2 arguments. AddVehicleUpgrade reads CModelInfo unchecked |
| 06E9-06EB MOD streaming | 0x49F640... | Pairs of left and right parts load together: use `HasVehicleUpgradeLoaded` 0x2C1E10 |
| 06ED PAINTJOB | 0x49F7C0 | 0x167CE0, -1 removes |
| 0893, 07AC TRAILER | 0x506308, 0x4D55C8 | First argument is the trailer, second the cab |
| 08A6 OPEN_DOOR_A_BIT | 0x523FF0 | Float 0 to 1. 0657 is the same at 1.0. Cars only: goes to CAutomobile door code |
| 0918 ENGINE | 0x525DB0 | Does nothing while the engine is broken |
| 095E CONTROL_CAR_DOOR | 0x527370 | Its angle is an **int** here, unlike the sa.json type. Not used |
| 099A SET_CAR_COLLISION | 0x5288C8 | Sets +0x1C bit 0 |
| 0A30 FIX_CAR | 0x59C140 | Exists on PS2. Fix virtual, then health 1000 |

`0247 REQUEST_MODEL` adds to a 75-entry resource table keyed by script (0x500B90). It dedupes and
ignores overflow, so it can't fail, and `0249` undoes it. Requesting asynchronously works: loading
happens in `CStreaming::Update`, so `feat_vehicle.c` polls instead of using `038B`, which stalls the
frame.

## 6. Not done

- **Trains.** `06D8` needs all five train models at once and the train runs on rails under its own
  control, so pushing a position at it does nothing useful. Models 449, 537, 538, 569, 570 and 590 get
  a record but no car.
- **Landing gear and Hydra thrust.** Sent as zero. On remote planes they are ignored.
- **Windows** (params) and **drive-by.** No opcode. Window params are ignored.
- **Heli rotors, tank turrets.** Need a driver on status PLAYER, as on PC.
- **Wheel spin and steering on remote cars.** A remote car is on STATUS_PHYSICS with an idle
  autopilot, which holds the brake. Writing steer and gas pedal needs to happen after that AI, and we
  run before it.
