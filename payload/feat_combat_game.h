#pragma once
/* The game side of feat_combat: every address, ped offset and SCM opcode the combat
 * stream uses lives behind this interface. feat_combat.c holds only logic, so the tests
 * in wip/combat run it on the host with this same file over a fake memory image.
 *
 * Every address and offset here is backed by the instructions listed in
 * wip/combat/check_re.py, which reads them out of the ELF. */

typedef struct {
    short lx, ly, rx, ry;
    short l1, l2, r1, r2;
    short up, down, left, right;
    short start, select;
    short square, triangle, cross, circle;
    short l3, r3;
} cg_pad_t; /* same layout as CPad NewState, see ui_game.h */

typedef struct {
    unsigned ped;      /* game pointer, only ever compared or passed back in here */
    float pos[3], vel[3];
    float health, armour;
    int dead, in_car, ducking;
    unsigned weapon;   /* type in the current slot */
    unsigned ammo;     /* total ammo in the current slot */
    unsigned clip, wstate;
    float fwd[2];      /* which way it faces, for a camera behind it */
    int last_weapon;   /* last damage: weapon type, -1 for none */
    unsigned last_source; /* last damage: entity pointer, 0 for none */
} cg_ped_t;

#define CG_SLOTS 13
typedef struct { unsigned weapon, ammo; } cg_slot_t;

typedef struct {
    unsigned mode;
    float front[3], pos[3];
} cg_camera_t;

/* ---- reads: memory only, safe to call any number of times per frame ---- */
int  cg_read_local(cg_ped_t *out);            /* 0 when there is no player ped */
int  cg_read_ped(int handle, cg_ped_t *out);  /* 0 when the handle is stale */
int  cg_handle_valid(int handle);
int  cg_read_car(int handle, float *pos, float *fwd); /* pos 3, fwd 2. 0 when stale */
void cg_read_slots(cg_slot_t *slots);         /* local ped, CG_SLOTS entries */
int  cg_pad(cg_pad_t *out, int *lr, int *ud); /* 0 when the player has no control */
int  cg_read_camera(cg_camera_t *out);
int  cg_money(void);
int  cg_restart_pending(void);                /* the game's wasted or busted state */
int  cg_local_handle(void);
int  cg_model_ok(unsigned id);                /* has a model info entry and is on the disc */

/* ---- writes on the local ped ---- */
void cg_set_health(float v);                  /* 0 or less kills through the game's die event */
void cg_set_armour(float v);
void cg_set_control(int on);
void cg_move_local(const float *pos);
void cg_set_heading(float angle);
void cg_set_velocity(const float *v);
void cg_set_visible(int on);
void cg_set_bullet_proof(int on);
void cg_override_restart(const float *pos, float angle);
void cg_no_death_penalty(void);
void cg_give_money(int amount);
void cg_set_area(unsigned area);
int  cg_get_area(void);
void cg_set_drunk(int level);
void cg_set_float_stat(unsigned stat, float v);
float cg_ground_z(float x, float y, float z);

/* ---- weapons, on any ped handle ---- */
int  cg_load_weapon(unsigned weapon);         /* 1 if resident, else requests it and returns 0 */
int  cg_model_loaded(unsigned id);
void cg_give_weapon(int handle, unsigned weapon, unsigned ammo);
void cg_set_ammo(int handle, unsigned weapon, unsigned ammo);
void cg_arm_weapon(int handle, unsigned weapon);
void cg_remove_weapons(int handle);
void cg_set_fight_style(int handle, unsigned style);
void cg_set_weapon_skill(int handle, unsigned skill);

/* ---- remote peds ---- */
void cg_ped_prepare(int handle);              /* no reactions, no critical hits, no drowning */
void cg_ped_health(int handle, float v);      /* plain store, never kills */
void cg_ped_clear_damage(int handle);
void cg_ped_die(int handle);
void cg_ped_aim(int handle, const float *at, int ms);
void cg_ped_shoot(int handle, const float *at, int ms);
void cg_ped_jump(int handle);
void cg_ped_duck(int handle, int on);
void cg_ped_hands_up(int handle, int ms);
void cg_ped_jetpack(int handle);
void cg_ped_clear_tasks(int handle);

/* ---- animations. Server names are checked against the game's own tables, because a
 * name the game does not know makes it register a block that can never load. ---- */
unsigned cg_anim_block(const char *lib);      /* 0 when the game has no such file */
void cg_anim_request(const char *lib);
int  cg_anim_ready(unsigned block);
int  cg_anim_exists(unsigned block, const char *name);
void cg_anim_play(int handle, const char *name, const char *lib, float delta,
                  int loop, int lock_x, int lock_y, int freeze, int ms);

/* ---- camera and world ---- */
void cg_camera_at(const float *pos);
void cg_camera_look(const float *at, unsigned cut);
void cg_camera_behind(void);
void cg_camera_follow_ped(int handle, unsigned cut);
void cg_camera_follow_car(int handle, unsigned cut);
float cg_camera_fov(void);
void cg_set_time(unsigned hour, unsigned minute);
void cg_set_weather(unsigned id);

void cg_draw_wanted(unsigned level);
