# SA-MP 0.3.7 message coverage (milestone 8)

Decoders and encoders are in `samp_rpc.c`. This file says what each message is, how
much it matters and how the PS2 side can act on it. Nothing here is implemented in
the game yet; the decoder calls a handler slot and the slot is empty until `game.c`
or `ui.c` fills it.

Wire layouts: `ref/openmp-src/Shared/NetCode/*.hpp` (MPL-2.0), `isDL` false. Huffman
frequency table: `ref/openmp-network/Encoding/str_compress.cpp`. IDs that open.mp
never writes were cross-checked against the register list in `ref/samp-source`, which
was read and not copied.

## How to read the tables

**Test**: `L` decoded from a real open.mp 1.6.7 server using exactly the bits it
sent, with the values read against what `rpctest.pwn` asked for. `U` unit test only.
Every row is also unit tested. No row has been tested against a stock samp-server.

Most `L` rows come from a private copy of the server running `rpctest.pwn`, because
the shared server's gungame mode only sends about 20 of these. To repeat that: copy
`omp-server.exe` and `components/` somewhere of your own, set `network.port`,
`network.bind`, `announce` false, `enable_query` false, `pawn.main_scripts`
`["rpctest 1"]`, compile `rpctest.pwn` with `qawno/pawncc.exe`, restart it for each
run (`/t9` and SetWorldTime leave state behind), then `run_script.sh <port>` and
`live_check.py`.

**Pri**: H needed by most real servers, M common, L rare.

**PS2**: `SCM` gives opcodes from `ref/scm/sa.json` (`check_coverage.py` checks every
one exists, is not marked unsupported or nop, and has that name). Opcodes are
assumed to behave as on PC, which the project has confirmed for the ones in
`game.c` only. `UI` means `ui.c` draws it or a client table holds it, no game action.
`Client` means logic in our client. `RE` means no opcode can do it and an address
must be found in the ELF. `No` means it cannot work on PS2, with the reason.

Three things apply to many rows and are not repeated:

- **Strings in SCM.** `scm.c` has no string argument. Opcodes that take text or an
  animation name need `scm_str()` added; see the end of this file.
- **GXT keys.** Print and menu opcodes (`00BA`, `00BC`, `03E5`, `08D4`, `08DB`) take
  keys into the game's text table, not text. Server strings are arbitrary, so these
  paths are `UI` or `RE`, never those opcodes as they stand.
- **Models.** Before `CREATE_*`: `0247 REQUEST_MODEL`, `038B LOAD_ALL_MODELS_NOW`.
  SA-MP's added objects (18631 and up) are not on the PS2 disc, so those models
  need a fallback or a skip.

## Server to client: RPCs

### Connection and players

| ID | Slot | Fields | Pri | Test | PS2 |
|---|---|---|---|---|---|
| 139 | init_game | flags, player id, marker mode, hour, weather, gravity, sync rates, server name, vehicle model list | H | L | Client. Rates must replace `SYNC_MS`. SCM `00C0 SET_TIME_OF_DAY`, `01B6 FORCE_WEATHER_NOW`, `08E7 DISABLE_ALL_ENTRY_EXITS`. Gravity: see 146 |
| 128 | request_class_reply | selectable, team, skin, spawn, angle, 3 weapons | H | L | Client; `samp.c` handles part of it |
| 129 | request_spawn_reply | allow (0 no, 1 yes, 2 spawn now) | H | L | Client. `samp.c` treats any non-zero as yes; 2 should skip the wait |
| 68 | set_spawn_info | team, skin, spawn, angle, 3 weapons | H | L | Client: store for the next spawn |
| 74 | force_class_selection | none | M | L | Client: go back to class selection at next death |
| 130 | connection_rejected | reason byte | M | U | UI: show why. Stock only |
| 40 | game_mode_restart | none | H | L | Client: free every pool below, then wait for InitGame again |
| 137 | player_join | id, colour, npc, name | H | L | UI: player table |
| 138 | player_quit | id, reason | H | L | UI, then the 163 path if streamed |
| 32 | player_stream_in | id, team, skin, pos, angle, colour, fight style, 11 skills | H | L | SCM `009A CREATE_CHAR`, `0173 SET_CHAR_HEADING`. Done in `game_remote_add` |
| 163 | player_stream_out | id | H | L | SCM `009B DELETE_CHAR`. Done in `game_remote_del` |
| 166 | player_death | id | H | L | SCM `05BE TASK_DIE` on the remote ped |
| 11 | set_player_name | id, name, success | M | L | UI |
| 72 | set_color | id, RGBA | H | L | UI: nametags, chat, markers |
| 69 | set_team | id, team | M | L | Client: friendly-fire checks before sending 115 |
| 80 | show_nametag | id, show | M | L | UI |
| 155 | score_and_ping | id, score, ping, per player | M | L | UI: scoreboard. Only sent in reply to our 155 |

### Local player

| ID | Slot | Fields | Pri | Test | PS2 |
|---|---|---|---|---|---|
| 12 | set_pos | pos | H | L | SCM `04E4 REQUEST_COLLISION`, `03CB LOAD_SCENE`, `00A1 SET_CHAR_COORDINATES`. Done in `game_teleport`; in a car use `00AB SET_CAR_COORDINATES` |
| 13 | set_pos_find_z | pos | M | L | SCM `02CE GET_GROUND_Z_FOR_3D_COORD`, then as 12 |
| 14 | set_health | float | H | L | SCM `0223 SET_CHAR_HEALTH` |
| 66 | set_armour | float | H | L | SCM `04DD GET_CHAR_ARMOUR` then `035F ADD_ARMOUR_TO_CHAR` with the difference, which is only additive. Or store at ped+0x588, already known in `game.c` |
| 15 | toggle_controllable | u8 | H | L | SCM `01B4 SET_PLAYER_CONTROL` |
| 19 | set_facing_angle | float | H | L | SCM `0173 SET_CHAR_HEADING` |
| 90 | set_velocity | vec3 | M | L | SCM `083C SET_CHAR_VELOCITY` |
| 18 | give_money | i32 | H | L | SCM `0109 ADD_SCORE` |
| 20 | reset_money | none | H | L | SCM `010B STORE_SCORE`, then `0109 ADD_SCORE` with the negative |
| 21 | reset_weapons | none | H | L | SCM `048F REMOVE_ALL_CHAR_WEAPONS` |
| 22 | give_weapon | weapon, ammo | H | L | SCM `0781 GET_WEAPONTYPE_MODEL`, load it, `01B2 GIVE_WEAPON_TO_CHAR` |
| 67 | set_armed_weapon | weapon | M | L | SCM `01B9 SET_CURRENT_CHAR_WEAPON` |
| 145 | set_ammo | weapon, ammo | M | L | SCM `017B SET_CHAR_AMMO` |
| 153 | set_skin | id, skin | H | L | Local: SCM `09C7 SET_PLAYER_MODEL` (in `game_set_skin`). Remote: delete and recreate. Skins 300-311 are not on PS2 |
| 34 | set_skill_level | id, skill, level | L | L | Local: SCM `062A SET_FLOAT_STAT`, stats 69 up. Remote: `081A SET_CHAR_WEAPON_SKILL` is coarse |
| 89 | set_fighting_style | id, style | L | L | SCM `07FE GIVE_MELEE_ATTACK_TO_CHAR` |
| 88 | set_special_action | action | M | L | Per action: SCM `07A7 TASK_JETPACK`, `04EB TASK_TOGGLE_DUCK`, `05C4 TASK_HANDS_UP`, or 86's path for dances. The 0.3.7 action numbering must be mapped by hand |
| 86 | apply_animation | id, lib, name, delta, 4 flags, time | H | L | SCM `04ED REQUEST_ANIMATION`, `04EE HAS_ANIMATION_LOADED`, `0812 TASK_PLAY_ANIM_NON_INTERRUPTABLE`. Loading takes frames, so the task must be queued |
| 87 | clear_animations | id | H | L | SCM `0792 CLEAR_CHAR_TASKS_IMMEDIATELY` |
| 133 | set_wanted_level | level | M | L | SCM `01F0 SET_MAX_WANTED_LEVEL`, `010D ALTER_WANTED_LEVEL`. `game_boot` sets the maximum to 0, and a real wanted level sends police, so showing stars without cops may need RE |
| 35 | set_drunk_level | i32 | L | L | SCM `052C SET_PLAYER_DRUNKENNESS` |
| 92 | set_drunk_visuals | i32 | L | U | SCM `052C SET_PLAYER_DRUNKENNESS`. Stock only; format from the 0.2.5-era handler |
| 150 | set_drunk_handling | i32 | L | U | SCM `03FD SET_DRUNK_INPUT_DELAY`. Stock only; format from the 0.2.5-era handler |
| 156 | set_interior | u8 | H | L | SCM `04BB SET_AREA_VISIBLE`, `0860 SET_CHAR_AREA_VISIBLE`, `04E4 REQUEST_COLLISION`. Report changes with 118 |
| 146 | set_gravity | float | M | L | RE: gravity is a game constant with no opcode. PC writes it directly |
| 104 | enable_stunt_bonus | bit | L | L | RE: the bonus is game code. Harmless to ignore |
| 33 | set_shop_name | 32 bytes | L | L | SCM `075E LOAD_SHOP`; takes a string |
| 167 | disable_remote_vehicle_collisions | bit | L | L | SCM `099A SET_CAR_COLLISION` per remote car |
| 170 | toggle_camera_target | bit | L | L | Client: gate for sending 168 |

### World

| ID | Slot | Fields | Pri | Test | PS2 |
|---|---|---|---|---|---|
| 29 | set_time | hour, minute | M | L | SCM `00C0 SET_TIME_OF_DAY` |
| 94 | set_world_time | hour | M | L | SCM `00C0 SET_TIME_OF_DAY` |
| 60 | game_time | ms | L | L | No action; PC uses it to pace the clock |
| 30 | toggle_clock | u8 | L | L | UI: draw the clock in `ui.c`. The HUD clock flag is RE |
| 152 | set_weather | u8 | M | L | SCM `01B6 FORCE_WEATHER_NOW` |
| 17 | set_world_bounds | max x, min x, max y, min y | M | L | Client: clamp in the frame hook, push back with `083C SET_CHAR_VELOCITY` |
| 16 | play_sound | id, pos | M | L | SCM `018C ADD_ONE_OFF_SOUND`. Whether PS2 sound banks match PC's ids is untested |
| 79 | create_explosion | pos, type, radius | M | L | SCM `0948 ADD_EXPLOSION_VARIABLE_SHAKE`, as the PC client does. Radius has no effect |
| 43 | remove_building | model, pos, radius | H | L | SCM `0363 SET_VISIBILITY_OF_CLOSEST_OBJECT_OF_TYPE` reaches dummies and objects only. Static `CBuilding` entries are RE. Mapped servers depend on this |
| 41 | play_audio_stream | url, pos, distance, use pos | L | L | No: needs HTTP streaming, an MP3 decoder and a route into audio the game owns. Show the URL at most |
| 42 | stop_audio_stream | none | L | L | No: as 41 |
| 112 | play_crime_report | suspect, in vehicle, model, colour, crime, pos | L | L | RE: the police scanner has no opcode. Safe to ignore |
| 96 | scm_event | player, event, vehicle, arg1, arg2 | H | L | By event: 1 SCM `06ED GIVE_VEHICLE_PAINTJOB`, 2 `06E7 ADD_VEHICLE_MOD`, 3 `0229 CHANGE_CAR_COLOUR`, 4 mod shop enter or exit, ignore |

### Text and interface

| ID | Slot | Fields | Pri | Test | PS2 |
|---|---|---|---|---|---|
| 93 | client_message | RGBA, text | H | L | UI. `samp.c` drops the colour; `{RRGGBB}` embeds need parsing |
| 101 | chat | id, text | H | L | UI: prefix with the player's name and colour |
| 73 | game_text | style, time, text | H | L | RE: `00BA PRINT_BIG` needs a key, so call the game's message code directly, or draw it in `ui.c`. `~r~` style codes need translating either way |
| 61 | show_dialog | id, style, title, 2 buttons, body (compressed) | H | L | UI. All styles need a pad-driven form and on-screen keyboard. `id` -1 hides. Answer with 62 |
| 134 | show_textdraw | id, flags, sizes, colours, shadow, outline, style, selectable, pos, preview model, text | H | L | UI through the game's font code. Fonts 0-3 only. Font 4 (sprites) is RE: textures must be loaded. Font 5 (model preview) is No: it needs render-to-texture |
| 135 | hide_textdraw | id | H | L | UI |
| 105 | set_textdraw_text | id, text | H | L | UI |
| 83 | select_textdraw | enable, hover RGBA | H | L | UI: pad-driven cursor. Clicks go out as 83. Login screens on many servers need this |
| 36 | show_label | id, RGBA, pos, distance, LOS, attach ids, text (compressed) | H | L | UI: project with SCM `068D GET_ACTIVE_CAMERA_COORDINATES`, `068E GET_ACTIVE_CAMERA_POINT_AT`, `0801 GET_CAMERA_FOV`, or RE of the game's projection. LOS: `06BD IS_LINE_OF_SIGHT_CLEAR`, as the PC client does |
| 58 | hide_label | id | H | L | UI |
| 59 | chat_bubble | id, RGBA, distance, expire, text | M | L | UI: as 36 |
| 55 | death_message | killer, victim, reason | M | L | UI: kill list; weapon icons are RE |
| 76 | init_menu | id, columns, title, pos, widths, row flags, headers, items | M | L | UI: `08D4 CREATE_MENU` needs keys. Answer with 132 or 140 |
| 77 | show_menu | id | M | L | UI |
| 78 | hide_menu | id | M | L | UI |
| 111 | toggle_widescreen | u8 | L | L | SCM `02A3 SWITCH_WIDESCREEN` |
| 56 | set_map_icon | index, pos, type, RGBA, style | H | L | Type 0: SCM `018A ADD_BLIP_FOR_COORD`, `0165 CHANGE_BLIP_COLOUR`. Others: `02A8 ADD_SPRITE_BLIP_FOR_COORD`, or `04CE ADD_SHORT_RANGE_SPRITE_BLIP_FOR_COORD` for local style. Remember handles per index |
| 144 | remove_map_icon | index | H | L | SCM `0164 REMOVE_BLIP` |
| 108 | show_gangzone | id, min, max, ABGR | H | L | RE: `CRadar` needs a hook to draw an area. Drawing a map overlay in `ui.c` is the alternative |
| 120 | hide_gangzone | id | H | L | RE, as 108 |
| 121 | flash_gangzone | id, ABGR | M | L | RE, as 108 |
| 85 | stop_flash_gangzone | id | M | L | RE, as 108 |

### Checkpoints and pickups

| ID | Slot | Fields | Pri | Test | PS2 |
|---|---|---|---|---|---|
| 107 | set_checkpoint | pos, size | H | L | SCM `06D5 CREATE_CHECKPOINT`, `018A ADD_BLIP_FOR_COORD`. The server detects entry from our sync; nothing to send |
| 37 | disable_checkpoint | none | H | L | SCM `06D6 DELETE_CHECKPOINT`, `0164 REMOVE_BLIP` |
| 38 | set_race_checkpoint | type, pos, next, size | H | L | SCM `06D5 CREATE_CHECKPOINT` with `next` as the point, or `07F3 SET_CHECKPOINT_COORDS` when one exists. Wire types need mapping to game types |
| 39 | disable_race_checkpoint | none | H | L | SCM `06D6 DELETE_CHECKPOINT` |
| 95 | create_pickup | id, model, type, pos | H | L | SCM `0213 CREATE_PICKUP`. Poll `0214 HAS_PICKUP_BEEN_COLLECTED` per frame, send 131 |
| 63 | destroy_pickup | id | H | L | SCM `0215 REMOVE_PICKUP` |
| 151 | destroy_weapon_pickup | index | L | U | SCM `0215 REMOVE_PICKUP`. Stock only; format from the 0.2.5-era handler |

### Objects

| ID | Slot | Fields | Pri | Test | PS2 |
|---|---|---|---|---|---|
| 44 | create_object | id, model, pos, rot, distance, no camera col, attach, materials | H | L | SCM `029B CREATE_OBJECT_NO_OFFSET`, `0453 SET_OBJECT_ROTATION`, `0681 ATTACH_OBJECT_TO_CAR`, `069A ATTACH_OBJECT_TO_OBJECT`. Servers stream up to 1000; the game's object pool size is RE |
| 45 | set_object_pos | id, pos | H | L | SCM `01BC SET_OBJECT_COORDINATES` |
| 46 | set_object_rot | id, rot | H | L | SCM `0453 SET_OBJECT_ROTATION` |
| 47 | destroy_object | id | H | L | SCM `0108 DELETE_OBJECT` |
| 99 | move_object | id, from, to, speed, rot | H | L | Client: move per frame with `01BC SET_OBJECT_COORDINATES` and `0453 SET_OBJECT_ROTATION`. `034E SLIDE_OBJECT` has no end rotation |
| 122 | stop_object | id | H | L | Client |
| 75 | attach_object_to_player | object, player, offset, rot | M | L | SCM `069B ATTACH_OBJECT_TO_CHAR` |
| 84 | set_object_material | object, type, slot, texture or text | H | L | Texture swap is RE: PS2 RenderWare materials differ. Text is No: PC draws it with Windows GDI fonts into a texture. Decoded and ignored; objects keep their stock look |
| 113 | set_attached_object | player, index, create, model, bone, offset, rot, scale, 2 ABGR | H | L | SCM `069B ATTACH_OBJECT_TO_CHAR` has no bone. `070A TASK_PICK_UP_OBJECT` has a bone but one slot per hand. Full support is RE of bone matrices, per frame |
| 169 | object_no_camera_col | id | L | U | RE; ignore. Stock only; id and field per the reference note |
| 27 | begin_object_select | none | L | L | UI: needs a pad cursor over world objects. Low value on a pad |
| 28 | cancel_object_edit | none | L | L | UI |
| 117 | edit_object | player object, id | L | L | UI: mapping-editor feature. Answer with 117 cancel |
| 116 | edit_attached_object | index | L | L | UI, as 117 |
| 81 | attach_camera_to_object | id | L | L | Client: per frame `01BB GET_OBJECT_COORDINATES`, `015F SET_FIXED_CAMERA_POSITION` |

### Camera and spectating

| ID | Slot | Fields | Pri | Test | PS2 |
|---|---|---|---|---|---|
| 157 | set_camera_pos | pos | H | L | SCM `015F SET_FIXED_CAMERA_POSITION` |
| 158 | set_camera_look_at | pos, cut | H | L | SCM `0160 POINT_CAMERA_AT_POINT`. Class selection on most servers |
| 162 | set_camera_behind | none | H | L | SCM `02EB RESTORE_CAMERA_JUMPCUT`, `0373 SET_CAMERA_BEHIND_PLAYER` |
| 82 | interpolate_camera | move or look, from, to, time, cut | M | L | SCM `0460 SET_INTERPOLATION_PARAMETERS`, `0930 CAMERA_PERSIST_POS`, `015F`, `0160` |
| 124 | toggle_spectating | u32 | M | L | SCM `0337 SET_CHAR_VISIBLE`, `01B4 SET_PLAYER_CONTROL`. Send 212 instead of 207 while on |
| 126 | spectate_player | id, mode | M | L | SCM `0159 POINT_CAMERA_AT_CHAR` |
| 127 | spectate_vehicle | id, mode | M | L | SCM `0158 POINT_CAMERA_AT_CAR` |

### Vehicles

| ID | Slot | Fields | Pri | Test | PS2 |
|---|---|---|---|---|---|
| 164 | vehicle_stream_in | id, model, pos, angle, 2 colours, health, interior, damage, siren, 14 mods, paintjob, 2 body colours | H | L | SCM `00A5 CREATE_CAR`, `0175 SET_CAR_HEADING`, `0229 CHANGE_CAR_COLOUR`, `0224 SET_CAR_HEALTH`, `06E7 ADD_VEHICLE_MOD`, `06ED GIVE_VEHICLE_PAINTJOB`, `0840 SET_VEHICLE_AREA_VISIBLE`. Trains: `06D8 CREATE_MISSION_TRAIN` |
| 165 | vehicle_stream_out | id | H | L | SCM `00A6 DELETE_CAR`; trains `07BD DELETE_MISSION_TRAIN` |
| 70 | put_in_vehicle | vehicle, seat | H | L | SCM `036A WARP_CHAR_INTO_CAR`, `0430 WARP_CHAR_INTO_CAR_AS_PASSENGER` |
| 71 | remove_from_vehicle | none | H | L | SCM `0633 TASK_LEAVE_ANY_CAR` |
| 26 | enter_vehicle | player, vehicle, passenger | H | L | SCM `05CB TASK_ENTER_CAR_AS_DRIVER`, `05CA TASK_ENTER_CAR_AS_PASSENGER` on the remote ped |
| 154 | exit_vehicle | player, vehicle | H | L | SCM `05CD TASK_LEAVE_CAR` on the remote ped |
| 147 | set_vehicle_health | id, float | H | L | SCM `0224 SET_CAR_HEALTH` |
| 159 | set_vehicle_pos | id, pos | H | L | SCM `00AB SET_CAR_COORDINATES` |
| 160 | set_vehicle_angle | id, float | M | L | SCM `0175 SET_CAR_HEADING` |
| 91 | set_vehicle_velocity | type, vec3 | M | L | Type 1: SCM `07DB SET_CAR_ROTATION_VELOCITY`. Type 0: `07D5 APPLY_FORCE_TO_CAR` approximates; exact needs the move-speed offset, RE |
| 24 | set_vehicle_params | id, engine, lights, alarm, doors, bonnet, boot, objective, siren, 4 doors, 4 windows | H | L | SCM `0918 SET_CAR_ENGINE_ON`, `067F FORCE_CAR_LIGHTS`, `020A LOCK_CAR_DOORS`, `0657 OPEN_CAR_DOOR`, `0508 CLOSE_ALL_CAR_DOORS`, `0397 SWITCH_CAR_SIREN`. Alarm and windows are RE; objective marker is UI. 255 means leave as it is |
| 161 | set_vehicle_params_old | id, objective, doors | M | U | SCM `020A LOCK_CAR_DOORS`. Stock only: open.mp folds this into 24, stock SetVehicleParamsForPlayer should use it. Field sizes from the 0.2.5-era handler |
| 106 | set_vehicle_damage | id, panels, doors, lights, tyres | H | L | SCM `0697 POP_CAR_PANEL`, `0698 FIX_CAR_PANEL`, `0689 POP_CAR_DOOR`, `068A FIX_CAR_DOOR`, `04FE BURST_CAR_TYRE`, `0699 FIX_CAR_TYRE`. Lights are RE |
| 98 | set_tyre_status | id, status | L | U | SCM `04FE BURST_CAR_TYRE`, `0699 FIX_CAR_TYRE`. Stock only; format from the 0.2.5-era handler |
| 57 | remove_component | id, component | M | L | SCM `06E8 REMOVE_VEHICLE_MOD` |
| 123 | set_plate | id, text | L | L | SCM `0674 CUSTOM_PLATE_FOR_NEXT_CAR` before creating; a live car must be recreated |
| 65 | link_vehicle_interior | id, interior | M | L | SCM `0840 SET_VEHICLE_AREA_VISIBLE` |
| 148 | attach_trailer | trailer, vehicle | M | L | SCM `0893 ATTACH_TRAILER_TO_CAB` |
| 149 | detach_trailer | vehicle | M | L | SCM `07AC DETACH_TRAILER_FROM_CAB` |

### Actors

| ID | Slot | Fields | Pri | Test | PS2 |
|---|---|---|---|---|---|
| 171 | actor_stream_in | id, skin, pos, angle, health, invulnerable | M | L | SCM `009A CREATE_CHAR`, `0173 SET_CHAR_HEADING`, `0223 SET_CHAR_HEALTH`, `02AB SET_CHAR_PROOFS` |
| 172 | actor_stream_out | id | M | L | SCM `009B DELETE_CHAR` |
| 173 | apply_actor_animation | id, lib, name, delta, flags, time | M | L | As 86 |
| 174 | clear_actor_animations | id | M | L | SCM `0792 CLEAR_CHAR_TASKS_IMMEDIATELY` |
| 175 | set_actor_angle | id, float | L | L | SCM `0173 SET_CHAR_HEADING` |
| 176 | set_actor_pos | id, pos | L | L | SCM `00A1 SET_CHAR_COORDINATES` |
| 178 | set_actor_health | id, float | L | L | SCM `0223 SET_CHAR_HEALTH` |

### Refused or not sent to a 0.3.7 client

| ID | Slot | Fields | Status |
|---|---|---|---|
| 103 | client_check | type, address, offset, count | **Unsupported, not answered.** Decoded so it can be logged. An honest reply needs PC client memory. There is deliberately no encoder. `OnClientCheckResponse` never fires for us, so a server that insists on it can kick us. Test: L |
| 48 | none | SetPlayerVirtualWorld | The server sends it to 0.3.DL only (`player_impl.hpp`). Would land in `unknown` |
| 179, 183, 185 | none | custom model list, URL, done | 0.3.DL only: `models.cpp` gates on the client version. Not possible on PS2 anyway |

Registered by the 0.3.7 client but with no known sender, left to `unknown`: 49, 64,
102 (server stats blob), 125. If a stock server turns out to send one, the `unknown`
slot gets the id and the stream.

## Server to client: sync packets

`samp_sync_dispatch`, id byte already consumed. A sync packet may arrive behind
`ID_TIMESTAMP` (40), which `on_packet` already strips.

| ID | Slot | Fields | Pri | Test | PS2 |
|---|---|---|---|---|---|
| 207 | sync_foot | player, optional lr and ud, keys, pos, packed quat, packed health and armour, weapon and key, action, compressed velocity, optional surf, optional anim | H | L | `game_remote_sync` for position. Weapon: SCM `01B9 SET_CURRENT_CHAR_WEAPON`. Keys and aim on a remote ped are RE; PC writes the ped's pad |
| 200 | sync_vehicle | player, vehicle, lr, ud, keys, packed quat, pos, compressed velocity, u16 health, packed health and armour, weapon and key, siren, gear, optional hydra, optional trailer | H | L | SCM `088C SET_CAR_COORDINATES_NO_OFFSET`, `07C6 SET_VEHICLE_QUATERNION`, `0224 SET_CAR_HEALTH`. Driver in seat: `036A WARP_CHAR_INTO_CAR`. Smooth motion needs the move-speed offset, RE |
| 211 | sync_passenger | player, vehicle, seat flags, health, armour, lr, ud, keys, pos | H | L | SCM `0430 WARP_CHAR_INTO_CAR_AS_PASSENGER` once. Drive-by aim is RE |
| 209 | sync_unoccupied | player, vehicle, seat, roll, direction, pos, velocity, turn, health | H | L | SCM `088C SET_CAR_COORDINATES_NO_OFFSET`, `07C6 SET_VEHICLE_QUATERNION` |
| 210 | sync_trailer | player, vehicle, pos, quat, velocity, turn | M | L | As 209. The quat is not packed here |
| 203 | sync_aim | player, cam mode, front, pos, aim z, zoom and state, aspect | H | L | RE: needs a camera and aim override for the remote ped, as PC hooks |
| 206 | sync_bullet | player, hit type, hit id, origin, hit, offset, weapon | M | L | RE for tracers and impacts. Damage needs nothing: each client reports its own |
| 208 | sync_marker | count, then id, active, x, y, z | H | L | SCM `0187 ADD_BLIP_FOR_CHAR` when streamed. Otherwise `018A ADD_BLIP_FOR_COORD`, recreated, since no opcode moves a blip |
| 205 | sync_stats | money, drunk | none | U | open.mp has a write() but never sends it to clients |
| 212 | sync_spectator | optional lr and ud, keys, pos | none | U | As 205, and it has no player id |

Packet 41 turned up once per join in every run. It looks like RakNet's
`ID_RECEIVED_STATIC_DATA`, the entry after `ID_TIMESTAMP`, and nothing needs it; `samp.c` counts it under `sync_in`, and it must not be read
as an RPC id. `samp_sync_dispatch` puts it in `unknown`.

## Client to server

Everything here has an encoder and was checked field by field against `read()`.
**Live** means the real server's callback reported the values we sent.

| ID | Encoder | Fields | Live | When a real client sends it | PS2 source |
|---|---|---|---|---|---|
| 207 | srpc_enc_sync_foot | see `srpc_out_foot_t` | yes | every `foot_rate` ms on foot | Have it in `samp.c`, but weapon, keys, action and anim are zero. Keys: pad state. Weapon: `0470 GET_CURRENT_CHAR_WEAPON` |
| 200 | srpc_enc_sync_vehicle | see `srpc_out_vehicle_t` | yes | every `car_rate` ms driving | **Missing.** `0811 GET_CAR_CHAR_IS_USING`, `07C5 GET_VEHICLE_QUATERNION`, `00AA GET_CAR_COORDINATES`, `06A2 GET_CAR_SPEED_VECTOR`, `0227 GET_CAR_HEALTH` |
| 211 | srpc_enc_sync_passenger | see `srpc_out_passenger_t` | yes | as passenger | **Missing.** Seat from `0432 GET_CHAR_IN_CAR_PASSENGER_SEAT` |
| 209 | srpc_enc_sync_unoccupied | see `srpc_out_unoccupied_t` | yes | for empty cars near us the server assigned | **Missing.** Needs a choice of which car; RE for velocities |
| 210 | srpc_enc_sync_trailer | see `srpc_out_trailer_t` | yes | while towing | **Missing** |
| 203 | srpc_enc_sync_aim | mode, front, pos, z, zoom, state, aspect | yes | every `weapon_rate` ms when aiming | **Missing.** `068D`, `068E` give position and direction. Cam mode is RE |
| 206 | srpc_enc_sync_bullet | hit type, id, origin, hit, offset, weapon | yes | per shot, lag compensation on | **Missing.** `02E0 IS_CHAR_SHOOTING` tells when; hit point is RE |
| 205 | srpc_enc_sync_stats | money, drunk | yes | about once a second | **Missing.** `010B STORE_SCORE`. Without it the server's `GetPlayerMoney` stays 0 |
| 204 | srpc_enc_sync_weapons | target player, target actor, slot, weapon, ammo | yes | when weapons change | **Missing.** `04B8 GET_CHAR_WEAPON_IN_SLOT` for 13 slots |
| 212 | srpc_enc_sync_spectator | lr, ud, keys, pos | yes | while spectating | **Missing** |
| 201 | srpc_enc_rcon | command | no | `/rcon` | Optional. Rcon is off on the test server |
| 25 | srpc_enc_client_join | version, mod, name, response, serial, version text | yes | after connect | Have it |
| 128 | srpc_enc_request_class | class | yes | class change | Have it. We send 32 bits; open.mp reads 16 |
| 129 | none, empty | | yes | spawn button | Have it |
| 52 | none, empty | | yes | after spawn allowed | Have it |
| 53 | srpc_enc_death | reason, killer | yes | on death | **Missing, high.** `0118 IS_CHAR_DEAD`; killer and reason are RE, 255 and 0xFFFF are valid |
| 101 | srpc_enc_chat | text | yes | chat | Have it |
| 50 | srpc_enc_command | text, starts with `/` | yes | commands | **Missing, high.** `samp_chat` sends `/help` to 101, so the server treats it as chat |
| 62 | srpc_enc_dialog_response | id, response, list item, text | yes | closing a dialog | **Missing, high** |
| 83 | srpc_enc_click_textdraw | id, 0xFFFF for Esc | yes | textdraw click | **Missing, high** |
| 26 | srpc_enc_enter_vehicle | vehicle, passenger | yes | starts entering | **Missing, high** |
| 154 | srpc_enc_exit_vehicle | vehicle | yes | starts leaving | **Missing, high** |
| 131 | srpc_enc_picked_up_pickup | id | yes | pickup collected | **Missing, high.** `0214 HAS_PICKUP_BEEN_COLLECTED` |
| 115 | srpc_enc_give_take_damage | taking, player, amount, weapon, part | yes | damage | **Missing, high.** `051A HAS_CHAR_BEEN_DAMAGED_BY_CHAR`; amount and body part are RE, or health delta |
| 106 | srpc_enc_vehicle_damage | vehicle, panels, doors, lights, tyres | yes | driver's damage changed | **Missing.** Damage bits are RE |
| 136 | srpc_enc_vehicle_destroyed | vehicle | yes | car blew up | **Missing.** `0119 IS_CAR_DEAD` |
| 96 | srpc_enc_scm_event | vehicle, arg1, arg2, event | yes | mod shop | **Missing.** Mod shops are off without the story scripts |
| 118 | srpc_enc_interior_change | interior | yes | interior changed | **Missing.** `077E GET_AREA_VISIBLE` |
| 132 | srpc_enc_menu_select | row | yes | menu choice | **Missing** |
| 140 | none, empty | | yes | menu closed | **Missing** |
| 155 | none, empty | | yes | scoreboard open | **Missing** |
| 119 | srpc_enc_click_map | pos | yes | map right-click | Optional; needs the pause map, RE |
| 23 | srpc_enc_click_player | player, source | yes | scoreboard click | With the scoreboard UI |
| 177 | srpc_enc_actor_damage | actor, amount, weapon, part | yes | hit an actor | As 115 |
| 168 | srpc_enc_camera_target | object, vehicle, player, actor | yes | after 170 | Low |
| 27 | srpc_enc_select_object | type, object, model, pos | yes | object picked | Low |
| 117 | srpc_enc_edit_object | player object, id, response, offset, rot | yes | editor | Low |
| 116 | srpc_enc_edit_attached_object | response, index, model, bone, offset, rot, scale, 2 colours | yes | editor | Low |
| 103 | none | | | client check reply | **Never.** See above |
| 54 | none | | | NPC join | Bots only |
| 181, 182, 184 | none | | | model download | 0.3.DL only |

## Requests outside `wip/rpc`

- `payload/scm.c`, `scm.h`: add `scm_str()`. Variable-length strings are data type
  0x0E (length byte, then chars) on PC; confirm on the PS2 interpreter first. Needed
  for 86, 173, 123, 33, 07FB.
- `payload/scm.c`: `code[512]` is enough for one action but not for creating a car
  with mods in one run. Run those in several batches or enlarge it.
- `payload/samp.c`: see the integration notes in the final report.
- `ref/`: the SDK submodule is empty, so `MAX_*` constants and the passenger bitfield
  were taken from SA-MP's documented limits and confirmed by exact bit use on a real
  server, except the drive-by, weapon and key bits of 211, which only pass through.
