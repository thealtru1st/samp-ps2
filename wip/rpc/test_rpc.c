/* Unit tests for samp_rpc.c. Built by run_tests.sh on the host with ASan/UBSan.
 *
 * bs_r and bs_w are wrapped (-Wl,--wrap) so every primitive the decoder reads
 * and the encoder writes is traced. wire_gen.inc, generated from open.mp's own
 * write() and read() bodies, supplies the widths to compare that trace with. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "samp_rpc.h"

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, e) (fabs((double)(a) - (double)(b)) <= (e))

/* ---- trace --------------------------------------------------------------- */

static int trace[4096], ntrace;
static void tr(int w) { if (ntrace < 4096) trace[ntrace++] = w; }

#define RD(name, ret, w) ret __real_##name(bs_t *b); ret __wrap_##name(bs_t *b) { tr(w); return __real_##name(b); }
RD(bs_r8, unsigned, 8)
RD(bs_r16, unsigned, 16)
RD(bs_r32, unsigned, 32)
RD(bs_rf, float, 32)
RD(bs_rbool, int, 1)
RD(bs_rcomp16, unsigned, -1)
int __real_bs_rbytes(bs_t *b, void *p, int n);
int __wrap_bs_rbytes(bs_t *b, void *p, int n) { if (n > 0) tr(n * 8); return __real_bs_rbytes(b, p, n); }

#define WR(name, type, w) void __real_##name(bs_t *b, type v); void __wrap_##name(bs_t *b, type v) { tr(w); __real_##name(b, v); }
WR(bs_w8, unsigned, 8)
WR(bs_w16, unsigned, 16)
WR(bs_w32, unsigned, 32)
WR(bs_wf, float, 32)
WR(bs_wbool, int, 1)
void __real_bs_wbytes(bs_t *b, const void *p, int n);
void __wrap_bs_wbytes(bs_t *b, const void *p, int n) { if (n > 0) tr(n * 8); __real_bs_wbytes(b, p, n); }

static int same_trace(const int *w, int n)
{
    int i;
    if (n != ntrace)
        return 0;
    for (i = 0; i < n; i++)
        if (w[i] != trace[i])
            return 0;
    return 1;
}

static void show_trace(const char *what, const int *w, int n)
{
    int i;
    printf("  %s\n  want:", what);
    for (i = 0; i < n; i++) printf(" %d", w[i]);
    printf("\n  got: ");
    for (i = 0; i < ntrace; i++) printf(" %d", trace[i]);
    printf("\n");
}

/* ---- generated decoder tests ---------------------------------------------- */

static unsigned char buf[16384];
typedef void (*dispatch_t)(unsigned, bs_t *);
static int cut_fail;

static void wzero(bs_t *b, int bits)
{
    static const unsigned char z[256];
    __real_bs_wbytes(b, z, bits / 8);
}

static void check(const char *name, dispatch_t fn, unsigned id, bs_t *w, const int *widths, int n)
{
    bs_t r;
    int len;

    ntrace = 0;
    memset(&samp_rpc_stats, 0, sizeof samp_rpc_stats);
    bs_rd_init(&r, buf, w->bits);
    fn(id, &r);
    checks++;
    if (r.err || bs_unread(&r) != 0 || samp_rpc_stats.decoded != 1 || samp_rpc_stats.unknown) {
        failures++;
        printf("FAIL %s (%u): err %d, %d bits unread, decoded %u unknown %u\n", name, id, r.err,
               bs_unread(&r), samp_rpc_stats.decoded, samp_rpc_stats.unknown);
    }
    checks++;
    if (!same_trace(widths, n)) {
        failures++;
        printf("FAIL %s (%u): field widths differ\n", name, id);
        show_trace(name, widths, n);
    }
    /* Any shorter version must be caught, not read past the end. */
    for (len = 0; len < w->bits; len++) {
        memset(&samp_rpc_stats, 0, sizeof samp_rpc_stats);
        bs_rd_init(&r, buf, len);
        fn(id, &r);
        if (samp_rpc_stats.truncated != 1) {
            cut_fail++;
            printf("FAIL %s (%u): cut to %d of %d bits not reported\n", name, id, len, w->bits);
            break;
        }
    }
}

#include "wire_gen.inc"

/* ---- values: the fields land where they should ---------------------------- */

static srpc_client_msg_t g_msg;
static void h_msg(const srpc_client_msg_t *m) { g_msg = *m; }
static srpc_dialog_t g_dialog;
static void h_dialog(const srpc_dialog_t *d) { g_dialog = *d; }
static srpc_label_t g_label;
static void h_label(const srpc_label_t *l) { g_label = *l; }
static srpc_textdraw_t g_td;
static void h_td(const srpc_textdraw_t *t) { g_td = *t; }
static srpc_foot_sync_t g_foot;
static void h_foot(const srpc_foot_sync_t *s) { g_foot = *s; }
static srpc_vehicle_sync_t g_car;
static void h_car(const srpc_vehicle_sync_t *s) { g_car = *s; }
static srpc_passenger_sync_t g_pass;
static void h_pass(const srpc_passenger_sync_t *s) { g_pass = *s; }
static srpc_aim_sync_t g_aim;
static void h_aim(const srpc_aim_sync_t *s) { g_aim = *s; }
static srpc_vehicle_t g_veh;
static void h_veh(const srpc_vehicle_t *v) { g_veh = *v; }
static srpc_object_t g_obj;
static void h_obj(const srpc_object_t *o) { g_obj = *o; }
static srpc_object_material_t g_mat;
static void h_mat(const srpc_object_material_t *m) { g_mat = *m; }
static srpc_attached_object_t g_att;
static void h_att(const srpc_attached_object_t *a) { g_att = *a; }
static srpc_menu_t g_menu;
static void h_menu(const srpc_menu_t *m) { g_menu = *m; }
static srpc_set_name_t g_name;
static void h_name(const srpc_set_name_t *n) { g_name = *n; }
static srpc_score_t g_scores[4];
static int n_scores;
static void h_score(const srpc_score_t *s) { if (n_scores < 4) g_scores[n_scores++] = *s; }
static srpc_marker_t g_marks[4];
static int n_marks;
static void h_mark(const srpc_marker_t *m) { if (n_marks < 4) g_marks[n_marks++] = *m; }
static srpc_spectator_sync_t g_spec;
static void h_spec(const srpc_spectator_sync_t *s) { g_spec = *s; }
static srpc_pickup_t g_pickup;
static void h_pickup(const srpc_pickup_t *p) { g_pickup = *p; }
static srpc_checkpoint_t g_cp;
static void h_cp(const srpc_checkpoint_t *c) { g_cp = *c; }
static srpc_gangzone_t g_zone;
static void h_zone(const srpc_gangzone_t *z) { g_zone = *z; }
static srpc_bounds_t g_bounds;
static void h_bounds(const srpc_bounds_t *x) { g_bounds = *x; }
static unsigned g_unknown;
static void h_unknown(unsigned id, bs_t *b) { g_unknown = id; }
static int g_reset;
static void h_reset(void) { g_reset++; }

static const samp_rpc_handlers_t handlers = {
    .client_message = h_msg, .show_dialog = h_dialog, .show_label = h_label, .show_textdraw = h_td,
    .sync_foot = h_foot, .sync_vehicle = h_car, .sync_passenger = h_pass, .sync_aim = h_aim,
    .vehicle_stream_in = h_veh, .create_object = h_obj, .set_object_material = h_mat,
    .set_attached_object = h_att, .init_menu = h_menu, .set_player_name = h_name,
    .score_and_ping = h_score, .sync_marker = h_mark, .sync_spectator = h_spec,
    .create_pickup = h_pickup, .set_checkpoint = h_cp, .show_gangzone = h_zone,
    .set_world_bounds = h_bounds, .unknown = h_unknown, .reset_money = h_reset,
};

/* Bytes spelled out, so nothing here relies on the writer under test. */
static void test_raw_bytes(void)
{
    static const unsigned char msg[] = {0x78, 0x56, 0x34, 0x12, 5, 0, 0, 0, 'h', 'e', 'l', 'l', 'o'};
    static const unsigned char pick[] = {
        0x07, 0, 0, 0,  0xFA, 0x04, 0, 0,  2, 0, 0, 0,  0, 0, 0x80, 0x3F, 0, 0, 0, 0x40, 0, 0, 0x40, 0x40};
    bs_t r;

    bs_rd_init(&r, msg, sizeof msg * 8);
    samp_rpc_dispatch(SRPC_client_message, &r);
    CHECK(g_msg.color == 0x12345678);
    CHECK(!strcmp(g_msg.text, "hello"));

    bs_rd_init(&r, pick, sizeof pick * 8);
    samp_rpc_dispatch(SRPC_create_pickup, &r);
    CHECK(g_pickup.id == 7 && g_pickup.model == 1274 && g_pickup.type == 2);
    CHECK(g_pickup.pos[0] == 1.0f && g_pickup.pos[1] == 2.0f && g_pickup.pos[2] == 3.0f);
}

static void test_huffman(void)
{
    static char big[3000], out[3000];
    bs_t w, r;
    char small[8];
    int i;

    for (i = 0; i < 2999; i++)
        big[i] = 32 + (i * 7) % 90;
    bs_wr_init(&w, buf, sizeof buf);
    samp_write_compressed_str(&w, "/help - this very same dialog.\n/kill");
    samp_write_compressed_str(&w, "");
    samp_write_compressed_str(&w, big);
    bs_w8(&w, 0xA5);
    bs_rd_init(&r, buf, w.bits);
    samp_read_compressed_str(&r, out, sizeof out);
    CHECK(!strcmp(out, "/help - this very same dialog.\n/kill"));
    samp_read_compressed_str(&r, out, sizeof out);
    CHECK(out[0] == 0);
    samp_read_compressed_str(&r, out, sizeof out);
    CHECK(!strcmp(out, big));
    CHECK(bs_r8(&r) == 0xA5);

    /* Shorter than the string: cut, terminated, and the stream still moves on. */
    bs_wr_init(&w, buf, sizeof buf);
    samp_write_compressed_str(&w, "a long enough sentence");
    bs_w8(&w, 0x5A);
    bs_rd_init(&r, buf, w.bits);
    samp_read_compressed_str(&r, small, sizeof small);
    CHECK(!strcmp(small, "a long "));
    CHECK(bs_r8(&r) == 0x5A);

    /* A bit length larger than the stream. */
    bs_wr_init(&w, buf, sizeof buf);
    bs_wcomp16(&w, 4000);
    bs_w8(&w, 0);
    bs_rd_init(&r, buf, w.bits);
    samp_read_compressed_str(&r, small, sizeof small);
    CHECK(r.err && small[0] == 0);
}

static void test_dialog(void)
{
    bs_t w, r;

    bs_wr_init(&w, buf, sizeof buf);
    bs_w16(&w, 0xFFFF);
    bs_w8(&w, 2);
    bs_w8(&w, 5); bs_wbytes(&w, "Title", 5);
    bs_w8(&w, 2); bs_wbytes(&w, "Ok", 2);
    bs_w8(&w, 0);
    samp_write_compressed_str(&w, "line one\nline two");
    bs_rd_init(&r, buf, w.bits);
    samp_rpc_dispatch(SRPC_show_dialog, &r);
    CHECK(g_dialog.id == -1 && g_dialog.style == 2);
    CHECK(!strcmp(g_dialog.title, "Title") && !strcmp(g_dialog.button1, "Ok") && !g_dialog.button2[0]);
    CHECK(!strcmp(g_dialog.body, "line one\nline two"));
    CHECK(bs_unread(&r) == 0);
}

static void test_label(void)
{
    bs_t w, r;
    float p[3] = {1, 2, 3};

    bs_wr_init(&w, buf, sizeof buf);
    bs_w16(&w, SRPC_LABEL_GLOBALS + 9);
    bs_w32(&w, 0x11223344);
    bs_wf(&w, p[0]); bs_wf(&w, p[1]); bs_wf(&w, p[2]);
    bs_wf(&w, 30.0f);
    bs_w8(&w, 1);
    bs_w16(&w, 4);
    bs_w16(&w, SRPC_INVALID_ID);
    samp_write_compressed_str(&w, "for sale");
    bs_rd_init(&r, buf, w.bits);
    samp_rpc_dispatch(SRPC_show_label, &r);
    CHECK(g_label.player && g_label.id == 9 && g_label.color == 0x11223344 && g_label.pos[2] == 3.0f);
    CHECK(g_label.distance == 30.0f && g_label.los == 1 && g_label.attach_player == 4);
    CHECK(g_label.attach_vehicle == SRPC_INVALID_ID && !strcmp(g_label.text, "for sale"));
}

static void test_textdraw(void)
{
    bs_t w, r;

    bs_wr_init(&w, buf, sizeof buf);
    bs_w16(&w, SRPC_TD_GLOBALS + 3);
    bs_w8(&w, 1 | (SRPC_TD_CENTER << 1) | (1 << 4));
    bs_wf(&w, 0.5f); bs_wf(&w, 1.5f);
    bs_w32(&w, 0xAABBCCDD);
    bs_wf(&w, 640.0f); bs_wf(&w, 480.0f);
    bs_w32(&w, 0x01020304);
    bs_w8(&w, 2); bs_w8(&w, 1);
    bs_w32(&w, 0x05060708);
    bs_w8(&w, 3); bs_w8(&w, 1);
    bs_wf(&w, 100.0f); bs_wf(&w, 200.0f);
    bs_w16(&w, 411);
    bs_wf(&w, 10.0f); bs_wf(&w, 20.0f); bs_wf(&w, 30.0f); bs_wf(&w, 1.25f);
    bs_w16(&w, 0xFFFF); bs_w16(&w, 7);
    bs_w16(&w, 4); bs_wbytes(&w, "text", 4);
    bs_rd_init(&r, buf, w.bits);
    samp_rpc_dispatch(SRPC_show_textdraw, &r);
    CHECK(g_td.player && g_td.id == 3 && g_td.box && g_td.align == SRPC_TD_CENTER && g_td.proportional);
    CHECK(g_td.letter_w == 0.5f && g_td.letter_h == 1.5f && g_td.letter_color == 0xAABBCCDD);
    CHECK(g_td.text_w == 640.0f && g_td.box_color == 0x01020304 && g_td.shadow == 2 && g_td.outline == 1);
    CHECK(g_td.back_color == 0x05060708 && g_td.style == 3 && g_td.selectable && g_td.x == 100.0f);
    CHECK(g_td.model == 411 && g_td.rot[2] == 30.0f && g_td.zoom == 1.25f);
    CHECK(g_td.color1 == -1 && g_td.color2 == 7 && !strcmp(g_td.text, "text"));
}

/* Writes a quaternion the way WriteNormQuat does. */
static void w_norm_quat(bs_t *w, float qw, float x, float y, float z)
{
    bs_wbool(w, qw < 0); bs_wbool(w, x < 0); bs_wbool(w, y < 0); bs_wbool(w, z < 0);
    bs_w16(w, (unsigned short)(fabs(x) * 65535.0));
    bs_w16(w, (unsigned short)(fabs(y) * 65535.0));
    bs_w16(w, (unsigned short)(fabs(z) * 65535.0));
}

static void w_comp_vec3(bs_t *w, float x, float y, float z)
{
    float m = sqrtf(x * x + y * y + z * z);
    bs_wf(w, m);
    if (m > 0.00001f) {
        bs_w16(w, (unsigned short)((x / m + 1.0f) * 32767.5f));
        bs_w16(w, (unsigned short)((y / m + 1.0f) * 32767.5f));
        bs_w16(w, (unsigned short)((z / m + 1.0f) * 32767.5f));
    }
}

static void test_foot(void)
{
    bs_t w, r;

    /* All optional parts present. */
    bs_wr_init(&w, buf, sizeof buf);
    bs_w16(&w, 12);
    bs_wbool(&w, 1); bs_w16(&w, 0xFF80);
    bs_wbool(&w, 1); bs_w16(&w, 128);
    bs_w16(&w, 0x0404);
    bs_wf(&w, 1.0f); bs_wf(&w, 2.0f); bs_wf(&w, 3.0f);
    w_norm_quat(&w, -0.5f, 0.5f, -0.5f, 0.5f);
    bs_w8(&w, (15 << 4) | 5);
    bs_w8(&w, 31 | (2 << 6));
    bs_w8(&w, 3);
    w_comp_vec3(&w, 0.0f, 0.3f, -0.4f);
    bs_wbool(&w, 1); bs_w16(&w, 2055); bs_wf(&w, 0.5f); bs_wf(&w, 0.25f); bs_wf(&w, 0.125f);
    bs_wbool(&w, 1); bs_w16(&w, 1189); bs_w16(&w, 0x4004);
    memset(&samp_rpc_stats, 0, sizeof samp_rpc_stats);
    bs_rd_init(&r, buf, w.bits);
    samp_sync_dispatch(SRPC_sync_foot, &r);
    CHECK(samp_rpc_stats.decoded == 1 && bs_unread(&r) == 0);
    CHECK(g_foot.player == 12 && g_foot.lr == 0xFF80 && g_foot.ud == 128 && g_foot.keys == 0x0404);
    CHECK(g_foot.pos[1] == 2.0f);
    CHECK(NEAR(g_foot.quat[0], -0.5, 1e-3) && NEAR(g_foot.quat[1], 0.5, 1e-3));
    CHECK(NEAR(g_foot.quat[2], -0.5, 1e-3) && NEAR(g_foot.quat[3], 0.5, 1e-3));
    CHECK(g_foot.health == 100 && g_foot.armour == 35);
    CHECK(g_foot.weapon == 31 && g_foot.additional_key == 2 && g_foot.special_action == 3);
    CHECK(NEAR(g_foot.velocity[0], 0, 1e-3) && NEAR(g_foot.velocity[1], 0.3, 1e-3) && NEAR(g_foot.velocity[2], -0.4, 1e-3));
    CHECK(g_foot.surfing && g_foot.surf_id == 2055 && g_foot.surf_offset[2] == 0.125f);
    CHECK(g_foot.anim_id == 1189 && g_foot.anim_flags == 0x4004);

    /* All optional parts absent, and zero velocity carries no direction. */
    bs_wr_init(&w, buf, sizeof buf);
    bs_w16(&w, 3);
    bs_wbool(&w, 0);
    bs_wbool(&w, 0);
    bs_w16(&w, 0);
    bs_wf(&w, 0.0f); bs_wf(&w, 0.0f); bs_wf(&w, 0.0f);
    w_norm_quat(&w, 1.0f, 0.0f, 0.0f, 0.0f);
    bs_w8(&w, 0);
    bs_w8(&w, 0);
    bs_w8(&w, 0);
    w_comp_vec3(&w, 0.0f, 0.0f, 0.0f);
    bs_wbool(&w, 0);
    bs_wbool(&w, 0);
    bs_rd_init(&r, buf, w.bits);
    samp_sync_dispatch(SRPC_sync_foot, &r);
    CHECK(bs_unread(&r) == 0 && !r.err);
    CHECK(g_foot.player == 3 && g_foot.lr == 0 && !g_foot.surfing && g_foot.anim_id == 0);
    CHECK(NEAR(g_foot.quat[0], 1.0, 1e-3));
}

static void test_vehicle_sync(void)
{
    bs_t w, r;
    int variant;

    for (variant = 0; variant < 2; variant++) {
        bs_wr_init(&w, buf, sizeof buf);
        bs_w16(&w, 5); bs_w16(&w, 400);
        bs_w16(&w, 1); bs_w16(&w, 2); bs_w16(&w, 3);
        w_norm_quat(&w, 0.5f, -0.5f, 0.5f, -0.5f);
        bs_wf(&w, 10.0f); bs_wf(&w, 20.0f); bs_wf(&w, 30.0f);
        w_comp_vec3(&w, 1.0f, 0.0f, 0.0f);
        bs_w16(&w, 999);
        bs_w8(&w, (14 << 4) | 1);
        bs_w8(&w, 24 | (1 << 6));
        bs_wbool(&w, 1);
        bs_wbool(&w, 0);
        bs_wbool(&w, variant);
        if (variant) bs_w32(&w, 5000);
        bs_wbool(&w, variant);
        if (variant) bs_w16(&w, 77);
        bs_rd_init(&r, buf, w.bits);
        samp_sync_dispatch(SRPC_sync_vehicle, &r);
        CHECK(bs_unread(&r) == 0 && !r.err);
        CHECK(g_car.player == 5 && g_car.vehicle == 400 && g_car.keys == 3 && g_car.pos[2] == 30.0f);
        CHECK(NEAR(g_car.quat[1], -0.5, 1e-3) && NEAR(g_car.velocity[0], 1.0, 1e-3));
        CHECK(g_car.health == 999 && g_car.player_health == 98 && g_car.player_armour == 7);
        CHECK(g_car.weapon == 24 && g_car.additional_key == 1 && g_car.siren == 1 && g_car.landing_gear == 0);
        CHECK(g_car.hydra_angle == (variant ? 5000u : 0u) && g_car.has_trailer == variant);
        CHECK(g_car.trailer == (variant ? 77u : 0u));
    }
}

static void test_bitfields(void)
{
    bs_t w, r;

    bs_wr_init(&w, buf, sizeof buf);
    bs_w16(&w, 2); bs_w16(&w, 9);
    bs_w16(&w, 3 | (1 << 2) | (29 << 8) | (2 << 14));
    bs_w8(&w, 90); bs_w8(&w, 40);
    bs_w16(&w, 1); bs_w16(&w, 2); bs_w16(&w, 3);
    bs_wf(&w, 1.0f); bs_wf(&w, 2.0f); bs_wf(&w, 3.0f);
    bs_rd_init(&r, buf, w.bits);
    samp_sync_dispatch(SRPC_sync_passenger, &r);
    CHECK(g_pass.seat == 3 && g_pass.drive_by == 1 && g_pass.weapon == 29 && g_pass.additional_key == 2);
    CHECK(g_pass.health == 90 && g_pass.armour == 40 && g_pass.pos[2] == 3.0f);

    bs_wr_init(&w, buf, sizeof buf);
    bs_w16(&w, 2); bs_w8(&w, 53);
    bs_wf(&w, 1.0f); bs_wf(&w, 0.0f); bs_wf(&w, 0.0f);
    bs_wf(&w, 5.0f); bs_wf(&w, 6.0f); bs_wf(&w, 7.0f);
    bs_wf(&w, 0.5f);
    bs_w8(&w, 35 | (3 << 6));
    bs_w8(&w, 127);
    bs_rd_init(&r, buf, w.bits);
    samp_sync_dispatch(SRPC_sync_aim, &r);
    CHECK(g_aim.cam_mode == 53 && g_aim.cam_pos[2] == 7.0f && g_aim.aim_z == 0.5f);
    CHECK(g_aim.cam_zoom == 35 && g_aim.weapon_state == 3 && g_aim.aspect == 127);
}

static void test_spectator(void)
{
    bs_t w, r;

    bs_wr_init(&w, buf, sizeof buf);
    bs_wbool(&w, 0);
    bs_wbool(&w, 1); bs_w16(&w, 0xFF80);
    bs_w16(&w, 8);
    bs_wf(&w, 1.0f); bs_wf(&w, 2.0f); bs_wf(&w, 3.0f);
    bs_rd_init(&r, buf, w.bits);
    samp_sync_dispatch(SRPC_sync_spectator, &r);
    CHECK(bs_unread(&r) == 0 && g_spec.lr == 0 && g_spec.ud == 0xFF80 && g_spec.keys == 8 && g_spec.pos[1] == 2.0f);
}

static void test_vehicle_stream(void)
{
    bs_t w, r;
    int i;

    bs_wr_init(&w, buf, sizeof buf);
    bs_w16(&w, 44); bs_w32(&w, 411);
    bs_wf(&w, 1.0f); bs_wf(&w, 2.0f); bs_wf(&w, 3.0f); bs_wf(&w, 90.0f);
    bs_w8(&w, 6); bs_w8(&w, 7);
    bs_wf(&w, 950.0f);
    bs_w8(&w, 3);
    bs_w32(&w, 0x11); bs_w32(&w, 0x22);
    bs_w8(&w, 0x33); bs_w8(&w, 0x44); bs_w8(&w, 1);
    for (i = 0; i < SRPC_CAR_MODS; i++)
        bs_w8(&w, i == 2 ? 1 : 0);
    bs_w8(&w, 2);
    bs_w32(&w, 0xFFFFFFFF); bs_w32(&w, 5);
    bs_rd_init(&r, buf, w.bits);
    samp_rpc_dispatch(SRPC_vehicle_stream_in, &r);
    CHECK(bs_unread(&r) == 0 && !r.err);
    CHECK(g_veh.id == 44 && g_veh.model == 411 && g_veh.angle == 90.0f && g_veh.color2 == 7);
    CHECK(g_veh.health == 950.0f && g_veh.interior == 3 && g_veh.door_damage == 0x11 && g_veh.panel_damage == 0x22);
    CHECK(g_veh.light_damage == 0x33 && g_veh.tyre_damage == 0x44 && g_veh.siren == 1);
    CHECK(g_veh.mods[2] == 1000 && g_veh.mods[3] == 0 && g_veh.paintjob == 2);
    CHECK(g_veh.body_color1 == -1 && g_veh.body_color2 == 5);
}

static void w_str8t(bs_t *w, const char *s)
{
    bs_w8(w, strlen(s));
    bs_wbytes(w, s, strlen(s));
}

static void w_object_head(bs_t *w, unsigned attach_vehicle, unsigned attach_object)
{
    bs_w16(w, 301);
    bs_w32(w, 1337);
    bs_wf(w, 1.0f); bs_wf(w, 2.0f); bs_wf(w, 3.0f);
    bs_wf(w, 4.0f); bs_wf(w, 5.0f); bs_wf(w, 6.0f);
    bs_wf(w, 300.0f);
    bs_w8(w, 1);
    bs_w16(w, attach_vehicle);
    bs_w16(w, attach_object);
}

static void test_object(void)
{
    bs_t w, r;

    /* Unattached, no materials. */
    bs_wr_init(&w, buf, sizeof buf);
    w_object_head(&w, SRPC_INVALID_ID, SRPC_INVALID_ID);
    bs_w8(&w, 0);
    bs_rd_init(&r, buf, w.bits);
    samp_rpc_dispatch(SRPC_create_object, &r);
    CHECK(bs_unread(&r) == 0 && !r.err);
    CHECK(g_obj.id == 301 && g_obj.model == 1337 && g_obj.rot[2] == 6.0f && g_obj.distance == 300.0f);
    CHECK(g_obj.no_camera_col == 1 && g_obj.material_count == 0);

    /* Attached to a vehicle, one texture and one text material. */
    bs_wr_init(&w, buf, sizeof buf);
    w_object_head(&w, 12, SRPC_INVALID_ID);
    bs_wf(&w, 0.1f); bs_wf(&w, 0.2f); bs_wf(&w, 0.3f);
    bs_wf(&w, 10.0f); bs_wf(&w, 20.0f); bs_wf(&w, 30.0f);
    bs_w8(&w, 1);
    bs_w8(&w, 2);
    bs_w8(&w, 1); bs_w8(&w, 3);
    bs_w16(&w, 19341); w_str8t(&w, "egg_txd"); w_str8t(&w, "egg_tex"); bs_w32(&w, 0xFF00FF00);
    bs_w8(&w, 2); bs_w8(&w, 4);
    bs_w8(&w, 90); w_str8t(&w, "Arial"); bs_w8(&w, 24); bs_w8(&w, 1);
    bs_w32(&w, 0xFFFFFFFF); bs_w32(&w, 0xFF000000); bs_w8(&w, 1);
    samp_write_compressed_str(&w, "hello sign");
    bs_rd_init(&r, buf, w.bits);
    samp_rpc_dispatch(SRPC_create_object, &r);
    CHECK(bs_unread(&r) == 0 && !r.err);
    CHECK(g_obj.attach_vehicle == 12 && g_obj.attach_object == SRPC_INVALID_ID && g_obj.sync_rotation == 1);
    CHECK(g_obj.offset_rot[1] == 20.0f && g_obj.material_count == 2);
    CHECK(g_obj.materials[0].type == 1 && g_obj.materials[0].slot == 3 && g_obj.materials[0].model == 19341);
    CHECK(!strcmp(g_obj.materials[0].txd, "egg_txd") && !strcmp(g_obj.materials[0].texture, "egg_tex"));
    CHECK(g_obj.materials[0].color == 0xFF00FF00);
    CHECK(g_obj.materials[1].type == 2 && g_obj.materials[1].slot == 4 && g_obj.materials[1].size == 90);
    CHECK(!strcmp(g_obj.materials[1].font, "Arial") && g_obj.materials[1].font_size == 24);
    CHECK(g_obj.materials[1].bold == 1 && g_obj.materials[1].align == 1);
    CHECK(!strcmp(g_obj.materials[1].text, "hello sign"));

    /* The separate material RPC. */
    bs_wr_init(&w, buf, sizeof buf);
    bs_w16(&w, 301);
    bs_w8(&w, 1); bs_w8(&w, 2);
    bs_w16(&w, 18646); w_str8t(&w, "matcolours"); w_str8t(&w, "white"); bs_w32(&w, 0x80112233);
    bs_rd_init(&r, buf, w.bits);
    samp_rpc_dispatch(SRPC_set_object_material, &r);
    CHECK(bs_unread(&r) == 0 && g_mat.object == 301 && g_mat.material.slot == 2);
    CHECK(g_mat.material.model == 18646 && !strcmp(g_mat.material.texture, "white"));
}

static void test_attached(void)
{
    bs_t w, r;
    int i;

    bs_wr_init(&w, buf, sizeof buf);
    bs_w16(&w, 8); bs_w32(&w, 2); bs_wbool(&w, 1);
    bs_w32(&w, 1242); bs_w32(&w, 2);
    for (i = 0; i < 9; i++)
        bs_wf(&w, i + 1.0f);
    bs_w32(&w, 0xAABBCCDD); bs_w32(&w, 0x11223344);
    bs_rd_init(&r, buf, w.bits);
    samp_rpc_dispatch(SRPC_set_attached_object, &r);
    CHECK(bs_unread(&r) == 0 && g_att.player == 8 && g_att.index == 2 && g_att.create);
    CHECK(g_att.model == 1242 && g_att.bone == 2 && g_att.scale[2] == 9.0f && g_att.color2 == 0x11223344);

    bs_wr_init(&w, buf, sizeof buf);
    bs_w16(&w, 8); bs_w32(&w, 2); bs_wbool(&w, 0);
    bs_rd_init(&r, buf, w.bits);
    samp_rpc_dispatch(SRPC_set_attached_object, &r);
    CHECK(bs_unread(&r) == 0 && !g_att.create && g_att.model == 0);
}

static void w_fixed(bs_t *w, const char *s)
{
    char f[32] = {0};
    strncpy(f, s, sizeof f - 1);
    bs_wbytes(w, f, 32);
}

static void test_menu(void)
{
    bs_t w, r;
    int i, two;

    for (two = 0; two < 2; two++) {
        bs_wr_init(&w, buf, sizeof buf);
        bs_w8(&w, 3);
        bs_w32(&w, two);
        w_fixed(&w, "Shop");
        bs_wf(&w, 100.0f); bs_wf(&w, 200.0f); bs_wf(&w, 150.0f);
        if (two) bs_wf(&w, 75.0f);
        bs_w32(&w, 1);
        for (i = 0; i < SRPC_MENU_ROWS; i++)
            bs_w32(&w, i != 1);
        w_fixed(&w, "Item");
        bs_w8(&w, 2);
        w_fixed(&w, "Deagle");
        w_fixed(&w, "Rifle");
        if (two) {
            w_fixed(&w, "Price");
            bs_w8(&w, 1);
            w_fixed(&w, "$500");
        }
        bs_rd_init(&r, buf, w.bits);
        samp_rpc_dispatch(SRPC_init_menu, &r);
        CHECK(bs_unread(&r) == 0 && !r.err);
        CHECK(g_menu.id == 3 && g_menu.two_columns == two && !strcmp(g_menu.title, "Shop"));
        CHECK(g_menu.y == 200.0f && g_menu.col2_width == (two ? 75.0f : 0.0f) && g_menu.enabled);
        CHECK(g_menu.row_enabled[0] == 1 && g_menu.row_enabled[1] == 0);
        CHECK(!strcmp(g_menu.header[0], "Item") && g_menu.count[0] == 2 && !strcmp(g_menu.items[0][1], "Rifle"));
        CHECK(g_menu.count[1] == (two ? 1u : 0u));
        if (two)
            CHECK(!strcmp(g_menu.header[1], "Price") && !strcmp(g_menu.items[1][0], "$500"));
    }
}

static void test_lists(void)
{
    bs_t w, r;

    bs_wr_init(&w, buf, sizeof buf);
    bs_w16(&w, 1); bs_w32(&w, 0xFFFFFFFE); bs_w32(&w, 30);
    bs_w16(&w, 2); bs_w32(&w, 100); bs_w32(&w, 40);
    n_scores = 0;
    bs_rd_init(&r, buf, w.bits);
    samp_rpc_dispatch(SRPC_score_and_ping, &r);
    CHECK(n_scores == 2 && (int)g_scores[0].score == -2 && g_scores[1].id == 2 && g_scores[1].ping == 40);

    bs_wr_init(&w, buf, sizeof buf);
    bs_w32(&w, 2);
    bs_w16(&w, 4); bs_wbool(&w, 1); bs_w16(&w, 0xFFF0); bs_w16(&w, 200); bs_w16(&w, 15);
    bs_w16(&w, 6); bs_wbool(&w, 0);
    n_marks = 0;
    bs_rd_init(&r, buf, w.bits);
    samp_sync_dispatch(SRPC_sync_marker, &r);
    CHECK(n_marks == 2 && g_marks[0].id == 4 && g_marks[0].active && g_marks[0].x == -16 && g_marks[0].y == 200);
    CHECK(g_marks[1].id == 6 && !g_marks[1].active && bs_unread(&r) < 8);

    /* A huge count ends at the end of the stream instead of spinning. */
    bs_wr_init(&w, buf, sizeof buf);
    bs_w32(&w, 0xFFFFFFFF);
    memset(&samp_rpc_stats, 0, sizeof samp_rpc_stats);
    bs_rd_init(&r, buf, w.bits);
    samp_sync_dispatch(SRPC_sync_marker, &r);
    CHECK(samp_rpc_stats.truncated == 1);
}

static void test_bounds(void)
{
    bs_t w, r;
    char big[300];

    /* A name longer than the buffer is cut, and the field after it still lands. */
    memset(big, 'x', sizeof big);
    bs_wr_init(&w, buf, sizeof buf);
    bs_w16(&w, 1);
    bs_w8(&w, 200);
    bs_wbytes(&w, big, 200);
    bs_w8(&w, 1);
    bs_rd_init(&r, buf, w.bits);
    samp_rpc_dispatch(SRPC_set_player_name, &r);
    CHECK(strlen(g_name.name) == SRPC_NAME_MAX - 1 && g_name.success == 1 && bs_unread(&r) == 0);

    /* A length past the end of the message. */
    bs_wr_init(&w, buf, sizeof buf);
    bs_w32(&w, 0);
    bs_w32(&w, 0x7FFFFFFF);
    bs_wbytes(&w, "short", 5);
    memset(&samp_rpc_stats, 0, sizeof samp_rpc_stats);
    strcpy(g_msg.text, "unchanged");
    bs_rd_init(&r, buf, w.bits);
    samp_rpc_dispatch(SRPC_client_message, &r);
    CHECK(samp_rpc_stats.truncated == 1 && samp_rpc_stats.last_bad == SRPC_client_message);
    CHECK(!strcmp(g_msg.text, "unchanged"));

    /* Unsigned lengths must not wrap the bound checks. */
    bs_wr_init(&w, buf, sizeof buf);
    bs_w32(&w, 0);
    bs_w32(&w, 0xFFFFFFFF);
    bs_rd_init(&r, buf, w.bits);
    samp_rpc_dispatch(SRPC_client_message, &r);
    CHECK(r.err);

    /* A menu claiming more rows than it may have. */
    {
        int i;
        bs_wr_init(&w, buf, sizeof buf);
        bs_w8(&w, 1); bs_w32(&w, 0);
        w_fixed(&w, "t");
        bs_wf(&w, 0); bs_wf(&w, 0); bs_wf(&w, 0);
        bs_w32(&w, 1);
        for (i = 0; i < SRPC_MENU_ROWS; i++)
            bs_w32(&w, 1);
        w_fixed(&w, "h");
        bs_w8(&w, 20);
        for (i = 0; i < 20; i++)
            w_fixed(&w, "row");
        bs_rd_init(&r, buf, w.bits);
        samp_rpc_dispatch(SRPC_init_menu, &r);
        CHECK(bs_unread(&r) == 0 && g_menu.count[0] == SRPC_MENU_ROWS);
    }

    /* An object claiming more material slots than exist. */
    {
        int i;
        bs_wr_init(&w, buf, sizeof buf);
        w_object_head(&w, SRPC_INVALID_ID, SRPC_INVALID_ID);
        bs_w8(&w, 30);
        for (i = 0; i < 30; i++) {
            bs_w8(&w, 1); bs_w8(&w, i);
            bs_w16(&w, 1); w_str8t(&w, "a"); w_str8t(&w, "b"); bs_w32(&w, 1);
        }
        bs_rd_init(&r, buf, w.bits);
        samp_rpc_dispatch(SRPC_create_object, &r);
        CHECK(bs_unread(&r) == 0 && g_obj.material_count == SRPC_MAT_SLOTS);
    }
}

static void test_misc(void)
{
    bs_t w, r;
    srpc_vehicle_params_old_t dummy;
    (void)dummy;

    /* Wire order is max x, min x, max y, min y. */
    bs_wr_init(&w, buf, sizeof buf);
    bs_wf(&w, 4.0f); bs_wf(&w, 3.0f); bs_wf(&w, 2.0f); bs_wf(&w, 1.0f);
    bs_rd_init(&r, buf, w.bits);
    samp_rpc_dispatch(SRPC_set_world_bounds, &r);
    CHECK(g_bounds.max_x == 4.0f && g_bounds.min_x == 3.0f && g_bounds.max_y == 2.0f && g_bounds.min_y == 1.0f);

    bs_wr_init(&w, buf, sizeof buf);
    bs_wf(&w, 1.0f); bs_wf(&w, 2.0f); bs_wf(&w, 3.0f); bs_wf(&w, 4.5f);
    bs_rd_init(&r, buf, w.bits);
    samp_rpc_dispatch(SRPC_set_checkpoint, &r);
    CHECK(g_cp.pos[2] == 3.0f && g_cp.size == 4.5f);

    bs_wr_init(&w, buf, sizeof buf);
    bs_w16(&w, 0xFFFE);
    bs_wf(&w, 1.0f); bs_wf(&w, 2.0f); bs_wf(&w, 3.0f); bs_wf(&w, 4.0f);
    bs_w32(&w, 0x12345678);
    bs_rd_init(&r, buf, w.bits);
    samp_rpc_dispatch(SRPC_show_gangzone, &r);
    CHECK(g_zone.id == -2 && g_zone.max_y == 4.0f && g_zone.color == 0x12345678);

    /* Empty RPCs reach their handler; an unknown id reaches the fallback. */
    g_reset = 0;
    bs_rd_init(&r, buf, 0);
    samp_rpc_dispatch(SRPC_reset_money, &r);
    CHECK(g_reset == 1);
    bs_rd_init(&r, buf, 0);
    samp_rpc_dispatch(250, &r);
    CHECK(g_unknown == 250);

    CHECK(!strcmp(samp_rpc_name(SRPC_show_dialog), "show_dialog"));
    CHECK(!strcmp(samp_rpc_name(SRPC_sync_foot), "sync_foot"));
    CHECK(samp_rpc_name(250) == 0);
}

/* Captured from a real open.mp 1.6 server sending rpctest.pwn's dialog and label.
 * A round trip cannot prove the Huffman tree matches the server's, these can. */
static unsigned hexval(char c) { return c <= '9' ? c - '0' : c - 'A' + 10; }

static int unhex(unsigned char *out, const char *s)
{
    int n = 0;
    for (; s[0] && s[1]; s += 2)
        out[n++] = hexval(s[0]) << 4 | hexval(s[1]);
    return n;
}

static void test_server_capture(void)
{
    static const char dialog[] =
        "4D00020C4469616C6F67207469746C65034275790643616E63656C4C00CE60494EAFFBC26FCF19AFEDB41DC8D2899E3671"
        "FA4A5D5FFB5A508E0F1AF948AC5654551545513E539903BBE592BC2A2A8AA200";
    static const char label[] =
        "0000443322110000C84200004943000030410000204201FFFFFFFF9615A941ED2F263C4BD8A9DA80";
    bs_t r;

    unhex(buf, dialog);
    bs_rd_init(&r, buf, 641);
    samp_rpc_dispatch(SRPC_show_dialog, &r);
    CHECK(bs_unread(&r) == 0 && !r.err);
    CHECK(g_dialog.id == 77 && g_dialog.style == 2 && !strcmp(g_dialog.title, "Dialog title"));
    CHECK(!strcmp(g_dialog.button1, "Buy") && !strcmp(g_dialog.button2, "Cancel"));
    CHECK(!strcmp(g_dialog.body, "/help - this very same dialog.\n/kill - suicide.\n{FF0000}Red $3000"));

    unhex(buf, label);
    bs_rd_init(&r, buf, 314);
    samp_rpc_dispatch(SRPC_show_label, &r);
    CHECK(bs_unread(&r) == 0 && !r.err);
    CHECK(g_label.id == 0 && !g_label.player && g_label.color == 0x11223344 && g_label.los == 1);
    CHECK(!strcmp(g_label.text, "Global label text"));
}

/* Stock server ids with no open.mp write() to generate a test from. */
static int g_i;
static void h_i(const srpc_int_t *x) { g_i = x->v; }
static unsigned g_id;
static void h_id(const srpc_id_t *x) { g_id = x->id; }
static srpc_id_val_t g_iv;
static void h_iv(const srpc_id_val_t *x) { g_iv = *x; }
static srpc_vehicle_params_old_t g_po;
static void h_po(const srpc_vehicle_params_old_t *x) { g_po = *x; }

static void test_stock_only(void)
{
    static const samp_rpc_handlers_t h = {
        .set_drunk_visuals = h_i, .set_drunk_handling = h_i, .connection_rejected = h_id,
        .destroy_weapon_pickup = h_id, .object_no_camera_col = h_id, .set_tyre_status = h_iv,
        .set_vehicle_params_old = h_po,
    };
    static const unsigned char i32[] = {0x78, 0x56, 0x34, 0x12};
    static const unsigned char veh[] = {0x34, 0x12, 1, 2};
    bs_t r;

    samp_rpc_set_handlers(&h);
    bs_rd_init(&r, i32, 32);
    samp_rpc_dispatch(SRPC_set_drunk_visuals, &r);
    CHECK(g_i == 0x12345678 && bs_unread(&r) == 0);
    g_i = 0;
    bs_rd_init(&r, i32, 32);
    samp_rpc_dispatch(SRPC_set_drunk_handling, &r);
    CHECK(g_i == 0x12345678 && bs_unread(&r) == 0);
    bs_rd_init(&r, veh + 2, 8);
    samp_rpc_dispatch(SRPC_connection_rejected, &r);
    CHECK(g_id == 1 && bs_unread(&r) == 0);
    bs_rd_init(&r, veh + 3, 8);
    samp_rpc_dispatch(SRPC_destroy_weapon_pickup, &r);
    CHECK(g_id == 2 && bs_unread(&r) == 0);
    bs_rd_init(&r, veh, 16);
    samp_rpc_dispatch(SRPC_object_no_camera_col, &r);
    CHECK(g_id == 0x1234 && bs_unread(&r) == 0);
    bs_rd_init(&r, veh, 24);
    samp_rpc_dispatch(SRPC_set_tyre_status, &r);
    CHECK(g_iv.id == 0x1234 && g_iv.v == 1 && bs_unread(&r) == 0);
    bs_rd_init(&r, veh, 32);
    samp_rpc_dispatch(SRPC_set_vehicle_params_old, &r);
    CHECK(g_po.id == 0x1234 && g_po.objective == 1 && g_po.doors == 2 && bs_unread(&r) == 0);
    samp_rpc_set_handlers(&handlers);
}

/* ---- encoders ------------------------------------------------------------- */

#define ENC(name, call) do { \
    bs_t b_; \
    bs_wr_init(&b_, buf, sizeof buf); \
    ntrace = 0; \
    { bs_t *b = &b_; call; } \
    checks++; \
    if (!same_trace(rw_##name, rn_##name) || b_.err) { \
        failures++; \
        printf("FAIL encoder for %s does not match its read()\n", #name); \
        show_trace(#name, rw_##name, rn_##name); \
    } \
} while (0)

static void test_encoders(void)
{
    static const float v[3] = {1, 2, 3};
    static const srpc_out_foot_t foot = {0};
    static const srpc_out_vehicle_t car = {0};
    static const srpc_out_passenger_t pass = {0};
    static const srpc_out_unoccupied_t unocc = {0};
    static const srpc_out_trailer_t trail = {0};

    ENC(OnPlayerClickPlayer, srpc_enc_click_player(b, 1, 0));
    ENC(OnPlayerEnterVehicle, srpc_enc_enter_vehicle(b, 1, 0));
    ENC(OnPlayerSelectObject, srpc_enc_select_object(b, 1, 2, 3, v));
    ENC(OnPlayerDeath, srpc_enc_death(b, 1, 2));
    ENC(OnPlayerDialogResponse, srpc_enc_dialog_response(b, 1, 1, 0, "abc"));
    ENC(OnPlayerSelectTextDraw, srpc_enc_click_textdraw(b, 1));
    ENC(SCMEvent, srpc_enc_scm_event(b, 1, 2, 3, 4));
    ENC(PlayerRequestChatMessage, srpc_enc_chat(b, "abc"));
    ENC(SetVehicleDamageStatus, srpc_enc_vehicle_damage(b, 1, 2, 3, 4, 5));
    ENC(OnPlayerGiveTakeDamage, srpc_enc_give_take_damage(b, 1, 2, 3.0f, 4, 5));
    ENC(OnPlayerEditAttachedObject, srpc_enc_edit_attached_object(b, 1, 2, 3, 4, v, v, v, 5, 6));
    ENC(OnPlayerEditObject, srpc_enc_edit_object(b, 1, 2, 3, v, v));
    ENC(OnPlayerInteriorChange, srpc_enc_interior_change(b, 1));
    ENC(OnPlayerClickMap, srpc_enc_click_map(b, v));
    ENC(OnPlayerPickUpPickup, srpc_enc_picked_up_pickup(b, 1));
    ENC(OnPlayerSelectedMenuRow, srpc_enc_menu_select(b, 1));
    ENC(VehicleDeath, srpc_enc_vehicle_destroyed(b, 1));
    ENC(OnPlayerExitVehicle, srpc_enc_exit_vehicle(b, 1));
    ENC(OnPlayerCameraTarget, srpc_enc_camera_target(b, 1, 2, 3, 4));
    ENC(OnPlayerDamageActor, srpc_enc_actor_damage(b, 1, 2.0f, 3, 4));
    ENC(PlayerFootSync, srpc_enc_sync_foot(b, &foot));
    ENC(PlayerVehicleSync, srpc_enc_sync_vehicle(b, &car));
    ENC(PlayerPassengerSync, srpc_enc_sync_passenger(b, &pass));
    ENC(PlayerUnoccupiedSync, srpc_enc_sync_unoccupied(b, &unocc));
    ENC(PlayerTrailerSync, srpc_enc_sync_trailer(b, &trail));
    ENC(PlayerAimSync, srpc_enc_sync_aim(b, 1, v, v, 0.5f, 2, 3, 4));
    ENC(PlayerBulletSync, srpc_enc_sync_bullet(b, 1, 2, v, v, v, 3));
    ENC(PlayerStatsSync, srpc_enc_sync_stats(b, 1, 2));
    ENC(PlayerSpectatorSync, srpc_enc_sync_spectator(b, 1, 2, 3, v));
    ENC(PlayerRconCommand, srpc_enc_rcon(b, "abc"));

    /* Bytes a real server would parse: a command, and a weapons update. */
    {
        bs_t w;
        static const srpc_out_weapon_t wp[2] = {{0, 1, 1}, {2, 24, 300}};
        static const unsigned char want[] = {204, 0xFF, 0xFF, 0xFF, 0xFF, 0, 1, 1, 0, 2, 24, 0x2C, 0x01};

        bs_wr_init(&w, buf, sizeof buf);
        srpc_enc_command(&w, "/kill");
        CHECK(w.bits == (4 + 5) * 8 && buf[0] == 5 && buf[1] == 0 && !memcmp(buf + 4, "/kill", 5));

        bs_wr_init(&w, buf, sizeof buf);
        srpc_enc_sync_weapons(&w, 0xFFFF, 0xFFFF, wp, 2);
        CHECK(w.bits == sizeof want * 8 && !memcmp(buf, want, sizeof want));

        /* Overflow is reported, not written past the end. */
        bs_wr_init(&w, buf, 4);
        srpc_enc_chat(&w, "much too long for four bytes");
        CHECK(w.err);
    }
}

/* ---- fuzz: never read outside the message, never hang --------------------- */

static void h_any(unsigned id, bs_t *b) {}

static void test_fuzz(void)
{
    static unsigned char *data;
    samp_rpc_handlers_t none;
    unsigned seed = 12345;
    int i, j;

    memset(&none, 0, sizeof none);
    none.unknown = h_any;
    samp_rpc_set_handlers(&none);
    for (i = 0; i < 400000; i++) {
        int bytes;
        bs_t r;
        seed = seed * 1103515245 + 12345;
        bytes = (seed >> 16) % 300;
        /* Exact-size heap block, so ASan catches any overread. */
        data = malloc(bytes ? bytes : 1);
        for (j = 0; j < bytes; j++) {
            seed = seed * 1103515245 + 12345;
            data[j] = seed >> 16;
        }
        seed = seed * 1103515245 + 12345;
        bs_rd_init(&r, data, bytes * 8);
        if ((seed >> 16) & 1)
            samp_rpc_dispatch((seed >> 20) & 255, &r);
        else
            samp_sync_dispatch(200 + ((seed >> 20) % 13), &r);
        free(data);
    }
    samp_rpc_set_handlers(&handlers);
    checks++;
}

int main(void)
{
    samp_rpc_set_handlers(0);
    generated_tests();
    CHECK(cut_fail == 0);
    samp_rpc_set_handlers(&handlers);
    test_raw_bytes();
    test_huffman();
    test_dialog();
    test_label();
    test_textdraw();
    test_foot();
    test_vehicle_sync();
    test_bitfields();
    test_spectator();
    test_vehicle_stream();
    test_object();
    test_attached();
    test_menu();
    test_lists();
    test_bounds();
    test_misc();
    test_server_capture();
    test_stock_only();
    test_encoders();
    test_fuzz();
    printf("%d messages generated from NetCode, %d by hand; %d checks, %d failures\n",
           GEN_MESSAGES, GEN_MANUAL, checks, failures);
    return failures != 0;
}
