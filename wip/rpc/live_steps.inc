/* Scripted steps for mode "script", paired with rpctest.pwn. One step every
 * STEP_MS after spawning. live_expected.txt lists what each step must produce. */
#define STEP_MS 180

enum { STEP_BASE = __COUNTER__ + 1 };
#define STEP case __COUNTER__ - STEP_BASE:

static void cmd(const char *s)
{
    OUT(srpc_enc_command(b, s));
    rpc_out(SRPC_C2S_COMMAND);
}

static void send_vehicle(unsigned vehicle, unsigned trailer)
{
    srpc_out_vehicle_t s;

    memset(&s, 0, sizeof s);
    s.vehicle = vehicle;
    s.lr = 0xFF80;
    s.keys = 8;
    s.quat[0] = 0.8f; s.quat[1] = 0.2f; s.quat[2] = 0.4f; s.quat[3] = 0.4f;
    s.pos[0] = 105.0f; s.pos[1] = 200.0f; s.pos[2] = 10.5f;
    s.velocity[0] = 0.3f;
    s.health = 876.0f;
    s.player_health = 77;
    s.player_armour = 33;
    s.weapon = 24;
    s.additional_key = 1;
    s.siren = 1;
    s.trailer = trailer;
    OUT(srpc_enc_sync_vehicle(b, &s));
    sync_out();
}

/* The second player: seen by the first as a chatting, driving, dying player. */
static void peer_steps(void)
{
    static int step;
    static unsigned next;

    if (now < spawned_at + 4000 || now < next)
        return;
    next = now + 1500;
    switch (step++) {
    case 0: OUT(srpc_enc_chat(b, "peer chat")); rpc_out(SRPC_C2S_CHAT); break;
    case 1: OUT(srpc_enc_enter_vehicle(b, 2, 0)); rpc_out(SRPC_C2S_ENTER_VEHICLE); break;
    case 2: OUT(srpc_enc_exit_vehicle(b, 2)); rpc_out(SRPC_C2S_EXIT_VEHICLE); break;
    case 7: pos_off = 1500.0f; break;
    case 8: OUT(srpc_enc_death(b, 53, SRPC_INVALID_ID)); rpc_out(SRPC_C2S_DEATH); break;
    }
}

static void steps(void)
{
    static const float v123[3] = {1.0f, 2.0f, 3.0f}, v456[3] = {4.0f, 5.0f, 6.0f}, v789[3] = {7.0f, 8.0f, 9.0f};
    static const float near_car[3] = {105.0f, 200.0f, 10.0f};
    static int step;
    static unsigned next;

    if (now < spawned_at + 2500 || now < next)
        return;
    next = now + STEP_MS;
    switch (step++) {
    STEP cmd("/t1"); break;
    STEP cmd("/name"); break;
    STEP cmd("/t2"); break;
    STEP cmd("/t3"); break;
    STEP OUT(srpc_enc_dialog_response(b, 77, 1, 2, "Red $3000")); rpc_out(SRPC_C2S_DIALOG_RESPONSE); break;
    STEP OUT(srpc_enc_click_textdraw(b, 0)); rpc_out(SRPC_C2S_CLICK_TEXTDRAW); break;
    STEP OUT(srpc_enc_click_textdraw(b, SRPC_TD_GLOBALS)); rpc_out(SRPC_C2S_CLICK_TEXTDRAW); break;
    STEP OUT(srpc_enc_menu_select(b, 1)); rpc_out(SRPC_C2S_MENU_SELECT); break;
    STEP cmd("/menu"); break;
    STEP rak_rpc(SRPC_C2S_MENU_QUIT, 0, 0, RAK_RELIABLE, 0); break;
    STEP OUT(srpc_enc_chat(b, "hello chat")); rpc_out(SRPC_C2S_CHAT); break;
    STEP OUT(srpc_enc_click_map(b, v123)); rpc_out(SRPC_C2S_CLICK_MAP); break;
    STEP OUT(srpc_enc_click_player(b, peer_id, 0)); rpc_out(SRPC_C2S_CLICK_PLAYER); break;
    STEP OUT(srpc_enc_interior_change(b, 5)); rpc_out(SRPC_C2S_INTERIOR_CHANGE); break;
    STEP OUT(srpc_enc_give_take_damage(b, 1, SRPC_INVALID_ID, 5.5f, 54, 3)); rpc_out(SRPC_C2S_GIVE_TAKE_DAMAGE); break;
    STEP OUT(srpc_enc_give_take_damage(b, 0, peer_id, 6.5f, 24, 9)); rpc_out(SRPC_C2S_GIVE_TAKE_DAMAGE); break;
    STEP OUT(srpc_enc_actor_damage(b, 0, 7.5f, 24, 4)); rpc_out(SRPC_C2S_ACTOR_DAMAGE); break;
    STEP OUT(srpc_enc_picked_up_pickup(b, global_pickup)); rpc_out(SRPC_C2S_PICKED_UP_PICKUP); break;
    STEP cmd("/t4"); break;
    STEP
        /* Pickups stream on the server's stream_rate tick, up to a second away. */
        if (player_pickup < 0) {
            step--;
            break;
        }
        OUT(srpc_enc_picked_up_pickup(b, player_pickup));
        rpc_out(SRPC_C2S_PICKED_UP_PICKUP);
        break;
    STEP cmd("/editobj"); break;
    STEP OUT(srpc_enc_edit_object(b, 1, player_object, 1, v123, v456)); rpc_out(SRPC_C2S_EDIT_OBJECT); break;
    STEP cmd("/editatt"); break;
    STEP {
        float s[3] = {1.5f, 2.5f, 3.5f};
        OUT(srpc_enc_edit_attached_object(b, 1, 2, 1242, 2, v123, v456, s, 0x11223344, 0x55667788));
        rpc_out(SRPC_C2S_EDIT_ATTACHED_OBJECT);
        break;
    }
    STEP cmd("/selectobj"); break;
    STEP OUT(srpc_enc_select_object(b, 1, global_object, 1337, v789)); rpc_out(SRPC_C2S_SELECT_OBJECT); break;
    STEP cmd("/t5"); break;
    STEP cmd("/t6"); break;
    /* Vehicles. */
    STEP OUT(srpc_enc_enter_vehicle(b, 1, 0)); rpc_out(SRPC_C2S_ENTER_VEHICLE); break;
    STEP cmd("/putin"); break;
    STEP send_vehicle(1, 0); break;
    STEP send_vehicle(1, 0); cmd("/vel"); break;
    STEP send_vehicle(1, 0); cmd("/where"); break;
    STEP send_vehicle(1, 0); OUT(srpc_enc_vehicle_damage(b, 1, 0x101, 0x202, 3, 4)); rpc_out(SRPC_C2S_VEHICLE_DAMAGE); break;
    STEP send_vehicle(1, 0); OUT(srpc_enc_scm_event(b, 1, 1010, 0, 2)); rpc_out(SRPC_C2S_SCM_EVENT); break;
    STEP send_vehicle(1, 0); OUT(srpc_enc_scm_event(b, 1, 2, 0, 1)); rpc_out(SRPC_C2S_SCM_EVENT); break;
    STEP send_vehicle(1, 0); OUT(srpc_enc_scm_event(b, 1, 8, 9, 3)); rpc_out(SRPC_C2S_SCM_EVENT); break;
    STEP send_vehicle(1, 3); cmd("/trailer"); break;
    STEP {
        srpc_out_trailer_t t;
        memset(&t, 0, sizeof t);
        send_vehicle(1, 3);
        t.vehicle = 3;
        t.pos[0] = 130.0f; t.pos[1] = 200.0f; t.pos[2] = 10.0f;
        t.quat[0] = 1.0f;
        OUT(srpc_enc_sync_trailer(b, &t));
        sync_out();
        break;
    }
    STEP send_vehicle(1, 3); cmd("/detach"); break;
    STEP send_vehicle(1, 0); OUT(srpc_enc_exit_vehicle(b, 1)); rpc_out(SRPC_C2S_EXIT_VEHICLE); break;
    STEP cmd("/out"); break;
    STEP {
        srpc_out_unoccupied_t s;
        memset(&s, 0, sizeof s);
        s.vehicle = 2;
        s.roll[0] = 1.0f;
        s.direction[1] = 1.0f;
        s.pos[0] = 120.5f; s.pos[1] = 200.0f; s.pos[2] = 10.0f;
        s.health = 900.0f;
        OUT(srpc_enc_sync_unoccupied(b, &s));
        sync_out();
        break;
    }
    STEP OUT(srpc_enc_enter_vehicle(b, 2, 1)); rpc_out(SRPC_C2S_ENTER_VEHICLE); break;
    STEP {
        srpc_out_passenger_t s;
        memset(&s, 0, sizeof s);
        s.vehicle = 2;
        s.seat_flags = 1 | (24 << 8);
        s.health = 80;
        s.armour = 20;
        s.keys = 4;
        s.pos[0] = 120.0f; s.pos[1] = 200.0f; s.pos[2] = 10.0f;
        OUT(srpc_enc_sync_passenger(b, &s));
        sync_out();
        break;
    }
    STEP cmd("/where"); break;
    /* Back on foot: aiming, shooting, stats. */
    STEP foot_weapon = 24; break;
    STEP {
        float front[3] = {0.0f, 1.0f, 0.0f}, cam[3] = {100.0f, 199.0f, 11.0f};
        OUT(srpc_enc_sync_aim(b, 53, front, cam, 0.25f, 12, 2, 127));
        sync_out();
        break;
    }
    STEP {
        float hit[3] = {100.0f, 210.0f, 10.0f}, off[3] = {0.0f, 0.0f, 0.0f}, org[3] = {100.0f, 200.0f, 10.5f};
        OUT(srpc_enc_sync_bullet(b, 0, SRPC_INVALID_ID, org, hit, off, 24));
        sync_out();
        break;
    }
    STEP OUT(srpc_enc_sync_stats(b, 4242, 1500)); sync_out(); break;
    STEP {
        static const srpc_out_weapon_t w[2] = {{0, 1, 1}, {2, 24, 300}};
        OUT(srpc_enc_sync_weapons(b, SRPC_INVALID_ID, SRPC_INVALID_ID, w, 2));
        sync_out();
        break;
    }
    STEP cmd("/money"); break;
    STEP cmd("/wep"); break;
    STEP OUT(srpc_enc_camera_target(b, SRPC_INVALID_ID, 1, SRPC_INVALID_ID, SRPC_INVALID_ID)); rpc_out(SRPC_C2S_CAMERA_TARGET); break;
    STEP cmd("/target"); break;
    STEP rak_rpc(SRPC_C2S_REQUEST_SCORES, 0, 0, RAK_RELIABLE, 0); break;
    STEP cmd("/t7"); stop_foot = 1; break;
    STEP {
        float p[3] = {111.0f, 222.0f, 33.0f};
        OUT(srpc_enc_sync_spectator(b, 0, 0, 0, p));
        sync_out();
        break;
    }
    STEP cmd("/where"); break;
    STEP cmd("/specplayer"); break;
    STEP cmd("/t8"); break;
    STEP stop_foot = 0; rak_rpc(SRPC_C2S_VEHICLE_DESTROYED, 0, 0, RAK_RELIABLE, 0); OUT(srpc_enc_vehicle_destroyed(b, 1)); rpc_out(SRPC_C2S_VEHICLE_DESTROYED); break;
    STEP OUT(srpc_enc_death(b, 54, SRPC_INVALID_ID)); rpc_out(SRPC_C2S_DEATH); break;
    STEP
        /* The peer has to finish first: a restart wipes its state. */
        if (now < spawned_at + 19000) {
            step--;
            break;
        }
        cmd("/t9");
        break;
    STEP printf("EV steps_done\n"); break;
    }
}
