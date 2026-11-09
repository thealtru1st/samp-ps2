/* Live test client: payload/rak.c and samp_rpc.c against a real server.
 *
 *   rpc_live <ip> <port> <name> <seconds> <mode>
 *
 * Every message goes through samp_rpc_dispatch or samp_sync_dispatch. Each one
 * is then checked to have used exactly the bits the server sent, which catches a
 * wrong layout even where the values look plausible. Decoded fields are printed
 * as "EV <slot> ..." lines for live_check.py to compare.
 *
 * mode "plain" joins and stays quiet. mode "script" also runs steps(), which
 * drives wip/rpc/rpctest.pwn. mode "peer" is the second player for that script. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "rak.h"
#include "samp_rpc.h"

#define NETGAME_VERSION 4057
#define ID_RPC 20
#define ID_TIMESTAMP 40

static int sock;
static struct sockaddr_in srv;
static const char *my_name, *mode;
static unsigned my_id;
static float spawn[3];
static int spawned, in_car;
static unsigned spawned_at, now;

static unsigned seen_rpc[256], seen_sync[256], rpc_bad, sync_bad;

/* Ids the server handed out, for steps() to echo back. */
static unsigned peer_id = SRPC_INVALID_ID, player_object, global_object;
static int global_pickup = -1, player_pickup = -1;
static unsigned foot_weapon = 4;
static int stop_foot;

void slog(const char *f, ...)
{
    va_list ap;
    va_start(ap, f); vprintf(f, ap); va_end(ap);
    putchar('\n');
    fflush(stdout);
}

static void send_raw(const unsigned char *d, unsigned n)
{
    sendto(sock, d, n, 0, (struct sockaddr *)&srv, sizeof srv);
}

/* ---- join ------------------------------------------------------------------ */

/* Same scheme as make_serial in payload/samp.c: hex, divisible by 1001. */
static void make_serial(char *out, const char *name)
{
    unsigned w[5] = {0x53414D50, 0x50533221, 0x2F2F2F2F, 0x12345678, 0};
    unsigned long long carry = 0;
    int i, n = 0, started = 0;

    for (i = 0; name[i]; i++)
        w[i & 3] = (w[i & 3] ^ (unsigned char)name[i]) * 0x01000193u;
    for (i = 0; i < 5; i++) {
        unsigned long long v = (unsigned long long)w[i] * 1001 + carry;
        w[i] = (unsigned)v;
        carry = v >> 32;
    }
    for (i = 39; i >= 0; i--) {
        unsigned nib = (w[i >> 3] >> ((i & 7) * 4)) & 15;
        if (nib || started) {
            out[n++] = "0123456789ABCDEF"[nib];
            started = 1;
        }
    }
    out[n] = 0;
}

static unsigned char ob[1024];
static bs_t ow;
#define OUT(call) do { bs_t *b = &ow; bs_wr_init(b, ob, sizeof ob); call; } while (0)
static void rpc_out(int id) { rak_rpc(id, ob, ow.bits, RAK_RELIABLE, 0); }
static void sync_out(void) { rak_send(ob, ow.bits, RAK_UNRELIABLE_SEQ, 0); }

static void on_connected(unsigned token, unsigned idx)
{
    char serial[48];
    make_serial(serial, my_name);
    OUT(srpc_enc_client_join(b, NETGAME_VERSION, 1, my_name, token ^ NETGAME_VERSION, serial, "0.3.7"));
    rpc_out(SRPC_C2S_CLIENT_JOIN);
}

static void on_closed(int reason) { printf("closed: reason %d\n", reason); }

/* ---- printing handlers ------------------------------------------------------ */

#define V3 "%.2f %.2f %.2f"
#define A3(v) (v)[0], (v)[1], (v)[2]
#define P(slot, T, f, ...) static void h_##slot(const T *x) { printf("EV " #slot " " f "\n", __VA_ARGS__); }
#define P0(slot) static void h_##slot(void) { printf("EV " #slot "\n"); }

P(set_player_name, srpc_set_name_t, "%u %s %u", x->id, x->name, x->success)
P(set_pos, srpc_vec3_t, V3, A3(x->v))
P(set_pos_find_z, srpc_vec3_t, V3, A3(x->v))
P(set_health, srpc_float_t, "%.2f", x->v)
P(toggle_controllable, srpc_int_t, "%d", x->v)
P(play_sound, srpc_sound_t, "%u " V3, x->sound, A3(x->pos))
P(set_world_bounds, srpc_bounds_t, "%.2f %.2f %.2f %.2f", x->max_x, x->min_x, x->max_y, x->min_y)
P(give_money, srpc_int_t, "%d", x->v)
P(set_facing_angle, srpc_float_t, "%.2f", x->v)
P0(reset_money)
P0(reset_weapons)
P(give_weapon, srpc_weapon_t, "%u %u", x->weapon, x->ammo)
P(set_vehicle_params, srpc_vehicle_params_t, "%u %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d", x->id, x->engine, x->lights,
  x->alarm, x->doors, x->bonnet, x->boot, x->objective, x->siren, x->door[0], x->door[1], x->door[2], x->door[3],
  x->window[0], x->window[1], x->window[2], x->window[3])
P(enter_vehicle, srpc_enter_vehicle_t, "%u %u %u", x->player, x->vehicle, x->passenger)
P0(begin_object_select)
P0(cancel_object_edit)
P(set_time, srpc_time_t, "%u %u", x->hour, x->minute)
P(toggle_clock, srpc_int_t, "%d", x->v)
static void h_player_stream_in(const srpc_player_stream_in_t *x)
{
    peer_id = x->id;
    printf("EV player_stream_in %u %u %u " V3 " %.2f %08X %u %u %u\n", x->id, x->team, x->skin, A3(x->pos), x->angle,
           x->color, x->fight_style, x->skills[0], x->skills[10]);
}
static void h_set_shop_name(const char *s) { printf("EV set_shop_name %s\n", s); }
P(set_skill_level, srpc_skill_t, "%u %u %u", x->id, x->skill, x->level)
P(set_drunk_level, srpc_int_t, "%d", x->v)
P(show_label, srpc_label_t, "%d %u %08X " V3 " %.2f %d %u %u %s", x->player, x->id, x->color, A3(x->pos), x->distance, x->los,
  x->attach_player, x->attach_vehicle, x->text)
P0(disable_checkpoint)
P(set_race_checkpoint, srpc_race_checkpoint_t, "%u " V3 " " V3 " %.2f", x->type, A3(x->pos), A3(x->next), x->size)
P0(disable_race_checkpoint)
P0(game_mode_restart)
P(play_audio_stream, srpc_audio_t, "%s " V3 " %.2f %d", x->url, A3(x->pos), x->distance, x->use_pos)
P0(stop_audio_stream)
P(remove_building, srpc_remove_building_t, "%u " V3 " %.2f", x->model, A3(x->pos), x->radius)
static void h_create_object(const srpc_object_t *x)
{
    unsigned i;
    if (x->model == 1339) player_object = x->id;
    if (x->model == 1337) global_object = x->id;
    printf("EV create_object %u %d " V3 " " V3 " %.2f %d %u %u %u", x->id, x->model, A3(x->pos), A3(x->rot), x->distance,
           x->no_camera_col, x->attach_vehicle, x->attach_object, x->material_count);
    for (i = 0; i < x->material_count; i++) {
        const srpc_material_t *m = &x->materials[i];
        if (m->type == 1)
            printf(" [tex %u %u %s %s %08X]", m->slot, m->model, m->txd, m->texture, m->color);
        else
            printf(" [text %u %u %s %u %u %08X %08X %u %s]", m->slot, m->size, m->font, m->font_size, m->bold, m->font_color,
                   m->back_color, m->align, m->text);
    }
    putchar('\n');
}
P(set_object_pos, srpc_id_vec3_t, "%u " V3, x->id, A3(x->v))
P(set_object_rot, srpc_id_vec3_t, "%u " V3, x->id, A3(x->v))
P(destroy_object, srpc_id_t, "%u", x->id)
P(death_message, srpc_death_msg_t, "%u %u %u", x->killer, x->victim, x->reason)
P(set_map_icon, srpc_map_icon_t, "%u " V3 " %u %08X %u", x->icon, A3(x->pos), x->type, x->color, x->style)
P(remove_component, srpc_component_t, "%u %u", x->id, x->component)
P(hide_label, srpc_label_id_t, "%d %u", x->player, x->id)
P(chat_bubble, srpc_chat_bubble_t, "%u %08X %.2f %u %s", x->id, x->color, x->distance, x->expire, x->text)
P(game_time, srpc_int_t, "%s", "seen")
static void h_show_dialog(const srpc_dialog_t *x)
{
    printf("EV show_dialog %d %u [%s] [%s] [%s] [%s]\n", x->id, x->style, x->title, x->button1, x->button2, x->body);
}
P(destroy_pickup, srpc_int_t, "%d", x->v)
P(link_vehicle_interior, srpc_id_val_t, "%u %u", x->id, x->v)
P(set_armour, srpc_float_t, "%.2f", x->v)
P(set_armed_weapon, srpc_id_t, "%u", x->id)
P(set_spawn_info, srpc_spawn_info_t, "%u %u " V3 " %.2f %u %u %u %u %u %u", x->team, x->skin, A3(x->spawn), x->angle,
  x->weapons[0], x->ammo[0], x->weapons[1], x->ammo[1], x->weapons[2], x->ammo[2])
P(set_team, srpc_id_val_t, "%u %u", x->id, x->v)
static void h_put_in_vehicle(const srpc_put_in_vehicle_t *x)
{
    in_car = 1;
    printf("EV put_in_vehicle %u %u\n", x->vehicle, x->seat);
}
static void h_remove_from_vehicle(void)
{
    in_car = 0;
    printf("EV remove_from_vehicle\n");
}
P(set_color, srpc_id_val_t, "%u %08X", x->id, x->v)
P(game_text, srpc_game_text_t, "%u %u %s", x->style, x->time, x->text)
P0(force_class_selection)
P(attach_object_to_player, srpc_object_attach_player_t, "%u %u " V3 " " V3, x->object, x->player, A3(x->offset), A3(x->rot))
static void h_init_menu(const srpc_menu_t *x)
{
    printf("EV init_menu %u %d [%s] %.2f %.2f %.2f %.2f %d %d %d [%s] %u [%s] [%s] [%s] %u [%s]\n", x->id, x->two_columns, x->title,
           x->x, x->y, x->col1_width, x->col2_width, x->enabled, x->row_enabled[0], x->row_enabled[1], x->header[0], x->count[0],
           x->items[0][0], x->items[0][1], x->header[1], x->count[1], x->items[1][0]);
}
P(show_menu, srpc_id_t, "%u", x->id)
P(hide_menu, srpc_id_t, "%u", x->id)
P(create_explosion, srpc_explosion_t, V3 " %u %.2f", A3(x->pos), x->type, x->radius)
P(show_nametag, srpc_nametag_t, "%u %d", x->id, x->show)
P(attach_camera_to_object, srpc_id_t, "%u", x->id)
P(interpolate_camera, srpc_camera_move_t, "%d " V3 " " V3 " %u %u", x->pos_set, A3(x->from), A3(x->to), x->time, x->cut)
P(select_textdraw, srpc_textdraw_select_t, "%d %08X", x->enable, x->color)
static void h_set_object_material(const srpc_object_material_t *x)
{
    const srpc_material_t *m = &x->material;
    if (m->type == 1)
        printf("EV set_object_material %u tex %u %u %s %s %08X\n", x->object, m->slot, m->model, m->txd, m->texture, m->color);
    else
        printf("EV set_object_material %u text %u %u %s %u %u %08X %08X %u %s\n", x->object, m->slot, m->size, m->font, m->font_size,
               m->bold, m->font_color, m->back_color, m->align, m->text);
}
P(stop_flash_gangzone, srpc_int_t, "%d", x->v)
P(apply_animation, srpc_id_anim_t, "%u %s %s %.2f %d %d %d %d %u", x->id, x->anim.lib, x->anim.name, x->anim.delta, x->anim.loop,
  x->anim.lock_x, x->anim.lock_y, x->anim.freeze, x->anim.time)
P(clear_animations, srpc_id_t, "%u", x->id)
P(set_special_action, srpc_id_t, "%u", x->id)
P(set_fighting_style, srpc_id_val_t, "%u %u", x->id, x->v)
P(set_velocity, srpc_vec3_t, V3, A3(x->v))
P(set_vehicle_velocity, srpc_vehicle_velocity_t, "%u " V3, x->type, A3(x->v))
P(set_drunk_visuals, srpc_int_t, "%d", x->v)
P(set_world_time, srpc_id_t, "%u", x->id)
static void h_create_pickup(const srpc_pickup_t *x)
{
    if (x->model == 1240) global_pickup = x->id;
    if (x->model == 1274) player_pickup = x->id;
    printf("EV create_pickup %d %d %d " V3 "\n", x->id, x->model, x->type, A3(x->pos));
}
P(scm_event, srpc_scm_event_t, "%u %u %u %u %u", x->player, x->event, x->vehicle, x->arg1, x->arg2)
P(set_tyre_status, srpc_id_val_t, "%u %u", x->id, x->v)
P(move_object, srpc_object_move_t, "%u " V3 " " V3 " %.2f " V3, x->id, A3(x->from), A3(x->to), x->speed, A3(x->rot))
P(chat, srpc_chat_t, "%u %s", x->id, x->text)
P(client_check, srpc_client_check_t, "%u %08X %u %u", x->type, x->address, x->offset, x->count)
P(enable_stunt_bonus, srpc_int_t, "%d", x->v)
P(set_textdraw_text, srpc_textdraw_text_t, "%d %u %s", x->player, x->id, x->text)
P(set_vehicle_damage, srpc_vehicle_damage_t, "%u %u %u %u %u", x->id, x->panels, x->doors, x->lights, x->tyres)
P(set_checkpoint, srpc_checkpoint_t, V3 " %.2f", A3(x->pos), x->size)
P(show_gangzone, srpc_gangzone_t, "%d %.2f %.2f %.2f %.2f %08X", x->id, x->min_x, x->min_y, x->max_x, x->max_y, x->color)
P(toggle_widescreen, srpc_int_t, "%d", x->v)
P(play_crime_report, srpc_crime_t, "%u %u %u %u %u " V3, x->suspect, x->in_vehicle, x->model, x->color, x->crime, A3(x->pos))
P(set_attached_object, srpc_attached_object_t, "%u %u %d %u %u " V3 " " V3 " " V3 " %08X %08X", x->player, x->index, x->create, x->model,
  x->bone, A3(x->offset), A3(x->rot), A3(x->scale), x->color1, x->color2)
P(edit_attached_object, srpc_id_t, "%u", x->id)
P(edit_object, srpc_object_edit_t, "%d %u", x->player_object, x->object)
P(hide_gangzone, srpc_int_t, "%d", x->v)
P(flash_gangzone, srpc_gangzone_flash_t, "%d %08X", x->id, x->color)
P(stop_object, srpc_id_t, "%u", x->id)
P(set_plate, srpc_plate_t, "%u %s", x->id, x->plate)
P(toggle_spectating, srpc_int_t, "%d", x->v)
P(spectate_player, srpc_spectate_t, "%u %u", x->id, x->mode)
P(spectate_vehicle, srpc_spectate_t, "%u %u", x->id, x->mode)
P(connection_rejected, srpc_id_t, "%u", x->id)
P(set_wanted_level, srpc_id_t, "%u", x->id)
static void h_show_textdraw(const srpc_textdraw_t *x)
{
    printf("EV show_textdraw %d %u %d %u %d %.2f %.2f %08X %.2f %.2f %08X %u %u %08X %u %d %.2f %.2f %u " V3 " %.2f %d %d %s\n",
           x->player, x->id, x->box, x->align, x->proportional, x->letter_w, x->letter_h, x->letter_color, x->text_w, x->text_h,
           x->box_color, x->shadow, x->outline, x->back_color, x->style, x->selectable, x->x, x->y, x->model, A3(x->rot), x->zoom,
           x->color1, x->color2, x->text);
}
P(hide_textdraw, srpc_textdraw_id_t, "%d %u", x->player, x->id)
static void h_player_join(const srpc_player_join_t *x)
{
    printf("EV player_join %u %08X %d %s\n", x->id, x->color, x->npc, x->name);
}
P(player_quit, srpc_player_quit_t, "%u %u", x->id, x->reason)
P(remove_map_icon, srpc_id_t, "%u", x->id)
P(set_ammo, srpc_weapon_t, "%u %u", x->weapon, x->ammo)
P(set_gravity, srpc_float_t, "%.4f", x->v)
P(set_vehicle_health, srpc_id_float_t, "%u %.2f", x->id, x->v)
P(attach_trailer, srpc_trailer_t, "%u %u", x->trailer, x->vehicle)
P(detach_trailer, srpc_id_t, "%u", x->id)
P(set_drunk_handling, srpc_int_t, "%d", x->v)
P(destroy_weapon_pickup, srpc_id_t, "%u", x->id)
P(set_weather, srpc_id_t, "%u", x->id)
P(set_skin, srpc_id_val_t, "%u %u", x->id, x->v)
P(exit_vehicle, srpc_exit_vehicle_t, "%u %u", x->player, x->vehicle)
P(score_and_ping, srpc_score_t, "%u %d", x->id, (int)x->score)
P(set_interior, srpc_id_t, "%u", x->id)
P(set_camera_pos, srpc_vec3_t, V3, A3(x->v))
P(set_camera_look_at, srpc_look_at_t, V3 " %u", A3(x->pos), x->cut)
P(set_vehicle_pos, srpc_id_vec3_t, "%u " V3, x->id, A3(x->v))
P(set_vehicle_angle, srpc_id_float_t, "%u %.2f", x->id, x->v)
P(set_vehicle_params_old, srpc_vehicle_params_old_t, "%u %u %u", x->id, x->objective, x->doors)
P0(set_camera_behind)
P(player_stream_out, srpc_id_t, "%u", x->id)
P(vehicle_stream_in, srpc_vehicle_t, "%u %u " V3 " %.2f %u %u %.2f %u %u %u %u %u %u %u %u %d %d", x->id, x->model, A3(x->pos), x->angle,
  x->color1, x->color2, x->health, x->interior, x->door_damage, x->panel_damage, x->light_damage, x->tyre_damage, x->siren,
  x->mods[0], x->paintjob, x->body_color1, x->body_color2)
P(vehicle_stream_out, srpc_id_t, "%u", x->id)
P(player_death, srpc_id_t, "%u", x->id)
P(disable_remote_vehicle_collisions, srpc_int_t, "%d", x->v)
P(object_no_camera_col, srpc_id_t, "%u", x->id)
P(toggle_camera_target, srpc_int_t, "%d", x->v)
P(actor_stream_in, srpc_actor_t, "%u %u " V3 " %.2f %.2f %d", x->id, x->skin, A3(x->pos), x->angle, x->health, x->invulnerable)
P(actor_stream_out, srpc_id_t, "%u", x->id)
P(apply_actor_animation, srpc_id_anim_t, "%u %s %s %.2f %d %d %d %d %u", x->id, x->anim.lib, x->anim.name, x->anim.delta, x->anim.loop,
  x->anim.lock_x, x->anim.lock_y, x->anim.freeze, x->anim.time)
P(clear_actor_animations, srpc_id_t, "%u", x->id)
P(set_actor_angle, srpc_id_float_t, "%u %.2f", x->id, x->v)
P(set_actor_pos, srpc_id_vec3_t, "%u " V3, x->id, A3(x->v))
P(set_actor_health, srpc_id_float_t, "%u %.2f", x->id, x->v)

static unsigned foot_logged;
static void h_sync_foot(const srpc_foot_sync_t *x)
{
    if (foot_logged++ < 3 || x->anim_id)
        printf("EV sync_foot %u %u %u %u " V3 " q %.3f %.3f %.3f %.3f %u %u %u %u %u v " V3 " %d %u %u %u\n", x->player, x->lr, x->ud, x->keys,
               A3(x->pos), x->quat[0], x->quat[1], x->quat[2], x->quat[3], x->health, x->armour, x->weapon, x->additional_key,
               x->special_action, A3(x->velocity), x->surfing, x->surf_id, x->anim_id, x->anim_flags);
}
P(sync_vehicle, srpc_vehicle_sync_t, "%u %u %u %u %u q %.3f %.3f %.3f %.3f " V3 " v " V3 " %u %u %u %u %u %d %d %u %d %u", x->player, x->vehicle, x->lr,
  x->ud, x->keys, x->quat[0], x->quat[1], x->quat[2], x->quat[3], A3(x->pos), A3(x->velocity), x->health, x->player_health, x->player_armour,
  x->weapon, x->additional_key, x->siren, x->landing_gear, x->hydra_angle, x->has_trailer, x->trailer)
P(sync_passenger, srpc_passenger_sync_t, "%u %u %u %u %u %u %u %u %u %u %u " V3, x->player, x->vehicle, x->seat, x->drive_by, x->weapon,
  x->additional_key, x->health, x->armour, x->lr, x->ud, x->keys, A3(x->pos))
P(sync_aim, srpc_aim_sync_t, "%u %u " V3 " " V3 " %.2f %u %u %u", x->player, x->cam_mode, A3(x->cam_front), A3(x->cam_pos), x->aim_z, x->cam_zoom,
  x->weapon_state, x->aspect)
P(sync_bullet, srpc_bullet_sync_t, "%u %u %u " V3 " " V3 " " V3 " %u", x->player, x->hit_type, x->hit_id, A3(x->origin), A3(x->hit), A3(x->offset), x->weapon)
P(sync_unoccupied, srpc_unoccupied_sync_t, "%u %u %u " V3 " %.2f", x->player, x->vehicle, x->seat, A3(x->pos), x->health)
P(sync_trailer, srpc_trailer_sync_t, "%u %u " V3 " q %.3f %.3f %.3f %.3f", x->player, x->vehicle, A3(x->pos), x->quat[0], x->quat[1], x->quat[2], x->quat[3])
P(sync_stats, srpc_stats_sync_t, "%d %d", x->money, x->drunk)
P(sync_spectator, srpc_spectator_sync_t, "%u %u %u " V3, x->lr, x->ud, x->keys, A3(x->pos))
static void h_sync_marker(const srpc_marker_t *x) { printf("EV sync_marker %u %d %d %d %d\n", x->id, x->active, x->x, x->y, x->z); }

/* ---- join flow, also through the handler table ----------------------------- */

static void h_init_game(const srpc_init_game_t *g)
{
    my_id = g->player_id;
    printf("EV init_game id %u name [%s] rates %u %u %u hour %u weather %u gravity %.4f markers %u\n", g->player_id, g->server_name, g->foot_rate,
           g->car_rate, g->weapon_rate, g->hour, g->weather, g->gravity, g->markers);
    OUT(srpc_enc_request_class(b, 0));
    rpc_out(SRPC_C2S_REQUEST_CLASS);
}

static void h_class(const srpc_class_t *c)
{
    printf("EV request_class_reply %u %u %u " V3 " %.2f %u %u\n", c->selectable, c->team, c->skin, A3(c->spawn), c->angle, c->weapons[0], c->ammo[0]);
    spawn[0] = c->spawn[0]; spawn[1] = c->spawn[1]; spawn[2] = c->spawn[2];
    rak_rpc(SRPC_C2S_REQUEST_SPAWN, 0, 0, RAK_RELIABLE, 0);
}

static void h_spawn_reply(const srpc_id_t *a)
{
    printf("EV request_spawn_reply %u\n", a->id);
    if (a->id) {
        rak_rpc(SRPC_C2S_SPAWN, 0, 0, RAK_RELIABLE, 0);
        spawned = 1;
        spawned_at = now;
    }
}

static void steps(void);

static void h_client_message(const srpc_client_msg_t *m) { printf("EV client_message %08X %s\n", m->color, m->text); }

static const samp_rpc_handlers_t handlers = {
#define S(slot) .slot = h_##slot,
    S(set_player_name) S(set_pos) S(set_pos_find_z) S(set_health) S(toggle_controllable) S(play_sound) S(set_world_bounds)
    S(give_money) S(set_facing_angle) S(reset_money) S(reset_weapons) S(give_weapon) S(set_vehicle_params) S(enter_vehicle)
    S(begin_object_select) S(cancel_object_edit) S(set_time) S(toggle_clock) S(player_stream_in) S(set_shop_name)
    S(set_skill_level) S(set_drunk_level) S(show_label) S(disable_checkpoint) S(set_race_checkpoint) S(disable_race_checkpoint)
    S(game_mode_restart) S(play_audio_stream) S(stop_audio_stream) S(remove_building) S(create_object) S(set_object_pos)
    S(set_object_rot) S(destroy_object) S(death_message) S(set_map_icon) S(remove_component) S(hide_label) S(chat_bubble)
    S(game_time) S(show_dialog) S(destroy_pickup) S(link_vehicle_interior) S(set_armour) S(set_armed_weapon) S(set_spawn_info)
    S(set_team) S(put_in_vehicle) S(remove_from_vehicle) S(set_color) S(game_text) S(force_class_selection)
    S(attach_object_to_player) S(init_menu) S(show_menu) S(hide_menu) S(create_explosion) S(show_nametag)
    S(attach_camera_to_object) S(interpolate_camera) S(select_textdraw) S(set_object_material) S(stop_flash_gangzone)
    S(apply_animation) S(clear_animations) S(set_special_action) S(set_fighting_style) S(set_velocity) S(set_vehicle_velocity)
    S(set_drunk_visuals) S(client_message) S(set_world_time) S(create_pickup) S(scm_event) S(set_tyre_status) S(move_object)
    S(chat) S(client_check) S(enable_stunt_bonus) S(set_textdraw_text) S(set_vehicle_damage) S(set_checkpoint) S(show_gangzone)
    S(toggle_widescreen) S(play_crime_report) S(set_attached_object) S(edit_attached_object) S(edit_object) S(hide_gangzone)
    S(flash_gangzone) S(stop_object) S(set_plate) S(toggle_spectating) S(spectate_player) S(spectate_vehicle)
    S(connection_rejected) S(set_wanted_level) S(show_textdraw) S(hide_textdraw) S(player_join) S(player_quit) S(remove_map_icon)
    S(set_ammo) S(set_gravity) S(set_vehicle_health) S(attach_trailer) S(detach_trailer) S(set_drunk_handling)
    S(destroy_weapon_pickup) S(set_weather) S(set_skin) S(exit_vehicle) S(score_and_ping) S(set_interior) S(set_camera_pos)
    S(set_camera_look_at) S(set_vehicle_pos) S(set_vehicle_angle) S(set_vehicle_params_old) S(set_camera_behind)
    S(player_stream_out) S(vehicle_stream_in) S(vehicle_stream_out) S(player_death) S(disable_remote_vehicle_collisions)
    S(object_no_camera_col) S(toggle_camera_target) S(actor_stream_in) S(actor_stream_out) S(apply_actor_animation)
    S(clear_actor_animations) S(set_actor_angle) S(set_actor_pos) S(set_actor_health)
    S(sync_foot) S(sync_vehicle) S(sync_passenger) S(sync_aim) S(sync_bullet) S(sync_unoccupied) S(sync_trailer) S(sync_stats)
    S(sync_spectator) S(sync_marker)
#undef S
    .init_game = h_init_game,
    .request_class_reply = h_class,
    .request_spawn_reply = h_spawn_reply,
};

/* ---- packet entry ---------------------------------------------------------- */

static void on_packet(const unsigned char *d, unsigned bits)
{
    static unsigned char rpcbuf[65536];
    bs_t b;
    unsigned id;

    bs_rd_init(&b, d, bits);
    id = bs_r8(&b);
    if (id == ID_TIMESTAMP) {
        bs_r32(&b);
        id = bs_r8(&b);
    }
    if (id == ID_RPC) {
        unsigned rpc = bs_r8(&b), n = bs_rcomp32(&b);
        unsigned bad0 = samp_rpc_stats.truncated;
        bs_t r;
        if (b.err || BS_BYTES(n) > sizeof rpcbuf)
            return;
        if (n)
            bs_rbits(&b, rpcbuf, n, 0);
        bs_rd_init(&r, rpcbuf, n);
        seen_rpc[rpc]++;
        if (rpc == SRPC_show_dialog || rpc == SRPC_show_label) {
            unsigned i;
            printf("HEX %u %u ", rpc, n);
            for (i = 0; i < BS_BYTES(n); i++)
                printf("%02X", rpcbuf[i]);
            putchar('\n');
        }
        samp_rpc_dispatch(rpc, &r);
        /* The RPC length is exact, so every bit must be used. */
        if (samp_rpc_stats.truncated != bad0 || bs_unread(&r) != 0) {
            rpc_bad++;
            printf("BAD rpc %u %s: %u bits, %d unread, err %d\n", rpc, samp_rpc_name(rpc) ? samp_rpc_name(rpc) : "?", n, bs_unread(&r), r.err);
        }
        return;
    }
    {
        unsigned bad0 = samp_rpc_stats.truncated;
        seen_sync[id]++;
        samp_sync_dispatch(id, &b);
        /* A packet is padded to a byte. */
        if (samp_rpc_stats.truncated != bad0 || bs_unread(&b) >= 8) {
            sync_bad++;
            printf("BAD sync %u: %u bits, %d unread, err %d\n", id, bits, bs_unread(&b), b.err);
        }
    }
}

static const rak_cb_t callbacks = {send_raw, on_connected, on_packet, on_closed};

/* ---- steady state and scripted steps --------------------------------------- */

static unsigned last_sync;
static float foot_quat[4] = {0.8f, 0.2f, 0.4f, 0.4f};
static unsigned foot_anim;
static float pos_off;

static void send_foot(void)
{
    srpc_out_foot_t f;

    memset(&f, 0, sizeof f);
    f.pos[0] = spawn[0] + pos_off;
    f.pos[1] = spawn[1];
    f.pos[2] = spawn[2];
    memcpy(f.quat, foot_quat, sizeof f.quat);
    f.health = 77;
    f.armour = 33;
    f.weapon = foot_weapon;
    f.additional_key = 1;
    f.keys = 0x0408;
    f.lr = 0xFF80;
    f.ud = 0x0080;
    f.velocity[0] = 0.1f;
    f.velocity[1] = -0.2f;
    f.anim_id = foot_anim;
    f.anim_flags = foot_anim ? 0x4004 : 0;
    OUT(srpc_enc_sync_foot(b, &f));
    sync_out();
}

#include "live_steps.inc"

static unsigned now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000u + t.tv_nsec / 1000000u;
}

int main(int c, char **v)
{
    unsigned char buf[2048];
    unsigned start, secs;
    int i, n = 0;

    if (c < 6)
        return 2;
    my_name = v[3];
    secs = atoi(v[4]);
    mode = v[5];
    if (secs > 28)
        secs = 28;
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    srv.sin_family = AF_INET;
    srv.sin_port = htons(atoi(v[2]));
    inet_pton(AF_INET, v[1], &srv.sin_addr);
    samp_rpc_set_handlers(&handlers);
    rak_connect(ntohl(srv.sin_addr.s_addr), atoi(v[2]), "", &callbacks);
    now = start = now_ms();
    while (now - start < secs * 1000) {
        struct pollfd p = {sock, POLLIN, 0};
        if (poll(&p, 1, 5) > 0) {
            int len = recv(sock, buf, sizeof buf, 0);
            if (len > 0)
                rak_datagram(buf, len, now_ms());
        }
        now = now_ms();
        rak_tick(now);
        if (spawned && rak_state() == RAK_CONNECTED) {
            if (now - last_sync >= 100 && !stop_foot && !in_car) {
                last_sync = now;
                send_foot();
            }
            if (!strcmp(mode, "script"))
                steps();
            else if (!strcmp(mode, "peer"))
                peer_steps();
        }
    }
    rak_disconnect();
    usleep(300000);
    for (i = 0; i < 256; i++) {
        if (seen_rpc[i]) {
            printf("SEEN rpc %3d %-34s x%u\n", i, samp_rpc_name(i) ? samp_rpc_name(i) : "?", seen_rpc[i]);
            n++;
        }
        if (seen_sync[i])
            printf("SEEN sync %3d %-33s x%u\n", i, i >= 200 && samp_rpc_name(i) ? samp_rpc_name(i) : "?", seen_sync[i]);
    }
    printf("SUMMARY %d distinct rpcs, decoded %u, truncated %u, unknown %u, bad rpc %u, bad sync %u\n", n, samp_rpc_stats.decoded,
           samp_rpc_stats.truncated, samp_rpc_stats.unknown, rpc_bad, sync_bad);
    return 0;
}
