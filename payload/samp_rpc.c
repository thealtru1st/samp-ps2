/* See samp_rpc.h. Wire layouts come from open.mp Shared/NetCode (MPL-2.0). */
#include "samp_rpc.h"

samp_rpc_stats_t samp_rpc_stats;

static const samp_rpc_handlers_t no_handlers;
static const samp_rpc_handlers_t *H = &no_handlers;

void samp_rpc_set_handlers(const samp_rpc_handlers_t *h) { H = h ? h : &no_handlers; }

const char *samp_rpc_name(unsigned id)
{
    switch (id) {
#define SRPC_NAME_(n, slot, arg) case n: return #slot;
    SRPC_TABLE(SRPC_NAME_)
    SRPC_SYNC_TABLE(SRPC_NAME_)
#undef SRPC_NAME_
    }
    return 0;
}

/* ---- reading --------------------------------------------------------------- */

static void rd_vec(bs_t *b, float *v, int n)
{
    int i;
    for (i = 0; i < n; i++)
        v[i] = bs_rf(b);
}

static void skip_bytes(bs_t *b, unsigned n)
{
    if (n > (unsigned)bs_unread(b) / 8)
        b->err = 1;
    else
        b->rd += n * 8;
}

/* n bytes from the stream into out, keeping at most cap - 1 of them. */
static void rd_chars(bs_t *b, char *out, unsigned cap, unsigned n)
{
    unsigned keep = n < cap ? n : cap - 1;

    out[0] = 0;
    if (n > (unsigned)bs_unread(b) / 8) {
        b->err = 1;
        return;
    }
    bs_rbytes(b, out, keep);
    out[keep] = 0;
    skip_bytes(b, n - keep);
}

static void rd_str8(bs_t *b, char *out, unsigned cap) { unsigned n = bs_r8(b); rd_chars(b, out, cap, n); }
static void rd_str16(bs_t *b, char *out, unsigned cap) { unsigned n = bs_r16(b); rd_chars(b, out, cap, n); }
static void rd_str32(bs_t *b, char *out, unsigned cap) { unsigned n = bs_r32(b); rd_chars(b, out, cap, n); }

static int rd_i16(bs_t *b)
{
    unsigned v = bs_r16(b);
    return v & 0x8000 ? (int)v - 0x10000 : (int)v;
}

static float root(float v)
{
    float r = 1.0f;
    int i;
    if (v <= 0.0f)
        return 0.0f;
    for (i = 0; i < 12; i++)
        r = 0.5f * (r + v / r);
    return r;
}

/* WriteNormQuat: four sign bits, then x, y, z magnitudes. w is derived. */
static void rd_norm_quat(bs_t *b, float *q)
{
    int sw = bs_rbool(b), sx = bs_rbool(b), sy = bs_rbool(b), sz = bs_rbool(b);

    q[1] = bs_r16(b) / 65535.0f;
    q[2] = bs_r16(b) / 65535.0f;
    q[3] = bs_r16(b) / 65535.0f;
    q[0] = root(1.0f - q[1] * q[1] - q[2] * q[2] - q[3] * q[3]);
    if (sw) q[0] = -q[0];
    if (sx) q[1] = -q[1];
    if (sy) q[2] = -q[2];
    if (sz) q[3] = -q[3];
}

/* writeCompressedVEC3: magnitude, then a unit vector as three 16-bit fractions. */
static void rd_comp_vec3(bs_t *b, float *v)
{
    float m = bs_rf(b);
    int i;

    for (i = 0; i < 3; i++)
        v[i] = 0.0f;
    if (m > 0.00001f)
        for (i = 0; i < 3; i++)
            v[i] = (bs_r16(b) / 32767.5f - 1.0f) * m;
}

/* writeCompressedPercentPair: two nibbles in steps of 7, 15 meaning 100 or more. */
static void rd_percent_pair(bs_t *b, unsigned *health, unsigned *armour)
{
    unsigned p = bs_r8(b), h = p >> 4, a = p & 15;

    *health = h == 15 ? 100 : h * 7;
    *armour = a == 15 ? 100 : a * 7;
}

static void rd_anim(bs_t *b, srpc_anim_t *a)
{
    rd_str8(b, a->lib, sizeof a->lib);
    rd_str8(b, a->name, sizeof a->name);
    a->delta = bs_rf(b);
    a->loop = bs_rbool(b);
    a->lock_x = bs_rbool(b);
    a->lock_y = bs_rbool(b);
    a->freeze = bs_rbool(b);
    a->time = bs_r32(b);
}

static void rd_material(bs_t *b, srpc_material_t *m)
{
    if (m->type == 1) {
        m->model = bs_r16(b);
        rd_str8(b, m->txd, sizeof m->txd);
        rd_str8(b, m->texture, sizeof m->texture);
        m->color = bs_r32(b);
    } else if (m->type == 2) {
        m->size = bs_r8(b);
        rd_str8(b, m->font, sizeof m->font);
        m->font_size = bs_r8(b);
        m->bold = bs_r8(b);
        m->font_color = bs_r32(b);
        m->back_color = bs_r32(b);
        m->align = bs_r8(b);
        samp_read_compressed_str(b, m->text, sizeof m->text);
    }
}

/* ---- Huffman ---------------------------------------------------------------
 * RakNet's StringCompressor. The tree is built the way
 * HuffmanEncodingTree::GenerateFromFrequencyTable builds it, ties included, so
 * every code matches the server's. Frequencies copied from
 * ref/openmp-network/Encoding/str_compress.cpp (RakNet, carried in open.mp's
 * network library), as any compatible client must. */
static const unsigned short huff_freq[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 722, 0, 0, 2, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    11084, 58, 63, 1, 0, 31, 0, 317, 64, 64, 44, 0, 695, 62, 980, 266,
    69, 67, 56, 7, 73, 3, 14, 2, 69, 1, 167, 9, 1, 2, 25, 94,
    0, 195, 139, 34, 96, 48, 103, 56, 125, 653, 21, 5, 23, 64, 85, 44,
    34, 7, 92, 76, 147, 12, 14, 57, 15, 39, 15, 1, 1, 1, 2, 3,
    0, 3611, 845, 1077, 1884, 5870, 841, 1057, 2501, 3212, 164, 531, 2019, 1330, 3056, 4037,
    848, 47, 2586, 2919, 4771, 1707, 535, 1106, 152, 1243, 100, 0, 2, 0, 10, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

#define HUFF_NODES 511
#define HUFF_ROOT  (HUFF_NODES - 1)

static short huff_child[HUFF_NODES - 256][2];
static short huff_parent[HUFF_NODES];
static unsigned huff_weight[HUFF_NODES];
static short huff_next[HUFF_NODES];
static short huff_head;
static int huff_ready;

static void huff_insert(int node)
{
    int prev = -1, cur = huff_head;

    while (cur >= 0 && huff_weight[cur] < huff_weight[node]) {
        prev = cur;
        cur = huff_next[cur];
    }
    huff_next[node] = cur;
    if (prev < 0)
        huff_head = node;
    else
        huff_next[prev] = node;
}

static void huff_init(void)
{
    int i, n = 256;

    if (huff_ready)
        return;
    huff_head = -1;
    for (i = 0; i < 256; i++) {
        huff_weight[i] = huff_freq[i] ? huff_freq[i] : 1;
        huff_insert(i);
    }
    while (n < HUFF_NODES) {
        int lo = huff_head, hi = huff_next[lo];
        huff_head = huff_next[hi];
        huff_child[n - 256][0] = lo;
        huff_child[n - 256][1] = hi;
        huff_parent[lo] = huff_parent[hi] = n;
        huff_weight[n] = huff_weight[lo] + huff_weight[hi];
        if (huff_head >= 0)
            huff_insert(n);
        n++;
    }
    huff_ready = 1;
}

unsigned samp_read_compressed_str(bs_t *b, char *out, unsigned cap)
{
    unsigned bits = bs_rcomp16(b), n = 0;
    int node = HUFF_ROOT;

    out[0] = 0;
    if (b->err)
        return 0;
    if (bits > (unsigned)bs_unread(b)) {
        b->err = 1;
        return 0;
    }
    huff_init();
    while (bits--) {
        int bit = (b->d[b->rd >> 3] >> (7 - (b->rd & 7))) & 1;
        b->rd++;
        node = huff_child[node - 256][bit];
        if (node < 256) {
            if (n + 1 < cap)
                out[n++] = (char)node;
            node = HUFF_ROOT;
        }
    }
    out[n] = 0;
    return n;
}

/* The code of symbol c, root first, one bit per byte. Returns its length. */
static int huff_code(int c, unsigned char *code)
{
    unsigned char rev[256];
    int n = 0, i, node = c;

    while (node != HUFF_ROOT) {
        int up = huff_parent[node];
        rev[n++] = huff_child[up - 256][1] == node;
        node = up;
    }
    for (i = 0; i < n; i++)
        code[i] = rev[n - 1 - i];
    return n;
}

void samp_write_compressed_str(bs_t *b, const char *s)
{
    static unsigned char tmp[SRPC_DIALOG_MAX * 2];
    unsigned char code[256];
    bs_t e;
    int i, n;

    huff_init();
    bs_wr_init(&e, tmp, sizeof tmp);
    for (; *s; s++) {
        n = huff_code((unsigned char)*s, code);
        for (i = 0; i < n; i++)
            bs_wbool(&e, code[i]);
    }
    /* Pad with the start of a longer code, so the padding decodes to nothing. */
    if (e.bits & 7) {
        int pad = 8 - (e.bits & 7), c;
        for (c = 0; c < 256; c++) {
            n = huff_code(c, code);
            if (n > pad) {
                for (i = 0; i < pad; i++)
                    bs_wbool(&e, code[i]);
                break;
            }
        }
    }
    bs_wcomp16(b, e.bits);
    bs_wbits(b, tmp, e.bits, 0);
}

/* ---- dispatch -------------------------------------------------------------- */

/* One buffer for whichever message is being decoded. */
static union {
    srpc_int_t i;
    srpc_float_t f;
    srpc_id_t id;
    srpc_vec3_t v3;
    srpc_id_float_t idf;
    srpc_id_vec3_t idv;
    srpc_id_val_t idval;
    srpc_init_game_t init;
    srpc_player_join_t join;
    srpc_player_quit_t quit;
    srpc_player_stream_in_t stream_in;
    srpc_set_name_t name;
    srpc_client_msg_t msg;
    srpc_chat_t chat;
    srpc_death_msg_t death;
    srpc_chat_bubble_t bubble;
    srpc_skill_t skill;
    srpc_id_anim_t anim;
    srpc_sound_t sound;
    srpc_time_t time;
    srpc_weapon_t weapon;
    srpc_audio_t audio;
    srpc_crime_t crime;
    srpc_remove_building_t building;
    srpc_explosion_t explosion;
    srpc_scm_event_t scm;
    srpc_game_text_t game_text;
    srpc_map_icon_t icon;
    srpc_nametag_t nametag;
    srpc_spectate_t spectate;
    srpc_client_check_t check;
    srpc_look_at_t look;
    srpc_camera_move_t camera;
    srpc_bounds_t bounds;
    srpc_score_t score;
    srpc_class_t cls;
    srpc_spawn_info_t spawn;
    srpc_checkpoint_t cp;
    srpc_race_checkpoint_t race;
    srpc_pickup_t pickup;
    srpc_gangzone_t zone;
    srpc_gangzone_flash_t zone_flash;
    srpc_label_t label;
    srpc_label_id_t label_id;
    srpc_textdraw_t td;
    srpc_textdraw_id_t td_id;
    srpc_textdraw_text_t td_text;
    srpc_textdraw_select_t td_select;
    srpc_dialog_t dialog;
    srpc_menu_t menu;
    srpc_object_t object;
    srpc_object_material_t object_material;
    srpc_object_move_t object_move;
    srpc_object_attach_player_t object_attach;
    srpc_attached_object_t attached;
    srpc_object_edit_t object_edit;
    srpc_vehicle_t vehicle;
    srpc_put_in_vehicle_t put;
    srpc_enter_vehicle_t enter;
    srpc_exit_vehicle_t exit;
    srpc_plate_t plate;
    srpc_vehicle_damage_t damage;
    srpc_component_t component;
    srpc_trailer_t trailer;
    srpc_vehicle_velocity_t car_vel;
    srpc_vehicle_params_old_t params_old;
    srpc_vehicle_params_t params;
    srpc_actor_t actor;
    srpc_foot_sync_t foot;
    srpc_vehicle_sync_t car_sync;
    srpc_passenger_sync_t passenger;
    srpc_aim_sync_t aim;
    srpc_bullet_sync_t bullet;
    srpc_unoccupied_sync_t unoccupied;
    srpc_trailer_sync_t trailer_sync;
    srpc_spectator_sync_t spectator;
    srpc_stats_sync_t stats;
    srpc_marker_t marker;
    char shop[33];
} u;

static void zero(void *p, unsigned n)
{
    unsigned char *c = p;
    while (n--)
        *c++ = 0;
}

static int ok(bs_t *b, unsigned id)
{
    if (b->err) {
        samp_rpc_stats.truncated++;
        samp_rpc_stats.last_bad = id;
        return 0;
    }
    samp_rpc_stats.decoded++;
    return 1;
}

#define EMIT(slot, arg)  do { if (ok(b, id) && H->slot) H->slot(arg); } while (0)
#define EMIT0(slot)      do { if (ok(b, id) && H->slot) H->slot(); } while (0)

static void unknown(unsigned id, bs_t *b)
{
    samp_rpc_stats.unknown++;
    if (H->unknown)
        H->unknown(id, b);
}

static void rd_init_game(bs_t *b, srpc_init_game_t *g)
{
    g->zone_names = bs_rbool(b);
    g->ped_anims = bs_rbool(b);
    g->interior_weapons = bs_rbool(b);
    g->use_chat_radius = bs_rbool(b);
    g->chat_radius = bs_rf(b);
    g->stunt_bonus = bs_rbool(b);
    g->nametag_distance = bs_rf(b);
    g->no_enex = bs_rbool(b);
    g->nametag_los = bs_rbool(b);
    g->manual_engine = bs_rbool(b);
    g->spawn_count = bs_r32(b);
    g->player_id = bs_r16(b);
    g->nametags = bs_rbool(b);
    g->markers = bs_r32(b);
    g->hour = bs_r8(b);
    g->weather = bs_r8(b);
    g->gravity = bs_rf(b);
    g->lan_mode = bs_rbool(b);
    g->death_drop = bs_r32(b);
    g->instagib = bs_rbool(b);
    g->foot_rate = bs_r32(b);
    g->car_rate = bs_r32(b);
    g->weapon_rate = bs_r32(b);
    g->multiplier = bs_r32(b);
    g->lag_comp = bs_r32(b);
    rd_str8(b, g->server_name, sizeof g->server_name);
    bs_rbytes(b, g->vehicle_models, SRPC_CAR_MODELS);
    g->vehicle_friendly_fire = bs_r32(b);
}

static void rd_spawn_tail(bs_t *b, float *spawn, float *angle, unsigned *weapons, unsigned *ammo)
{
    int i;
    rd_vec(b, spawn, 3);
    *angle = bs_rf(b);
    for (i = 0; i < 3; i++)
        weapons[i] = bs_r32(b);
    for (i = 0; i < 3; i++)
        ammo[i] = bs_r32(b);
}

static void rd_textdraw(bs_t *b, srpc_textdraw_t *t)
{
    unsigned id = bs_r16(b), flags = bs_r8(b);

    t->player = id >= SRPC_TD_GLOBALS;
    t->id = t->player ? id - SRPC_TD_GLOBALS : id;
    t->box = flags & 1;
    t->align = (flags >> 1) & 7;
    t->proportional = (flags >> 4) & 1;
    t->letter_w = bs_rf(b);
    t->letter_h = bs_rf(b);
    t->letter_color = bs_r32(b);
    t->text_w = bs_rf(b);
    t->text_h = bs_rf(b);
    t->box_color = bs_r32(b);
    t->shadow = bs_r8(b);
    t->outline = bs_r8(b);
    t->back_color = bs_r32(b);
    t->style = bs_r8(b);
    t->selectable = bs_r8(b);
    t->x = bs_rf(b);
    t->y = bs_rf(b);
    t->model = bs_r16(b);
    rd_vec(b, t->rot, 3);
    t->zoom = bs_rf(b);
    t->color1 = rd_i16(b);
    t->color2 = rd_i16(b);
    rd_str16(b, t->text, sizeof t->text);
}

/* A fixed-width field, terminated here because the sender need not. */
static void rd_fixed(bs_t *b, char *out)
{
    bs_rbytes(b, out, SRPC_MENU_LINE);
    out[SRPC_MENU_LINE] = 0;
}

static void rd_menu_column(bs_t *b, srpc_menu_t *m, int col)
{
    unsigned i, n;

    rd_fixed(b, m->header[col]);
    n = bs_r8(b);
    m->count[col] = n < SRPC_MENU_ROWS ? n : SRPC_MENU_ROWS;
    for (i = 0; i < n && !b->err; i++) {
        if (i < SRPC_MENU_ROWS)
            rd_fixed(b, m->items[col][i]);
        else
            skip_bytes(b, SRPC_MENU_LINE);
    }
}

static void rd_menu(bs_t *b, srpc_menu_t *m)
{
    int i;

    zero(m, sizeof *m);
    m->id = bs_r8(b);
    m->two_columns = bs_r32(b) != 0;
    rd_fixed(b, m->title);
    m->x = bs_rf(b);
    m->y = bs_rf(b);
    m->col1_width = bs_rf(b);
    if (m->two_columns)
        m->col2_width = bs_rf(b);
    m->enabled = bs_r32(b) != 0;
    for (i = 0; i < SRPC_MENU_ROWS; i++)
        m->row_enabled[i] = bs_r32(b) != 0;
    rd_menu_column(b, m, 0);
    if (m->two_columns)
        rd_menu_column(b, m, 1);
}

static void rd_object(bs_t *b, srpc_object_t *o)
{
    unsigned i, n;

    zero(o, sizeof *o);
    o->id = bs_r16(b);
    o->model = (int)bs_r32(b);
    rd_vec(b, o->pos, 3);
    rd_vec(b, o->rot, 3);
    o->distance = bs_rf(b);
    o->no_camera_col = bs_r8(b);
    o->attach_vehicle = bs_r16(b);
    o->attach_object = bs_r16(b);
    if (o->attach_vehicle != SRPC_INVALID_ID || o->attach_object != SRPC_INVALID_ID) {
        rd_vec(b, o->offset, 3);
        rd_vec(b, o->offset_rot, 3);
        o->sync_rotation = bs_r8(b);
    }
    n = bs_r8(b);
    o->material_count = n < SRPC_MAT_SLOTS ? n : SRPC_MAT_SLOTS;
    for (i = 0; i < n && !b->err; i++) {
        /* A count past the limit reuses the last entry, to keep walking the stream. */
        srpc_material_t *m = &o->materials[i < SRPC_MAT_SLOTS ? i : SRPC_MAT_SLOTS - 1];
        m->type = bs_r8(b);
        m->slot = bs_r8(b);
        rd_material(b, m);
    }
}

static void rd_vehicle(bs_t *b, srpc_vehicle_t *v)
{
    int i;

    v->id = bs_r16(b);
    v->model = bs_r32(b);
    rd_vec(b, v->pos, 3);
    v->angle = bs_rf(b);
    v->color1 = bs_r8(b);
    v->color2 = bs_r8(b);
    v->health = bs_rf(b);
    v->interior = bs_r8(b);
    v->door_damage = bs_r32(b);
    v->panel_damage = bs_r32(b);
    v->light_damage = bs_r8(b);
    v->tyre_damage = bs_r8(b);
    v->siren = bs_r8(b);
    for (i = 0; i < SRPC_CAR_MODS; i++) {
        unsigned m = bs_r8(b);
        v->mods[i] = m ? m + 999 : 0;
    }
    v->paintjob = bs_r8(b);
    v->body_color1 = (int)bs_r32(b);
    v->body_color2 = (int)bs_r32(b);
}

void samp_rpc_dispatch(unsigned id, bs_t *b)
{
    switch (id) {
    case SRPC_set_player_name:
        u.name.id = bs_r16(b);
        rd_str8(b, u.name.name, sizeof u.name.name);
        u.name.success = bs_r8(b);
        EMIT(set_player_name, &u.name);
        break;
    case SRPC_set_pos:          rd_vec(b, u.v3.v, 3); EMIT(set_pos, &u.v3); break;
    case SRPC_set_pos_find_z:   rd_vec(b, u.v3.v, 3); EMIT(set_pos_find_z, &u.v3); break;
    case SRPC_set_health:       u.f.v = bs_rf(b); EMIT(set_health, &u.f); break;
    case SRPC_toggle_controllable: u.i.v = bs_r8(b); EMIT(toggle_controllable, &u.i); break;
    case SRPC_play_sound:
        u.sound.sound = bs_r32(b);
        rd_vec(b, u.sound.pos, 3);
        EMIT(play_sound, &u.sound);
        break;
    case SRPC_set_world_bounds:
        u.bounds.max_x = bs_rf(b);
        u.bounds.min_x = bs_rf(b);
        u.bounds.max_y = bs_rf(b);
        u.bounds.min_y = bs_rf(b);
        EMIT(set_world_bounds, &u.bounds);
        break;
    case SRPC_give_money:       u.i.v = (int)bs_r32(b); EMIT(give_money, &u.i); break;
    case SRPC_set_facing_angle: u.f.v = bs_rf(b); EMIT(set_facing_angle, &u.f); break;
    case SRPC_reset_money:      EMIT0(reset_money); break;
    case SRPC_reset_weapons:    EMIT0(reset_weapons); break;
    case SRPC_give_weapon:
        u.weapon.weapon = bs_r32(b);
        u.weapon.ammo = bs_r32(b);
        EMIT(give_weapon, &u.weapon);
        break;
    case SRPC_set_vehicle_params: {
        int i;
        u.params.id = bs_r16(b);
        u.params.engine = bs_r8(b);
        u.params.lights = bs_r8(b);
        u.params.alarm = bs_r8(b);
        u.params.doors = bs_r8(b);
        u.params.bonnet = bs_r8(b);
        u.params.boot = bs_r8(b);
        u.params.objective = bs_r8(b);
        u.params.siren = bs_r8(b);
        for (i = 0; i < 4; i++)
            u.params.door[i] = bs_r8(b);
        for (i = 0; i < 4; i++)
            u.params.window[i] = bs_r8(b);
        EMIT(set_vehicle_params, &u.params);
        break;
    }
    case SRPC_enter_vehicle:
        u.enter.player = bs_r16(b);
        u.enter.vehicle = bs_r16(b);
        u.enter.passenger = bs_r8(b);
        EMIT(enter_vehicle, &u.enter);
        break;
    case SRPC_begin_object_select: EMIT0(begin_object_select); break;
    case SRPC_cancel_object_edit:  EMIT0(cancel_object_edit); break;
    case SRPC_set_time:
        u.time.hour = bs_r8(b);
        u.time.minute = bs_r8(b);
        EMIT(set_time, &u.time);
        break;
    case SRPC_toggle_clock:     u.i.v = bs_r8(b); EMIT(toggle_clock, &u.i); break;
    case SRPC_player_stream_in: {
        int i;
        u.stream_in.id = bs_r16(b);
        u.stream_in.team = bs_r8(b);
        u.stream_in.skin = bs_r32(b);
        rd_vec(b, u.stream_in.pos, 3);
        u.stream_in.angle = bs_rf(b);
        u.stream_in.color = bs_r32(b);
        u.stream_in.fight_style = bs_r8(b);
        for (i = 0; i < SRPC_SKILLS; i++)
            u.stream_in.skills[i] = bs_r16(b);
        EMIT(player_stream_in, &u.stream_in);
        break;
    }
    case SRPC_set_shop_name:
        bs_rbytes(b, u.shop, 32);
        u.shop[32] = 0;
        EMIT(set_shop_name, u.shop);
        break;
    case SRPC_set_skill_level:
        u.skill.id = bs_r16(b);
        u.skill.skill = bs_r32(b);
        u.skill.level = bs_r16(b);
        EMIT(set_skill_level, &u.skill);
        break;
    case SRPC_set_drunk_level:  u.i.v = (int)bs_r32(b); EMIT(set_drunk_level, &u.i); break;
    case SRPC_show_label: {
        unsigned wire = bs_r16(b);
        u.label.player = wire >= SRPC_LABEL_GLOBALS;
        u.label.id = u.label.player ? wire - SRPC_LABEL_GLOBALS : wire;
        u.label.color = bs_r32(b);
        rd_vec(b, u.label.pos, 3);
        u.label.distance = bs_rf(b);
        u.label.los = bs_r8(b);
        u.label.attach_player = bs_r16(b);
        u.label.attach_vehicle = bs_r16(b);
        samp_read_compressed_str(b, u.label.text, sizeof u.label.text);
        EMIT(show_label, &u.label);
        break;
    }
    case SRPC_disable_checkpoint: EMIT0(disable_checkpoint); break;
    case SRPC_set_race_checkpoint:
        u.race.type = bs_r8(b);
        rd_vec(b, u.race.pos, 3);
        rd_vec(b, u.race.next, 3);
        u.race.size = bs_rf(b);
        EMIT(set_race_checkpoint, &u.race);
        break;
    case SRPC_disable_race_checkpoint: EMIT0(disable_race_checkpoint); break;
    case SRPC_game_mode_restart: EMIT0(game_mode_restart); break;
    case SRPC_play_audio_stream:
        rd_str8(b, u.audio.url, sizeof u.audio.url);
        rd_vec(b, u.audio.pos, 3);
        u.audio.distance = bs_rf(b);
        u.audio.use_pos = bs_r8(b);
        EMIT(play_audio_stream, &u.audio);
        break;
    case SRPC_stop_audio_stream: EMIT0(stop_audio_stream); break;
    case SRPC_remove_building:
        u.building.model = bs_r32(b);
        rd_vec(b, u.building.pos, 3);
        u.building.radius = bs_rf(b);
        EMIT(remove_building, &u.building);
        break;
    case SRPC_create_object:    rd_object(b, &u.object); EMIT(create_object, &u.object); break;
    case SRPC_set_object_pos:
        u.idv.id = bs_r16(b);
        rd_vec(b, u.idv.v, 3);
        EMIT(set_object_pos, &u.idv);
        break;
    case SRPC_set_object_rot:
        u.idv.id = bs_r16(b);
        rd_vec(b, u.idv.v, 3);
        EMIT(set_object_rot, &u.idv);
        break;
    case SRPC_destroy_object:   u.id.id = bs_r16(b); EMIT(destroy_object, &u.id); break;
    case SRPC_death_message:
        u.death.killer = bs_r16(b);
        u.death.victim = bs_r16(b);
        u.death.reason = bs_r8(b);
        EMIT(death_message, &u.death);
        break;
    case SRPC_set_map_icon:
        u.icon.icon = bs_r8(b);
        rd_vec(b, u.icon.pos, 3);
        u.icon.type = bs_r8(b);
        u.icon.color = bs_r32(b);
        u.icon.style = bs_r8(b);
        EMIT(set_map_icon, &u.icon);
        break;
    case SRPC_remove_component:
        u.component.id = bs_r16(b);
        u.component.component = bs_r16(b);
        EMIT(remove_component, &u.component);
        break;
    case SRPC_hide_label: {
        unsigned wire = bs_r16(b);
        u.label_id.player = wire >= SRPC_LABEL_GLOBALS;
        u.label_id.id = u.label_id.player ? wire - SRPC_LABEL_GLOBALS : wire;
        EMIT(hide_label, &u.label_id);
        break;
    }
    case SRPC_chat_bubble:
        u.bubble.id = bs_r16(b);
        u.bubble.color = bs_r32(b);
        u.bubble.distance = bs_rf(b);
        u.bubble.expire = bs_r32(b);
        rd_str8(b, u.bubble.text, sizeof u.bubble.text);
        EMIT(chat_bubble, &u.bubble);
        break;
    case SRPC_game_time:        u.i.v = (int)bs_r32(b); EMIT(game_time, &u.i); break;
    case SRPC_show_dialog:
        u.dialog.id = rd_i16(b);
        u.dialog.style = bs_r8(b);
        rd_str8(b, u.dialog.title, sizeof u.dialog.title);
        rd_str8(b, u.dialog.button1, sizeof u.dialog.button1);
        rd_str8(b, u.dialog.button2, sizeof u.dialog.button2);
        samp_read_compressed_str(b, u.dialog.body, sizeof u.dialog.body);
        EMIT(show_dialog, &u.dialog);
        break;
    case SRPC_destroy_pickup:   u.i.v = (int)bs_r32(b); EMIT(destroy_pickup, &u.i); break;
    case SRPC_link_vehicle_interior:
        u.idval.id = bs_r16(b);
        u.idval.v = bs_r8(b);
        EMIT(link_vehicle_interior, &u.idval);
        break;
    case SRPC_set_armour:       u.f.v = bs_rf(b); EMIT(set_armour, &u.f); break;
    case SRPC_set_armed_weapon: u.id.id = bs_r32(b); EMIT(set_armed_weapon, &u.id); break;
    case SRPC_set_spawn_info:
        u.spawn.team = bs_r8(b);
        u.spawn.skin = bs_r32(b);
        u.spawn.unknown = bs_r8(b);
        rd_spawn_tail(b, u.spawn.spawn, &u.spawn.angle, u.spawn.weapons, u.spawn.ammo);
        EMIT(set_spawn_info, &u.spawn);
        break;
    case SRPC_set_team:
        u.idval.id = bs_r16(b);
        u.idval.v = bs_r8(b);
        EMIT(set_team, &u.idval);
        break;
    case SRPC_put_in_vehicle:
        u.put.vehicle = bs_r16(b);
        u.put.seat = bs_r8(b);
        EMIT(put_in_vehicle, &u.put);
        break;
    case SRPC_remove_from_vehicle: EMIT0(remove_from_vehicle); break;
    case SRPC_set_color:
        u.idval.id = bs_r16(b);
        u.idval.v = bs_r32(b);
        EMIT(set_color, &u.idval);
        break;
    case SRPC_game_text:
        u.game_text.style = bs_r32(b);
        u.game_text.time = bs_r32(b);
        rd_str32(b, u.game_text.text, sizeof u.game_text.text);
        EMIT(game_text, &u.game_text);
        break;
    case SRPC_force_class_selection: EMIT0(force_class_selection); break;
    case SRPC_attach_object_to_player:
        u.object_attach.object = bs_r16(b);
        u.object_attach.player = bs_r16(b);
        rd_vec(b, u.object_attach.offset, 3);
        rd_vec(b, u.object_attach.rot, 3);
        EMIT(attach_object_to_player, &u.object_attach);
        break;
    case SRPC_init_menu:        rd_menu(b, &u.menu); EMIT(init_menu, &u.menu); break;
    case SRPC_show_menu:        u.id.id = bs_r8(b); EMIT(show_menu, &u.id); break;
    case SRPC_hide_menu:        u.id.id = bs_r8(b); EMIT(hide_menu, &u.id); break;
    case SRPC_create_explosion:
        rd_vec(b, u.explosion.pos, 3);
        u.explosion.type = bs_r32(b);
        u.explosion.radius = bs_rf(b);
        EMIT(create_explosion, &u.explosion);
        break;
    case SRPC_show_nametag:
        u.nametag.id = bs_r16(b);
        u.nametag.show = bs_r8(b);
        EMIT(show_nametag, &u.nametag);
        break;
    case SRPC_attach_camera_to_object: u.id.id = bs_r16(b); EMIT(attach_camera_to_object, &u.id); break;
    case SRPC_interpolate_camera:
        u.camera.pos_set = bs_rbool(b);
        rd_vec(b, u.camera.from, 3);
        rd_vec(b, u.camera.to, 3);
        u.camera.time = bs_r32(b);
        u.camera.cut = bs_r8(b);
        EMIT(interpolate_camera, &u.camera);
        break;
    case SRPC_select_textdraw:
        u.td_select.enable = bs_rbool(b);
        u.td_select.color = bs_r32(b);
        EMIT(select_textdraw, &u.td_select);
        break;
    case SRPC_set_object_material:
        zero(&u.object_material, sizeof u.object_material);
        u.object_material.object = bs_r16(b);
        u.object_material.material.type = bs_r8(b);
        u.object_material.material.slot = bs_r8(b);
        rd_material(b, &u.object_material.material);
        EMIT(set_object_material, &u.object_material);
        break;
    case SRPC_stop_flash_gangzone: u.i.v = rd_i16(b); EMIT(stop_flash_gangzone, &u.i); break;
    case SRPC_apply_animation:
        u.anim.id = bs_r16(b);
        rd_anim(b, &u.anim.anim);
        EMIT(apply_animation, &u.anim);
        break;
    case SRPC_clear_animations: u.id.id = bs_r16(b); EMIT(clear_animations, &u.id); break;
    case SRPC_set_special_action: u.id.id = bs_r8(b); EMIT(set_special_action, &u.id); break;
    case SRPC_set_fighting_style:
        u.idval.id = bs_r16(b);
        u.idval.v = bs_r8(b);
        EMIT(set_fighting_style, &u.idval);
        break;
    case SRPC_set_velocity:     rd_vec(b, u.v3.v, 3); EMIT(set_velocity, &u.v3); break;
    case SRPC_set_vehicle_velocity:
        u.car_vel.type = bs_r8(b);
        rd_vec(b, u.car_vel.v, 3);
        EMIT(set_vehicle_velocity, &u.car_vel);
        break;
    case SRPC_set_drunk_visuals: u.i.v = (int)bs_r32(b); EMIT(set_drunk_visuals, &u.i); break;
    case SRPC_client_message:
        u.msg.color = bs_r32(b);
        rd_str32(b, u.msg.text, sizeof u.msg.text);
        EMIT(client_message, &u.msg);
        break;
    case SRPC_set_world_time:   u.id.id = bs_r8(b); EMIT(set_world_time, &u.id); break;
    case SRPC_create_pickup:
        u.pickup.id = (int)bs_r32(b);
        u.pickup.model = (int)bs_r32(b);
        u.pickup.type = (int)bs_r32(b);
        rd_vec(b, u.pickup.pos, 3);
        EMIT(create_pickup, &u.pickup);
        break;
    case SRPC_scm_event:
        u.scm.player = bs_r16(b);
        u.scm.event = bs_r32(b);
        u.scm.vehicle = bs_r32(b);
        u.scm.arg1 = bs_r32(b);
        u.scm.arg2 = bs_r32(b);
        EMIT(scm_event, &u.scm);
        break;
    case SRPC_set_tyre_status:
        u.idval.id = bs_r16(b);
        u.idval.v = bs_r8(b);
        EMIT(set_tyre_status, &u.idval);
        break;
    case SRPC_move_object:
        u.object_move.id = bs_r16(b);
        rd_vec(b, u.object_move.from, 3);
        rd_vec(b, u.object_move.to, 3);
        u.object_move.speed = bs_rf(b);
        rd_vec(b, u.object_move.rot, 3);
        EMIT(move_object, &u.object_move);
        break;
    case SRPC_chat:
        u.chat.id = bs_r16(b);
        rd_str8(b, u.chat.text, sizeof u.chat.text);
        EMIT(chat, &u.chat);
        break;
    case SRPC_client_check:
        u.check.type = bs_r8(b);
        u.check.address = bs_r32(b);
        u.check.offset = bs_r16(b);
        u.check.count = bs_r16(b);
        EMIT(client_check, &u.check);
        break;
    case SRPC_enable_stunt_bonus: u.i.v = bs_rbool(b); EMIT(enable_stunt_bonus, &u.i); break;
    case SRPC_set_textdraw_text: {
        unsigned wire = bs_r16(b);
        u.td_text.player = wire >= SRPC_TD_GLOBALS;
        u.td_text.id = u.td_text.player ? wire - SRPC_TD_GLOBALS : wire;
        rd_str16(b, u.td_text.text, sizeof u.td_text.text);
        EMIT(set_textdraw_text, &u.td_text);
        break;
    }
    case SRPC_set_vehicle_damage:
        u.damage.id = bs_r16(b);
        u.damage.panels = bs_r32(b);
        u.damage.doors = bs_r32(b);
        u.damage.lights = bs_r8(b);
        u.damage.tyres = bs_r8(b);
        EMIT(set_vehicle_damage, &u.damage);
        break;
    case SRPC_set_checkpoint:
        rd_vec(b, u.cp.pos, 3);
        u.cp.size = bs_rf(b);
        EMIT(set_checkpoint, &u.cp);
        break;
    case SRPC_show_gangzone:
        u.zone.id = rd_i16(b);
        u.zone.min_x = bs_rf(b);
        u.zone.min_y = bs_rf(b);
        u.zone.max_x = bs_rf(b);
        u.zone.max_y = bs_rf(b);
        u.zone.color = bs_r32(b);
        EMIT(show_gangzone, &u.zone);
        break;
    case SRPC_toggle_widescreen: u.i.v = bs_r8(b); EMIT(toggle_widescreen, &u.i); break;
    case SRPC_play_crime_report:
        u.crime.suspect = bs_r16(b);
        u.crime.in_vehicle = bs_r32(b);
        u.crime.model = bs_r32(b);
        u.crime.color = bs_r32(b);
        u.crime.crime = bs_r32(b);
        rd_vec(b, u.crime.pos, 3);
        EMIT(play_crime_report, &u.crime);
        break;
    case SRPC_set_attached_object:
        zero(&u.attached, sizeof u.attached);
        u.attached.player = bs_r16(b);
        u.attached.index = bs_r32(b);
        u.attached.create = bs_rbool(b);
        if (u.attached.create) {
            u.attached.model = bs_r32(b);
            u.attached.bone = bs_r32(b);
            rd_vec(b, u.attached.offset, 3);
            rd_vec(b, u.attached.rot, 3);
            rd_vec(b, u.attached.scale, 3);
            u.attached.color1 = bs_r32(b);
            u.attached.color2 = bs_r32(b);
        }
        EMIT(set_attached_object, &u.attached);
        break;
    case SRPC_edit_attached_object: u.id.id = bs_r32(b); EMIT(edit_attached_object, &u.id); break;
    case SRPC_edit_object:
        u.object_edit.player_object = bs_rbool(b);
        u.object_edit.object = bs_r16(b);
        EMIT(edit_object, &u.object_edit);
        break;
    case SRPC_hide_gangzone:    u.i.v = rd_i16(b); EMIT(hide_gangzone, &u.i); break;
    case SRPC_flash_gangzone:
        u.zone_flash.id = rd_i16(b);
        u.zone_flash.color = bs_r32(b);
        EMIT(flash_gangzone, &u.zone_flash);
        break;
    case SRPC_stop_object:      u.id.id = bs_r16(b); EMIT(stop_object, &u.id); break;
    case SRPC_set_plate:
        u.plate.id = bs_r16(b);
        rd_str8(b, u.plate.plate, sizeof u.plate.plate);
        EMIT(set_plate, &u.plate);
        break;
    case SRPC_toggle_spectating: u.i.v = bs_r32(b) != 0; EMIT(toggle_spectating, &u.i); break;
    case SRPC_spectate_player:
        u.spectate.id = bs_r16(b);
        u.spectate.mode = bs_r8(b);
        EMIT(spectate_player, &u.spectate);
        break;
    case SRPC_spectate_vehicle:
        u.spectate.id = bs_r16(b);
        u.spectate.mode = bs_r8(b);
        EMIT(spectate_vehicle, &u.spectate);
        break;
    case SRPC_request_class_reply:
        u.cls.selectable = bs_r8(b);
        u.cls.team = bs_r8(b);
        u.cls.skin = bs_r32(b);
        u.cls.unknown = bs_r8(b);
        rd_spawn_tail(b, u.cls.spawn, &u.cls.angle, u.cls.weapons, u.cls.ammo);
        EMIT(request_class_reply, &u.cls);
        break;
    case SRPC_request_spawn_reply: u.id.id = bs_r32(b); EMIT(request_spawn_reply, &u.id); break;
    case SRPC_connection_rejected: u.id.id = bs_r8(b); EMIT(connection_rejected, &u.id); break;
    case SRPC_set_wanted_level: u.id.id = bs_r8(b); EMIT(set_wanted_level, &u.id); break;
    case SRPC_show_textdraw:    rd_textdraw(b, &u.td); EMIT(show_textdraw, &u.td); break;
    case SRPC_hide_textdraw: {
        unsigned wire = bs_r16(b);
        u.td_id.player = wire >= SRPC_TD_GLOBALS;
        u.td_id.id = u.td_id.player ? wire - SRPC_TD_GLOBALS : wire;
        EMIT(hide_textdraw, &u.td_id);
        break;
    }
    case SRPC_player_join:
        u.join.id = bs_r16(b);
        u.join.color = bs_r32(b);
        u.join.npc = bs_r8(b);
        rd_str8(b, u.join.name, sizeof u.join.name);
        EMIT(player_join, &u.join);
        break;
    case SRPC_player_quit:
        u.quit.id = bs_r16(b);
        u.quit.reason = bs_r8(b);
        EMIT(player_quit, &u.quit);
        break;
    case SRPC_init_game:        rd_init_game(b, &u.init); EMIT(init_game, &u.init); break;
    case SRPC_remove_map_icon:  u.id.id = bs_r8(b); EMIT(remove_map_icon, &u.id); break;
    case SRPC_set_ammo:
        u.weapon.weapon = bs_r8(b);
        u.weapon.ammo = bs_r16(b);
        EMIT(set_ammo, &u.weapon);
        break;
    case SRPC_set_gravity:      u.f.v = bs_rf(b); EMIT(set_gravity, &u.f); break;
    case SRPC_set_vehicle_health:
        u.idf.id = bs_r16(b);
        u.idf.v = bs_rf(b);
        EMIT(set_vehicle_health, &u.idf);
        break;
    case SRPC_attach_trailer:
        u.trailer.trailer = bs_r16(b);
        u.trailer.vehicle = bs_r16(b);
        EMIT(attach_trailer, &u.trailer);
        break;
    case SRPC_detach_trailer:   u.id.id = bs_r16(b); EMIT(detach_trailer, &u.id); break;
    case SRPC_set_drunk_handling: u.i.v = (int)bs_r32(b); EMIT(set_drunk_handling, &u.i); break;
    case SRPC_destroy_weapon_pickup: u.id.id = bs_r8(b); EMIT(destroy_weapon_pickup, &u.id); break;
    case SRPC_set_weather:      u.id.id = bs_r8(b); EMIT(set_weather, &u.id); break;
    case SRPC_set_skin:
        u.idval.id = bs_r32(b);
        u.idval.v = bs_r32(b);
        EMIT(set_skin, &u.idval);
        break;
    case SRPC_exit_vehicle:
        u.exit.player = bs_r16(b);
        u.exit.vehicle = bs_r16(b);
        EMIT(exit_vehicle, &u.exit);
        break;
    case SRPC_score_and_ping:
        while (bs_unread(b) >= 80) {
            u.score.id = bs_r16(b);
            u.score.score = bs_r32(b);
            u.score.ping = bs_r32(b);
            if (H->score_and_ping)
                H->score_and_ping(&u.score);
        }
        ok(b, id);
        break;
    case SRPC_set_interior:     u.id.id = bs_r8(b); EMIT(set_interior, &u.id); break;
    case SRPC_set_camera_pos:   rd_vec(b, u.v3.v, 3); EMIT(set_camera_pos, &u.v3); break;
    case SRPC_set_camera_look_at:
        rd_vec(b, u.look.pos, 3);
        u.look.cut = bs_r8(b);
        EMIT(set_camera_look_at, &u.look);
        break;
    case SRPC_set_vehicle_pos:
        u.idv.id = bs_r16(b);
        rd_vec(b, u.idv.v, 3);
        EMIT(set_vehicle_pos, &u.idv);
        break;
    case SRPC_set_vehicle_angle:
        u.idf.id = bs_r16(b);
        u.idf.v = bs_rf(b);
        EMIT(set_vehicle_angle, &u.idf);
        break;
    case SRPC_set_vehicle_params_old:
        u.params_old.id = bs_r16(b);
        u.params_old.objective = bs_r8(b);
        u.params_old.doors = bs_r8(b);
        EMIT(set_vehicle_params_old, &u.params_old);
        break;
    case SRPC_set_camera_behind: EMIT0(set_camera_behind); break;
    case SRPC_player_stream_out: u.id.id = bs_r16(b); EMIT(player_stream_out, &u.id); break;
    case SRPC_vehicle_stream_in: rd_vehicle(b, &u.vehicle); EMIT(vehicle_stream_in, &u.vehicle); break;
    case SRPC_vehicle_stream_out: u.id.id = bs_r16(b); EMIT(vehicle_stream_out, &u.id); break;
    case SRPC_player_death:     u.id.id = bs_r16(b); EMIT(player_death, &u.id); break;
    case SRPC_disable_remote_vehicle_collisions:
        u.i.v = bs_rbool(b);
        EMIT(disable_remote_vehicle_collisions, &u.i);
        break;
    case SRPC_object_no_camera_col: u.id.id = bs_r16(b); EMIT(object_no_camera_col, &u.id); break;
    case SRPC_toggle_camera_target: u.i.v = bs_rbool(b); EMIT(toggle_camera_target, &u.i); break;
    case SRPC_actor_stream_in:
        u.actor.id = bs_r16(b);
        u.actor.skin = bs_r32(b);
        rd_vec(b, u.actor.pos, 3);
        u.actor.angle = bs_rf(b);
        u.actor.health = bs_rf(b);
        u.actor.invulnerable = bs_r8(b);
        EMIT(actor_stream_in, &u.actor);
        break;
    case SRPC_actor_stream_out: u.id.id = bs_r16(b); EMIT(actor_stream_out, &u.id); break;
    case SRPC_apply_actor_animation:
        u.anim.id = bs_r16(b);
        rd_anim(b, &u.anim.anim);
        EMIT(apply_actor_animation, &u.anim);
        break;
    case SRPC_clear_actor_animations: u.id.id = bs_r16(b); EMIT(clear_actor_animations, &u.id); break;
    case SRPC_set_actor_angle:
        u.idf.id = bs_r16(b);
        u.idf.v = bs_rf(b);
        EMIT(set_actor_angle, &u.idf);
        break;
    case SRPC_set_actor_pos:
        u.idv.id = bs_r16(b);
        rd_vec(b, u.idv.v, 3);
        EMIT(set_actor_pos, &u.idv);
        break;
    case SRPC_set_actor_health:
        u.idf.id = bs_r16(b);
        u.idf.v = bs_rf(b);
        EMIT(set_actor_health, &u.idf);
        break;
    default:
        unknown(id, b);
        break;
    }
}

/* A bit, then the value when the bit is set. */
static unsigned rd_opt16(bs_t *b) { return bs_rbool(b) ? bs_r16(b) : 0; }

static void rd_weapon_key(bs_t *b, unsigned *weapon, unsigned *key)
{
    unsigned v = bs_r8(b);
    *weapon = v & 63;
    *key = v >> 6;
}

static void rd_foot(bs_t *b, srpc_foot_sync_t *s)
{
    zero(s, sizeof *s);
    s->player = bs_r16(b);
    s->lr = rd_opt16(b);
    s->ud = rd_opt16(b);
    s->keys = bs_r16(b);
    rd_vec(b, s->pos, 3);
    rd_norm_quat(b, s->quat);
    rd_percent_pair(b, &s->health, &s->armour);
    rd_weapon_key(b, &s->weapon, &s->additional_key);
    s->special_action = bs_r8(b);
    rd_comp_vec3(b, s->velocity);
    s->surfing = bs_rbool(b);
    if (s->surfing) {
        s->surf_id = bs_r16(b);
        rd_vec(b, s->surf_offset, 3);
    }
    if (bs_rbool(b)) {
        s->anim_id = bs_r16(b);
        s->anim_flags = bs_r16(b);
    }
}

static void rd_vehicle_sync(bs_t *b, srpc_vehicle_sync_t *s)
{
    zero(s, sizeof *s);
    s->player = bs_r16(b);
    s->vehicle = bs_r16(b);
    s->lr = bs_r16(b);
    s->ud = bs_r16(b);
    s->keys = bs_r16(b);
    rd_norm_quat(b, s->quat);
    rd_vec(b, s->pos, 3);
    rd_comp_vec3(b, s->velocity);
    s->health = bs_r16(b);
    rd_percent_pair(b, &s->player_health, &s->player_armour);
    rd_weapon_key(b, &s->weapon, &s->additional_key);
    s->siren = bs_rbool(b);
    s->landing_gear = bs_rbool(b);
    if (bs_rbool(b))
        s->hydra_angle = bs_r32(b);
    s->has_trailer = bs_rbool(b);
    if (s->has_trailer)
        s->trailer = bs_r16(b);
}

void samp_sync_dispatch(unsigned id, bs_t *b)
{
    switch (id) {
    case SRPC_sync_foot:
        rd_foot(b, &u.foot);
        EMIT(sync_foot, &u.foot);
        break;
    case SRPC_sync_vehicle:
        rd_vehicle_sync(b, &u.car_sync);
        EMIT(sync_vehicle, &u.car_sync);
        break;
    case SRPC_sync_passenger: {
        srpc_passenger_sync_t *s = &u.passenger;
        s->player = bs_r16(b);
        s->vehicle = bs_r16(b);
        s->seat_flags = bs_r16(b);
        s->seat = s->seat_flags & 3;
        s->drive_by = (s->seat_flags >> 2) & 63;
        s->weapon = (s->seat_flags >> 8) & 63;
        s->additional_key = s->seat_flags >> 14;
        s->health = bs_r8(b);
        s->armour = bs_r8(b);
        s->lr = bs_r16(b);
        s->ud = bs_r16(b);
        s->keys = bs_r16(b);
        rd_vec(b, s->pos, 3);
        EMIT(sync_passenger, s);
        break;
    }
    case SRPC_sync_aim: {
        srpc_aim_sync_t *s = &u.aim;
        unsigned zoom;
        s->player = bs_r16(b);
        s->cam_mode = bs_r8(b);
        rd_vec(b, s->cam_front, 3);
        rd_vec(b, s->cam_pos, 3);
        s->aim_z = bs_rf(b);
        zoom = bs_r8(b);
        s->cam_zoom = zoom & 63;
        s->weapon_state = zoom >> 6;
        s->aspect = bs_r8(b);
        EMIT(sync_aim, s);
        break;
    }
    case SRPC_sync_bullet: {
        srpc_bullet_sync_t *s = &u.bullet;
        s->player = bs_r16(b);
        s->hit_type = bs_r8(b);
        s->hit_id = bs_r16(b);
        rd_vec(b, s->origin, 3);
        rd_vec(b, s->hit, 3);
        rd_vec(b, s->offset, 3);
        s->weapon = bs_r8(b);
        EMIT(sync_bullet, s);
        break;
    }
    case SRPC_sync_stats:
        u.stats.money = (int)bs_r32(b);
        u.stats.drunk = (int)bs_r32(b);
        EMIT(sync_stats, &u.stats);
        break;
    case SRPC_sync_marker: {
        unsigned n = bs_r32(b), i;
        for (i = 0; i < n && !b->err; i++) {
            u.marker.id = bs_r16(b);
            u.marker.active = bs_rbool(b);
            u.marker.x = u.marker.y = u.marker.z = 0;
            if (u.marker.active) {
                u.marker.x = rd_i16(b);
                u.marker.y = rd_i16(b);
                u.marker.z = rd_i16(b);
            }
            if (!b->err && H->sync_marker)
                H->sync_marker(&u.marker);
        }
        ok(b, id);
        break;
    }
    case SRPC_sync_unoccupied: {
        srpc_unoccupied_sync_t *s = &u.unoccupied;
        s->player = bs_r16(b);
        s->vehicle = bs_r16(b);
        s->seat = bs_r8(b);
        rd_vec(b, s->roll, 3);
        rd_vec(b, s->direction, 3);
        rd_vec(b, s->pos, 3);
        rd_vec(b, s->velocity, 3);
        rd_vec(b, s->turn, 3);
        s->health = bs_rf(b);
        EMIT(sync_unoccupied, s);
        break;
    }
    case SRPC_sync_trailer: {
        srpc_trailer_sync_t *s = &u.trailer_sync;
        s->player = bs_r16(b);
        s->vehicle = bs_r16(b);
        rd_vec(b, s->pos, 3);
        rd_vec(b, s->quat, 4);
        rd_vec(b, s->velocity, 3);
        rd_vec(b, s->turn, 3);
        EMIT(sync_trailer, s);
        break;
    }
    case SRPC_sync_spectator:
        u.spectator.lr = rd_opt16(b);
        u.spectator.ud = rd_opt16(b);
        u.spectator.keys = bs_r16(b);
        rd_vec(b, u.spectator.pos, 3);
        EMIT(sync_spectator, &u.spectator);
        break;
    default:
        unknown(id, b);
        break;
    }
}

/* ---- writing --------------------------------------------------------------- */

static unsigned slen(const char *s)
{
    unsigned n = 0;
    while (s[n])
        n++;
    return n;
}

static void w_vec(bs_t *b, const float *v, int n)
{
    int i;
    for (i = 0; i < n; i++)
        bs_wf(b, v[i]);
}

static void w_str8(bs_t *b, const char *s)
{
    unsigned n = slen(s);
    if (n > 255)
        n = 255;
    bs_w8(b, n);
    bs_wbytes(b, s, n);
}

static void w_str32(bs_t *b, const char *s)
{
    unsigned n = slen(s);
    bs_w32(b, n);
    bs_wbytes(b, s, n);
}

void srpc_enc_click_player(bs_t *b, unsigned player, unsigned source)
{
    bs_w16(b, player);
    bs_w8(b, source);
}

void srpc_enc_client_join(bs_t *b, unsigned version, unsigned modded, const char *name,
                          unsigned challenge_response, const char *serial, const char *version_text)
{
    bs_w32(b, version);
    bs_w8(b, modded);
    w_str8(b, name);
    bs_w32(b, challenge_response);
    w_str8(b, serial);
    w_str8(b, version_text);
}

void srpc_enc_enter_vehicle(bs_t *b, unsigned vehicle, int passenger)
{
    bs_w16(b, vehicle);
    bs_w8(b, passenger != 0);
}

void srpc_enc_select_object(bs_t *b, unsigned type, unsigned object, unsigned model, const float *pos)
{
    bs_w32(b, type);
    bs_w16(b, object);
    bs_w32(b, model);
    w_vec(b, pos, 3);
}

void srpc_enc_command(bs_t *b, const char *text) { w_str32(b, text); }

void srpc_enc_death(bs_t *b, unsigned reason, unsigned killer)
{
    bs_w8(b, reason);
    bs_w16(b, killer);
}

void srpc_enc_dialog_response(bs_t *b, int dialog, int response, int list_item, const char *text)
{
    bs_w16(b, (unsigned)dialog);
    bs_w8(b, (unsigned)response);
    bs_w16(b, (unsigned)list_item);
    w_str8(b, text);
}

void srpc_enc_click_textdraw(bs_t *b, unsigned wire_id) { bs_w16(b, wire_id); }

void srpc_enc_scm_event(bs_t *b, unsigned vehicle, unsigned arg1, unsigned arg2, unsigned event)
{
    bs_w32(b, vehicle);
    bs_w32(b, arg1);
    bs_w32(b, arg2);
    bs_w32(b, event);
}

void srpc_enc_chat(bs_t *b, const char *text) { w_str8(b, text); }

void srpc_enc_vehicle_damage(bs_t *b, unsigned vehicle, unsigned panels, unsigned doors,
                             unsigned lights, unsigned tyres)
{
    bs_w16(b, vehicle);
    bs_w32(b, panels);
    bs_w32(b, doors);
    bs_w8(b, lights);
    bs_w8(b, tyres);
}

void srpc_enc_give_take_damage(bs_t *b, int taking, unsigned player, float damage,
                               unsigned weapon, unsigned bodypart)
{
    bs_wbool(b, taking);
    bs_w16(b, player);
    bs_wf(b, damage);
    bs_w32(b, weapon);
    bs_w32(b, bodypart);
}

void srpc_enc_edit_attached_object(bs_t *b, unsigned response, unsigned index, unsigned model, unsigned bone,
                                   const float *offset, const float *rot, const float *scale,
                                   unsigned color1, unsigned color2)
{
    bs_w32(b, response);
    bs_w32(b, index);
    bs_w32(b, model);
    bs_w32(b, bone);
    w_vec(b, offset, 3);
    w_vec(b, rot, 3);
    w_vec(b, scale, 3);
    bs_w32(b, color1);
    bs_w32(b, color2);
}

void srpc_enc_edit_object(bs_t *b, int player_object, unsigned object, unsigned response,
                          const float *offset, const float *rot)
{
    bs_wbool(b, player_object);
    bs_w16(b, object);
    bs_w32(b, response);
    w_vec(b, offset, 3);
    w_vec(b, rot, 3);
}

void srpc_enc_interior_change(bs_t *b, unsigned interior) { bs_w8(b, interior); }
void srpc_enc_click_map(bs_t *b, const float *pos) { w_vec(b, pos, 3); }

/* 32 bits, as samp.c has always sent it; open.mp reads the low 16. */
void srpc_enc_request_class(bs_t *b, unsigned cls) { bs_w32(b, cls); }

void srpc_enc_picked_up_pickup(bs_t *b, int pickup) { bs_w32(b, (unsigned)pickup); }
void srpc_enc_menu_select(bs_t *b, unsigned row) { bs_w8(b, row); }
void srpc_enc_vehicle_destroyed(bs_t *b, unsigned vehicle) { bs_w16(b, vehicle); }
void srpc_enc_exit_vehicle(bs_t *b, unsigned vehicle) { bs_w16(b, vehicle); }

void srpc_enc_camera_target(bs_t *b, unsigned object, unsigned vehicle, unsigned player, unsigned actor)
{
    bs_w16(b, object);
    bs_w16(b, vehicle);
    bs_w16(b, player);
    bs_w16(b, actor);
}

void srpc_enc_actor_damage(bs_t *b, unsigned actor, float damage, unsigned weapon, unsigned bodypart)
{
    bs_wbool(b, 0);
    bs_w16(b, actor);
    bs_wf(b, damage);
    bs_w32(b, weapon);
    bs_w32(b, bodypart);
}

void srpc_enc_sync_foot(bs_t *b, const srpc_out_foot_t *s)
{
    bs_w8(b, SRPC_sync_foot);
    bs_w16(b, s->lr);
    bs_w16(b, s->ud);
    bs_w16(b, s->keys);
    w_vec(b, s->pos, 3);
    w_vec(b, s->quat, 4);
    bs_w8(b, s->health);
    bs_w8(b, s->armour);
    bs_w8(b, (s->weapon & 63) | (s->additional_key << 6));
    bs_w8(b, s->special_action);
    w_vec(b, s->velocity, 3);
    w_vec(b, s->surf_offset, 3);
    bs_w16(b, s->surf_id);
    bs_w16(b, s->anim_id);
    bs_w16(b, s->anim_flags);
}

void srpc_enc_sync_vehicle(bs_t *b, const srpc_out_vehicle_t *s)
{
    bs_w8(b, SRPC_sync_vehicle);
    bs_w16(b, s->vehicle);
    bs_w16(b, s->lr);
    bs_w16(b, s->ud);
    bs_w16(b, s->keys);
    w_vec(b, s->quat, 4);
    w_vec(b, s->pos, 3);
    w_vec(b, s->velocity, 3);
    bs_wf(b, s->health);
    bs_w8(b, s->player_health);
    bs_w8(b, s->player_armour);
    bs_w8(b, (s->weapon & 63) | (s->additional_key << 6));
    bs_w8(b, s->siren);
    bs_w8(b, s->landing_gear);
    bs_w16(b, s->trailer);
    bs_w32(b, s->hydra_angle);
}

void srpc_enc_sync_passenger(bs_t *b, const srpc_out_passenger_t *s)
{
    bs_w8(b, SRPC_sync_passenger);
    bs_w16(b, s->vehicle);
    bs_w16(b, s->seat_flags);
    bs_w8(b, s->health);
    bs_w8(b, s->armour);
    bs_w16(b, s->lr);
    bs_w16(b, s->ud);
    bs_w16(b, s->keys);
    w_vec(b, s->pos, 3);
}

void srpc_enc_sync_unoccupied(bs_t *b, const srpc_out_unoccupied_t *s)
{
    bs_w8(b, SRPC_sync_unoccupied);
    bs_w16(b, s->vehicle);
    bs_w8(b, s->seat);
    w_vec(b, s->roll, 3);
    w_vec(b, s->direction, 3);
    w_vec(b, s->pos, 3);
    w_vec(b, s->velocity, 3);
    w_vec(b, s->turn, 3);
    bs_wf(b, s->health);
}

void srpc_enc_sync_trailer(bs_t *b, const srpc_out_trailer_t *s)
{
    bs_w8(b, SRPC_sync_trailer);
    bs_w16(b, s->vehicle);
    w_vec(b, s->pos, 3);
    w_vec(b, s->quat, 4);
    w_vec(b, s->velocity, 3);
    w_vec(b, s->turn, 3);
}

void srpc_enc_sync_aim(bs_t *b, unsigned cam_mode, const float *cam_front, const float *cam_pos,
                       float aim_z, unsigned cam_zoom, unsigned weapon_state, unsigned aspect)
{
    bs_w8(b, SRPC_sync_aim);
    bs_w8(b, cam_mode);
    w_vec(b, cam_front, 3);
    w_vec(b, cam_pos, 3);
    bs_wf(b, aim_z);
    bs_w8(b, (cam_zoom & 63) | (weapon_state << 6));
    bs_w8(b, aspect);
}

void srpc_enc_sync_bullet(bs_t *b, unsigned hit_type, unsigned hit_id, const float *origin,
                          const float *hit, const float *offset, unsigned weapon)
{
    bs_w8(b, SRPC_sync_bullet);
    bs_w8(b, hit_type);
    bs_w16(b, hit_id);
    w_vec(b, origin, 3);
    w_vec(b, hit, 3);
    w_vec(b, offset, 3);
    bs_w8(b, weapon);
}

void srpc_enc_sync_stats(bs_t *b, int money, int drunk)
{
    bs_w8(b, SRPC_sync_stats);
    bs_w32(b, (unsigned)money);
    bs_w32(b, (unsigned)drunk);
}

void srpc_enc_sync_weapons(bs_t *b, unsigned target_player, unsigned target_actor,
                           const srpc_out_weapon_t *w, unsigned count)
{
    unsigned i;
    bs_w8(b, 204);
    bs_w16(b, target_player);
    bs_w16(b, target_actor);
    for (i = 0; i < count; i++) {
        bs_w8(b, w[i].slot);
        bs_w8(b, w[i].weapon);
        bs_w16(b, w[i].ammo);
    }
}

void srpc_enc_sync_spectator(bs_t *b, unsigned lr, unsigned ud, unsigned keys, const float *pos)
{
    bs_w8(b, SRPC_sync_spectator);
    bs_w16(b, lr);
    bs_w16(b, ud);
    bs_w16(b, keys);
    w_vec(b, pos, 3);
}

void srpc_enc_rcon(bs_t *b, const char *command)
{
    bs_w8(b, 201);
    w_str32(b, command);
}
