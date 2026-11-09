#pragma once
/* Fixed-address debug block, read from the host over PINE. */
#define STATUS_ADDR 0x83F00000u
#define STATUS_MAGIC 0x504D4153u /* "SAMP" */
typedef struct {
    volatile unsigned magic, frames, stage, err;
    volatile unsigned v[12];
    volatile char log[4096];
    volatile unsigned logpos;
    volatile unsigned cmd_len;          /* host sets last: bytes of SCM in cmd[] */
    volatile int cmd_locals[4];         /* copied to/from script locals 0..3 */
    volatile unsigned char cmd[256];
    volatile unsigned kb_alive;   /* tools/kbd.py writes the frame counter here */
    volatile unsigned kb_seq;     /* bumped after kb_text changes */
    volatile unsigned edit_open;  /* non-zero while a text box is open */
    volatile char kb_text[160];
    volatile char edit_text[160]; /* what a box starts with, for the helper to read */
    volatile unsigned dbg_pad_mask; /* development: bit n forces pad field n to dbg_pad[n] */
    volatile short dbg_pad[20];     /* same order as menu_pad_t, see tools/rig.py */
    volatile unsigned dbg_fn, dbg_ret; /* development: call a game function, host sets fn last */
    volatile unsigned dbg_arg[4];
    volatile unsigned dbg_buf[16];  /* scratch that dbg_arg can point at */
    volatile unsigned where;        /* last step main.c started; 0 between frames */
    volatile unsigned where_op;     /* first opcode of the script scm_run is running */
} status_t;
#define ST ((status_t *)STATUS_ADDR)
