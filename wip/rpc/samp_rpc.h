#pragma once
/* SA-MP 0.3.7 message layer: every server-to-client RPC and sync packet decoded
 * into a typed struct, and every client-to-server message encoded. Portable C:
 * no libc, no malloc, nothing about the game or the platform.
 *
 * Wire layouts are transcribed from the write()/read() methods in open.mp
 * Shared/NetCode (MPL-2.0), with isDL false. IDs marked "stock only" are
 * registered by the 0.3.7 client but never sent by open.mp; see coverage.md for
 * how far each of those can be trusted.
 *
 * Decoded structs live in one static buffer that is reused by the next message,
 * so a handler must copy anything it wants to keep. Not reentrant. */
#include "bitstream.h"

/* ---- limits. Longer strings are cut here, the stream still advances fully. */
#define SRPC_NAME_MAX    25    /* MAX_PLAYER_NAME + 1 */
#define SRPC_CHAT_MAX    256
#define SRPC_DIALOG_MAX  4096  /* the 0.3.7 client's own body limit */
#define SRPC_TITLE_MAX   128
#define SRPC_BUTTON_MAX  64
#define SRPC_TD_MAX      1024  /* textdraw */
#define SRPC_LABEL_MAX   1024  /* 3D text label */
#define SRPC_GAMETEXT_MAX 512
#define SRPC_URL_MAX     256
#define SRPC_ANIM_MAX    64
#define SRPC_PLATE_MAX   32
#define SRPC_MAT_NAME_MAX 64
#define SRPC_MAT_TEXT_MAX 256
#define SRPC_MAT_SLOTS   16

#define SRPC_MENU_LINE   32    /* MAX_MENU_TEXT_LENGTH */
#define SRPC_MENU_ROWS   12    /* MAX_MENU_ITEMS */
#define SRPC_SKILLS      11    /* NUM_SKILL_LEVELS */
#define SRPC_CAR_MODS    14    /* MAX_VEHICLE_COMPONENT_SLOT_IN_RPC */
#define SRPC_CAR_MODELS  212   /* MAX_VEHICLE_MODELS */

#define SRPC_TD_GLOBALS  2048  /* textdraw ids at or above this are per-player */
#define SRPC_LABEL_GLOBALS 1024 /* label ids at or above this are per-player */
#define SRPC_INVALID_ID  0xFFFFu

#define SRPC_TD_LEFT   1 /* wire bits, not TextDrawAlignment()'s 1, 2, 3 */
#define SRPC_TD_RIGHT  2
#define SRPC_TD_CENTER 4

/* ---- field groups shared by several messages ----------------------------- */

typedef struct { unsigned id; } srpc_id_t;
typedef struct { int v; } srpc_int_t;
typedef struct { float v; } srpc_float_t;
typedef struct { float v[3]; } srpc_vec3_t;
typedef struct { unsigned id; float v; } srpc_id_float_t;
typedef struct { unsigned id; float v[3]; } srpc_id_vec3_t;
typedef struct { unsigned id; unsigned v; } srpc_id_val_t;

typedef struct {
    char lib[SRPC_ANIM_MAX], name[SRPC_ANIM_MAX];
    float delta;
    int loop, lock_x, lock_y, freeze;
    unsigned time;
} srpc_anim_t;

typedef struct {
    unsigned char type; /* 1 texture, 2 text */
    unsigned char slot;
    unsigned model;                 /* type 1 */
    char txd[SRPC_MAT_NAME_MAX];    /* type 1 */
    char texture[SRPC_MAT_NAME_MAX];/* type 1 */
    unsigned color;                 /* type 1, ABGR */
    unsigned char size;             /* type 2 */
    char font[SRPC_MAT_NAME_MAX];   /* type 2 */
    unsigned char font_size, bold, align; /* type 2 */
    unsigned font_color, back_color;/* type 2, ARGB */
    char text[SRPC_MAT_TEXT_MAX];   /* type 2 */
} srpc_material_t;

/* ---- connection and players ---------------------------------------------- */

typedef struct {
    int zone_names, ped_anims, interior_weapons, use_chat_radius;
    float chat_radius;
    int stunt_bonus;
    float nametag_distance;
    int no_enex, nametag_los, manual_engine;
    unsigned spawn_count, player_id;
    int nametags;
    unsigned markers;
    unsigned char hour, weather;
    float gravity;
    int lan_mode;
    unsigned death_drop;
    int instagib;
    unsigned foot_rate, car_rate, weapon_rate, multiplier, lag_comp;
    char server_name[SRPC_NAME_MAX * 4];
    unsigned char vehicle_models[SRPC_CAR_MODELS];
    unsigned vehicle_friendly_fire;
} srpc_init_game_t;

typedef struct { unsigned id, color; int npc; char name[SRPC_NAME_MAX]; } srpc_player_join_t;
typedef struct { unsigned id, reason; } srpc_player_quit_t;

typedef struct {
    unsigned id, team, skin;
    float pos[3], angle;
    unsigned color, fight_style;
    unsigned short skills[SRPC_SKILLS];
} srpc_player_stream_in_t;

typedef struct { unsigned id; char name[SRPC_NAME_MAX]; unsigned success; } srpc_set_name_t;
typedef struct { unsigned color; char text[SRPC_CHAT_MAX]; } srpc_client_msg_t;
typedef struct { unsigned id; char text[SRPC_CHAT_MAX]; } srpc_chat_t;
typedef struct { unsigned killer, victim, reason; } srpc_death_msg_t;
typedef struct { unsigned id, color; float distance; unsigned expire; char text[SRPC_CHAT_MAX]; } srpc_chat_bubble_t;
typedef struct { unsigned id, skill, level; } srpc_skill_t;
typedef struct { unsigned id; srpc_anim_t anim; } srpc_id_anim_t;
typedef struct { unsigned sound; float pos[3]; } srpc_sound_t;
typedef struct { unsigned hour, minute; } srpc_time_t;
typedef struct { unsigned weapon, ammo; } srpc_weapon_t;
typedef struct { char url[SRPC_URL_MAX]; float pos[3], distance; int use_pos; } srpc_audio_t;
typedef struct { unsigned suspect, in_vehicle, model, color, crime; float pos[3]; } srpc_crime_t;
typedef struct { unsigned model; float pos[3], radius; } srpc_remove_building_t;
typedef struct { float pos[3]; unsigned type; float radius; } srpc_explosion_t;
typedef struct { unsigned player, vehicle, event, arg1, arg2; } srpc_scm_event_t;
typedef struct { unsigned style, time; char text[SRPC_GAMETEXT_MAX]; } srpc_game_text_t;
typedef struct { unsigned icon; float pos[3]; unsigned type, color, style; } srpc_map_icon_t;
typedef struct { unsigned id; int show; } srpc_nametag_t;
typedef struct { unsigned id, mode; } srpc_spectate_t;
typedef struct { unsigned type, address, offset, count; } srpc_client_check_t;
typedef struct { float pos[3]; unsigned cut; } srpc_look_at_t;
typedef struct { int pos_set; float from[3], to[3]; unsigned time, cut; } srpc_camera_move_t;
typedef struct { float max_x, min_x, max_y, min_y; } srpc_bounds_t; /* wire order */
typedef struct { unsigned id, score, ping; } srpc_score_t;

typedef struct {
    unsigned selectable, team, skin, unknown;
    float spawn[3], angle;
    unsigned weapons[3], ammo[3];
} srpc_class_t;

typedef struct {
    unsigned team, skin, unknown;
    float spawn[3], angle;
    unsigned weapons[3], ammo[3];
} srpc_spawn_info_t;

/* ---- world ---------------------------------------------------------------- */

typedef struct { float pos[3], size; } srpc_checkpoint_t;
typedef struct { unsigned type; float pos[3], next[3], size; } srpc_race_checkpoint_t;

typedef struct { int id, model, type; float pos[3]; } srpc_pickup_t;

typedef struct {
    int id;
    float min_x, min_y, max_x, max_y;
    unsigned color; /* ABGR */
} srpc_gangzone_t;
typedef struct { int id; unsigned color; } srpc_gangzone_flash_t; /* ABGR */

typedef struct {
    int player;   /* true when the wire id was in the per-player range */
    unsigned id;  /* pool index with the per-player offset removed */
    unsigned color;
    float pos[3], distance;
    int los;
    unsigned attach_player, attach_vehicle;
    char text[SRPC_LABEL_MAX];
} srpc_label_t;
typedef struct { int player; unsigned id; } srpc_label_id_t;

typedef struct {
    int player;
    unsigned id;
    int box;
    unsigned align; /* SRPC_TD_LEFT, _RIGHT or _CENTER; 0 when the script set none */
    int proportional;
    float letter_w, letter_h;
    unsigned letter_color;  /* ABGR */
    float text_w, text_h;
    unsigned box_color;     /* ABGR */
    unsigned shadow, outline;
    unsigned back_color;    /* ABGR */
    unsigned style;
    int selectable;
    float x, y;
    unsigned model;
    float rot[3], zoom;
    int color1, color2;
    char text[SRPC_TD_MAX];
} srpc_textdraw_t;
typedef struct { int player; unsigned id; } srpc_textdraw_id_t;
typedef struct { int player; unsigned id; char text[SRPC_TD_MAX]; } srpc_textdraw_text_t;
typedef struct { int enable; unsigned color; } srpc_textdraw_select_t;

typedef struct {
    int id;
    unsigned style;
    char title[SRPC_TITLE_MAX];
    char button1[SRPC_BUTTON_MAX], button2[SRPC_BUTTON_MAX];
    char body[SRPC_DIALOG_MAX];
} srpc_dialog_t;

typedef struct {
    unsigned id;
    int two_columns;
    char title[SRPC_MENU_LINE + 1];
    float x, y, col1_width, col2_width;
    int enabled;
    int row_enabled[SRPC_MENU_ROWS];
    char header[2][SRPC_MENU_LINE + 1];
    unsigned count[2];
    char items[2][SRPC_MENU_ROWS][SRPC_MENU_LINE + 1];
} srpc_menu_t;

typedef struct {
    unsigned id;
    int model;
    float pos[3], rot[3], distance;
    int no_camera_col;
    unsigned attach_vehicle, attach_object;
    float offset[3], offset_rot[3]; /* valid when attached */
    int sync_rotation;              /* valid when attached */
    unsigned material_count;
    srpc_material_t materials[SRPC_MAT_SLOTS];
} srpc_object_t;

typedef struct { unsigned object; srpc_material_t material; } srpc_object_material_t;
typedef struct { unsigned id; float from[3], to[3], speed, rot[3]; } srpc_object_move_t;
typedef struct { unsigned object, player; float offset[3], rot[3]; } srpc_object_attach_player_t;

typedef struct {
    unsigned player, index;
    int create;
    unsigned model, bone;
    float offset[3], rot[3], scale[3];
    unsigned color1, color2; /* ABGR */
} srpc_attached_object_t;
typedef struct { int player_object; unsigned object; } srpc_object_edit_t;

/* ---- vehicles ------------------------------------------------------------- */

typedef struct {
    unsigned id, model;
    float pos[3], angle;
    unsigned color1, color2;
    float health;
    unsigned interior, door_damage, panel_damage, light_damage, tyre_damage, siren;
    unsigned short mods[SRPC_CAR_MODS]; /* component model id, 0 for none */
    unsigned paintjob;
    int body_color1, body_color2;
} srpc_vehicle_t;

typedef struct { unsigned vehicle, seat; } srpc_put_in_vehicle_t;
typedef struct { unsigned player, vehicle, passenger; } srpc_enter_vehicle_t;
typedef struct { unsigned player, vehicle; } srpc_exit_vehicle_t;
typedef struct { unsigned id; char plate[SRPC_PLATE_MAX]; } srpc_plate_t;
typedef struct { unsigned id, panels, doors, lights, tyres; } srpc_vehicle_damage_t;
typedef struct { unsigned id, component; } srpc_component_t;
typedef struct { unsigned trailer, vehicle; } srpc_trailer_t;
typedef struct { unsigned type; float v[3]; } srpc_vehicle_velocity_t;
typedef struct { unsigned id, objective, doors; } srpc_vehicle_params_old_t;
typedef struct {
    unsigned id;
    unsigned char engine, lights, alarm, doors, bonnet, boot, objective, siren;
    unsigned char door[4], window[4]; /* driver, passenger, back left, back right */
} srpc_vehicle_params_t;

/* ---- actors --------------------------------------------------------------- */

typedef struct { unsigned id, skin; float pos[3], angle, health; int invulnerable; } srpc_actor_t;

/* ---- sync packets, relayed from other players ---------------------------- */

typedef struct {
    unsigned player, lr, ud, keys;
    float pos[3], quat[4]; /* w, x, y, z */
    unsigned health, armour; /* from the packed pair: steps of 7, 15 meaning 100 */
    unsigned weapon, additional_key, special_action;
    float velocity[3];
    int surfing;
    unsigned surf_id;  /* a vehicle id, or vehicle pool size + object id */
    float surf_offset[3];
    unsigned anim_id, anim_flags;
} srpc_foot_sync_t;

typedef struct {
    unsigned player, vehicle, lr, ud, keys;
    float quat[4], pos[3], velocity[3];
    unsigned health, player_health, player_armour, weapon, additional_key;
    int siren, landing_gear;
    unsigned hydra_angle;
    int has_trailer;
    unsigned trailer;
} srpc_vehicle_sync_t;

typedef struct {
    unsigned player, vehicle;
    unsigned seat_flags; /* raw: seat, drive-by, weapon and additional key bits */
    unsigned seat, drive_by, weapon, additional_key;
    unsigned health, armour, lr, ud, keys;
    float pos[3];
} srpc_passenger_sync_t;

typedef struct {
    unsigned player, cam_mode;
    float cam_front[3], cam_pos[3], aim_z;
    unsigned cam_zoom, weapon_state, aspect;
} srpc_aim_sync_t;

typedef struct {
    unsigned player, hit_type, hit_id;
    float origin[3], hit[3], offset[3];
    unsigned weapon;
} srpc_bullet_sync_t;

typedef struct {
    unsigned player, vehicle, seat;
    float roll[3], direction[3], pos[3], velocity[3], turn[3], health;
} srpc_unoccupied_sync_t;

typedef struct {
    unsigned player, vehicle;
    float pos[3], quat[4], velocity[3], turn[3];
} srpc_trailer_sync_t;

typedef struct { unsigned lr, ud, keys; float pos[3]; } srpc_spectator_sync_t;
typedef struct { int money, drunk; } srpc_stats_sync_t;
typedef struct { unsigned id; int active; int x, y, z; } srpc_marker_t;

/* ---- the table ------------------------------------------------------------
 * X(id, slot, argument). One row per server-to-client RPC. Each row produces
 * SRPC_<slot> = id and a handler slot of that name. */
#define SRPC_TABLE(X) \
    X(11,  set_player_name,      const srpc_set_name_t *) \
    X(12,  set_pos,              const srpc_vec3_t *) \
    X(13,  set_pos_find_z,       const srpc_vec3_t *) \
    X(14,  set_health,           const srpc_float_t *) \
    X(15,  toggle_controllable,  const srpc_int_t *) \
    X(16,  play_sound,           const srpc_sound_t *) \
    X(17,  set_world_bounds,     const srpc_bounds_t *) \
    X(18,  give_money,           const srpc_int_t *) \
    X(19,  set_facing_angle,     const srpc_float_t *) \
    X(20,  reset_money,          void) \
    X(21,  reset_weapons,        void) \
    X(22,  give_weapon,          const srpc_weapon_t *) \
    X(24,  set_vehicle_params,   const srpc_vehicle_params_t *) \
    X(26,  enter_vehicle,        const srpc_enter_vehicle_t *) \
    X(27,  begin_object_select,  void) \
    X(28,  cancel_object_edit,   void) \
    X(29,  set_time,             const srpc_time_t *) \
    X(30,  toggle_clock,         const srpc_int_t *) \
    X(32,  player_stream_in,     const srpc_player_stream_in_t *) \
    X(33,  set_shop_name,        const char *) \
    X(34,  set_skill_level,      const srpc_skill_t *) \
    X(35,  set_drunk_level,      const srpc_int_t *) \
    X(36,  show_label,           const srpc_label_t *) \
    X(37,  disable_checkpoint,   void) \
    X(38,  set_race_checkpoint,  const srpc_race_checkpoint_t *) \
    X(39,  disable_race_checkpoint, void) \
    X(40,  game_mode_restart,    void) \
    X(41,  play_audio_stream,    const srpc_audio_t *) \
    X(42,  stop_audio_stream,    void) \
    X(43,  remove_building,      const srpc_remove_building_t *) \
    X(44,  create_object,        const srpc_object_t *) \
    X(45,  set_object_pos,       const srpc_id_vec3_t *) \
    X(46,  set_object_rot,       const srpc_id_vec3_t *) \
    X(47,  destroy_object,       const srpc_id_t *) \
    X(55,  death_message,        const srpc_death_msg_t *) \
    X(56,  set_map_icon,         const srpc_map_icon_t *) \
    X(57,  remove_component,     const srpc_component_t *) \
    X(58,  hide_label,           const srpc_label_id_t *) \
    X(59,  chat_bubble,          const srpc_chat_bubble_t *) \
    X(60,  game_time,            const srpc_int_t *) \
    X(61,  show_dialog,          const srpc_dialog_t *) \
    X(63,  destroy_pickup,       const srpc_int_t *) \
    X(65,  link_vehicle_interior, const srpc_id_val_t *) \
    X(66,  set_armour,           const srpc_float_t *) \
    X(67,  set_armed_weapon,     const srpc_id_t *) \
    X(68,  set_spawn_info,       const srpc_spawn_info_t *) \
    X(69,  set_team,             const srpc_id_val_t *) \
    X(70,  put_in_vehicle,       const srpc_put_in_vehicle_t *) \
    X(71,  remove_from_vehicle,  void) \
    X(72,  set_color,            const srpc_id_val_t *) \
    X(73,  game_text,            const srpc_game_text_t *) \
    X(74,  force_class_selection, void) \
    X(75,  attach_object_to_player, const srpc_object_attach_player_t *) \
    X(76,  init_menu,            const srpc_menu_t *) \
    X(77,  show_menu,            const srpc_id_t *) \
    X(78,  hide_menu,            const srpc_id_t *) \
    X(79,  create_explosion,     const srpc_explosion_t *) \
    X(80,  show_nametag,         const srpc_nametag_t *) \
    X(81,  attach_camera_to_object, const srpc_id_t *) \
    X(82,  interpolate_camera,   const srpc_camera_move_t *) \
    X(83,  select_textdraw,      const srpc_textdraw_select_t *) \
    X(84,  set_object_material,  const srpc_object_material_t *) \
    X(85,  stop_flash_gangzone,  const srpc_int_t *) \
    X(86,  apply_animation,      const srpc_id_anim_t *) \
    X(87,  clear_animations,     const srpc_id_t *) \
    X(88,  set_special_action,   const srpc_id_t *) \
    X(89,  set_fighting_style,   const srpc_id_val_t *) \
    X(90,  set_velocity,         const srpc_vec3_t *) \
    X(91,  set_vehicle_velocity, const srpc_vehicle_velocity_t *) \
    X(92,  set_drunk_visuals,    const srpc_int_t *)   /* stock only */ \
    X(93,  client_message,       const srpc_client_msg_t *) \
    X(94,  set_world_time,       const srpc_id_t *) \
    X(95,  create_pickup,        const srpc_pickup_t *) \
    X(96,  scm_event,            const srpc_scm_event_t *) \
    X(98,  set_tyre_status,      const srpc_id_val_t *) /* stock only */ \
    X(99,  move_object,          const srpc_object_move_t *) \
    X(101, chat,                 const srpc_chat_t *) \
    X(103, client_check,         const srpc_client_check_t *) \
    X(104, enable_stunt_bonus,   const srpc_int_t *) \
    X(105, set_textdraw_text,    const srpc_textdraw_text_t *) \
    X(106, set_vehicle_damage,   const srpc_vehicle_damage_t *) \
    X(107, set_checkpoint,       const srpc_checkpoint_t *) \
    X(108, show_gangzone,        const srpc_gangzone_t *) \
    X(111, toggle_widescreen,    const srpc_int_t *) \
    X(112, play_crime_report,    const srpc_crime_t *) \
    X(113, set_attached_object,  const srpc_attached_object_t *) \
    X(116, edit_attached_object, const srpc_id_t *) \
    X(117, edit_object,          const srpc_object_edit_t *) \
    X(120, hide_gangzone,        const srpc_int_t *) \
    X(121, flash_gangzone,       const srpc_gangzone_flash_t *) \
    X(122, stop_object,          const srpc_id_t *) \
    X(123, set_plate,            const srpc_plate_t *) \
    X(124, toggle_spectating,    const srpc_int_t *) \
    X(126, spectate_player,      const srpc_spectate_t *) \
    X(127, spectate_vehicle,     const srpc_spectate_t *) \
    X(128, request_class_reply,  const srpc_class_t *) \
    X(129, request_spawn_reply,  const srpc_id_t *) \
    X(130, connection_rejected,  const srpc_id_t *)    /* stock only */ \
    X(133, set_wanted_level,     const srpc_id_t *) \
    X(134, show_textdraw,        const srpc_textdraw_t *) \
    X(135, hide_textdraw,        const srpc_textdraw_id_t *) \
    X(137, player_join,          const srpc_player_join_t *) \
    X(138, player_quit,          const srpc_player_quit_t *) \
    X(139, init_game,            const srpc_init_game_t *) \
    X(144, remove_map_icon,      const srpc_id_t *) \
    X(145, set_ammo,             const srpc_weapon_t *) \
    X(146, set_gravity,          const srpc_float_t *) \
    X(147, set_vehicle_health,   const srpc_id_float_t *) \
    X(148, attach_trailer,       const srpc_trailer_t *) \
    X(149, detach_trailer,       const srpc_id_t *) \
    X(150, set_drunk_handling,   const srpc_int_t *)   /* stock only */ \
    X(151, destroy_weapon_pickup, const srpc_id_t *)   /* stock only */ \
    X(152, set_weather,          const srpc_id_t *) \
    X(153, set_skin,             const srpc_id_val_t *) \
    X(154, exit_vehicle,         const srpc_exit_vehicle_t *) \
    X(155, score_and_ping,       const srpc_score_t *)  /* once per player */ \
    X(156, set_interior,         const srpc_id_t *) \
    X(157, set_camera_pos,       const srpc_vec3_t *) \
    X(158, set_camera_look_at,   const srpc_look_at_t *) \
    X(159, set_vehicle_pos,      const srpc_id_vec3_t *) \
    X(160, set_vehicle_angle,    const srpc_id_float_t *) \
    X(161, set_vehicle_params_old, const srpc_vehicle_params_old_t *) /* stock only */ \
    X(162, set_camera_behind,    void) \
    X(163, player_stream_out,    const srpc_id_t *) \
    X(164, vehicle_stream_in,    const srpc_vehicle_t *) \
    X(165, vehicle_stream_out,   const srpc_id_t *) \
    X(166, player_death,         const srpc_id_t *) \
    X(167, disable_remote_vehicle_collisions, const srpc_int_t *) \
    X(169, object_no_camera_col, const srpc_id_t *)    /* stock only */ \
    X(170, toggle_camera_target, const srpc_int_t *) \
    X(171, actor_stream_in,      const srpc_actor_t *) \
    X(172, actor_stream_out,     const srpc_id_t *) \
    X(173, apply_actor_animation, const srpc_id_anim_t *) \
    X(174, clear_actor_animations, const srpc_id_t *) \
    X(175, set_actor_angle,      const srpc_id_float_t *) \
    X(176, set_actor_pos,        const srpc_id_vec3_t *) \
    X(178, set_actor_health,     const srpc_id_float_t *)

/* X(id, slot, argument). One row per relayed sync packet. */
#define SRPC_SYNC_TABLE(X) \
    X(200, sync_vehicle,    const srpc_vehicle_sync_t *) \
    X(203, sync_aim,        const srpc_aim_sync_t *) \
    X(205, sync_stats,      const srpc_stats_sync_t *) \
    X(206, sync_bullet,     const srpc_bullet_sync_t *) \
    X(207, sync_foot,       const srpc_foot_sync_t *) \
    X(208, sync_marker,     const srpc_marker_t *)  /* once per player */ \
    X(209, sync_unoccupied, const srpc_unoccupied_sync_t *) \
    X(210, sync_trailer,    const srpc_trailer_sync_t *) \
    X(211, sync_passenger,  const srpc_passenger_sync_t *) \
    X(212, sync_spectator,  const srpc_spectator_sync_t *)

#define SRPC_ENUM_(id, slot, arg) SRPC_##slot = id,
enum { SRPC_TABLE(SRPC_ENUM_) SRPC_SYNC_TABLE(SRPC_ENUM_) };
#undef SRPC_ENUM_

/* Every slot left at 0 is a no-op, so a platform fills in only what it can do. */
typedef struct {
#define SRPC_SLOT_(id, slot, arg) void (*slot)(arg);
    SRPC_TABLE(SRPC_SLOT_)
    SRPC_SYNC_TABLE(SRPC_SLOT_)
#undef SRPC_SLOT_
    /* An id nothing here decodes. The stream is positioned at the payload. */
    void (*unknown)(unsigned id, bs_t *b);
} samp_rpc_handlers_t;

typedef struct {
    unsigned decoded;   /* messages decoded cleanly, whether or not a slot was set */
    unsigned truncated; /* messages shorter than their layout: handler not called */
    unsigned unknown;
    unsigned last_bad;  /* id of the last truncated message */
} samp_rpc_stats_t;
extern samp_rpc_stats_t samp_rpc_stats;

void samp_rpc_set_handlers(const samp_rpc_handlers_t *h);

/* Decode one RPC. b must be positioned at the start of the RPC payload. */
void samp_rpc_dispatch(unsigned id, bs_t *b);

/* Decode one sync packet. b must be positioned just after the packet id byte. */
void samp_sync_dispatch(unsigned id, bs_t *b);

/* Name of an RPC or sync id, or 0 when there is none. For logging. */
const char *samp_rpc_name(unsigned id);

/* Read a Huffman-compressed string (RakNet StringCompressor). Always terminates
 * out, and always consumes the whole string from the stream. Returns its length. */
unsigned samp_read_compressed_str(bs_t *b, char *out, unsigned cap);
/* The inverse, for tests. The client never has to send one of these. */
void samp_write_compressed_str(bs_t *b, const char *s);

/* ---- client to server ------------------------------------------------------
 * RPC encoders write only the payload; pass it to rak_rpc with the C2S id. Sync
 * encoders write the packet id too; pass the result to rak_send with
 * RAK_UNRELIABLE_SEQ. All of them leave b->err set on overflow. */
enum {
    SRPC_C2S_CLICK_PLAYER = 23,
    SRPC_C2S_CLIENT_JOIN = 25,
    SRPC_C2S_ENTER_VEHICLE = 26,
    SRPC_C2S_SELECT_OBJECT = 27,
    SRPC_C2S_COMMAND = 50,
    SRPC_C2S_SPAWN = 52,
    SRPC_C2S_DEATH = 53,
    SRPC_C2S_DIALOG_RESPONSE = 62,
    SRPC_C2S_CLICK_TEXTDRAW = 83,
    SRPC_C2S_SCM_EVENT = 96,
    SRPC_C2S_CHAT = 101,
    SRPC_C2S_VEHICLE_DAMAGE = 106,
    SRPC_C2S_GIVE_TAKE_DAMAGE = 115,
    SRPC_C2S_EDIT_ATTACHED_OBJECT = 116,
    SRPC_C2S_EDIT_OBJECT = 117,
    SRPC_C2S_INTERIOR_CHANGE = 118,
    SRPC_C2S_CLICK_MAP = 119,
    SRPC_C2S_REQUEST_CLASS = 128,
    SRPC_C2S_REQUEST_SPAWN = 129,
    SRPC_C2S_PICKED_UP_PICKUP = 131,
    SRPC_C2S_MENU_SELECT = 132,
    SRPC_C2S_VEHICLE_DESTROYED = 136,
    SRPC_C2S_MENU_QUIT = 140,
    SRPC_C2S_EXIT_VEHICLE = 154,
    SRPC_C2S_REQUEST_SCORES = 155,
    SRPC_C2S_CAMERA_TARGET = 168,
    SRPC_C2S_ACTOR_DAMAGE = 177
};

void srpc_enc_click_player(bs_t *b, unsigned player, unsigned source);
void srpc_enc_client_join(bs_t *b, unsigned version, unsigned modded, const char *name,
                          unsigned challenge_response, const char *serial, const char *version_text);
void srpc_enc_enter_vehicle(bs_t *b, unsigned vehicle, int passenger);
void srpc_enc_select_object(bs_t *b, unsigned type, unsigned object, unsigned model, const float *pos);
void srpc_enc_command(bs_t *b, const char *text);   /* text starts with '/' */
void srpc_enc_death(bs_t *b, unsigned reason, unsigned killer);
void srpc_enc_dialog_response(bs_t *b, int dialog, int response, int list_item, const char *text);
void srpc_enc_click_textdraw(bs_t *b, unsigned wire_id); /* SRPC_INVALID_ID for Esc */
void srpc_enc_scm_event(bs_t *b, unsigned vehicle, unsigned arg1, unsigned arg2, unsigned event);
void srpc_enc_chat(bs_t *b, const char *text);
void srpc_enc_vehicle_damage(bs_t *b, unsigned vehicle, unsigned panels, unsigned doors,
                             unsigned lights, unsigned tyres);
void srpc_enc_give_take_damage(bs_t *b, int taking, unsigned player, float damage,
                               unsigned weapon, unsigned bodypart);
void srpc_enc_edit_attached_object(bs_t *b, unsigned response, unsigned index, unsigned model, unsigned bone,
                                   const float *offset, const float *rot, const float *scale,
                                   unsigned color1, unsigned color2);
void srpc_enc_edit_object(bs_t *b, int player_object, unsigned object, unsigned response,
                          const float *offset, const float *rot);
void srpc_enc_interior_change(bs_t *b, unsigned interior);
void srpc_enc_click_map(bs_t *b, const float *pos);
void srpc_enc_request_class(bs_t *b, unsigned cls);
void srpc_enc_picked_up_pickup(bs_t *b, int pickup);
void srpc_enc_menu_select(bs_t *b, unsigned row);
void srpc_enc_vehicle_destroyed(bs_t *b, unsigned vehicle);
void srpc_enc_exit_vehicle(bs_t *b, unsigned vehicle);
void srpc_enc_camera_target(bs_t *b, unsigned object, unsigned vehicle, unsigned player, unsigned actor);
void srpc_enc_actor_damage(bs_t *b, unsigned actor, float damage, unsigned weapon, unsigned bodypart);

/* Sync, in the client's uncompressed form: a server only compresses what it relays. */
typedef struct {
    unsigned lr, ud, keys;
    float pos[3], quat[4]; /* w, x, y, z */
    unsigned health, armour; /* 0-255 */
    unsigned weapon, additional_key, special_action;
    float velocity[3], surf_offset[3];
    unsigned surf_id, anim_id, anim_flags;
} srpc_out_foot_t;

typedef struct {
    unsigned vehicle, lr, ud, keys;
    float quat[4], pos[3], velocity[3], health;
    unsigned player_health, player_armour, weapon, additional_key, siren, landing_gear, trailer, hydra_angle;
} srpc_out_vehicle_t;

typedef struct {
    unsigned vehicle, seat_flags, health, armour, lr, ud, keys;
    float pos[3];
} srpc_out_passenger_t;

typedef struct {
    unsigned vehicle, seat;
    float roll[3], direction[3], pos[3], velocity[3], turn[3], health;
} srpc_out_unoccupied_t;

typedef struct {
    unsigned vehicle;
    float pos[3], quat[4], velocity[3], turn[3];
} srpc_out_trailer_t;

typedef struct { unsigned slot, weapon, ammo; } srpc_out_weapon_t;

void srpc_enc_sync_foot(bs_t *b, const srpc_out_foot_t *s);
void srpc_enc_sync_vehicle(bs_t *b, const srpc_out_vehicle_t *s);
void srpc_enc_sync_passenger(bs_t *b, const srpc_out_passenger_t *s);
void srpc_enc_sync_unoccupied(bs_t *b, const srpc_out_unoccupied_t *s);
void srpc_enc_sync_trailer(bs_t *b, const srpc_out_trailer_t *s);
void srpc_enc_sync_aim(bs_t *b, unsigned cam_mode, const float *cam_front, const float *cam_pos,
                       float aim_z, unsigned cam_zoom, unsigned weapon_state, unsigned aspect);
void srpc_enc_sync_bullet(bs_t *b, unsigned hit_type, unsigned hit_id, const float *origin,
                          const float *hit, const float *offset, unsigned weapon);
void srpc_enc_sync_stats(bs_t *b, int money, int drunk);
void srpc_enc_sync_weapons(bs_t *b, unsigned target_player, unsigned target_actor,
                           const srpc_out_weapon_t *w, unsigned count);
void srpc_enc_sync_spectator(bs_t *b, unsigned lr, unsigned ud, unsigned keys, const float *pos);
void srpc_enc_rcon(bs_t *b, const char *command);
