#pragma once
/* Runs SCM bytecode through the game's own interpreter, the same technique the
 * PC SA-MP client uses. Script opcodes are shared between PC and PS2, so this
 * avoids finding a separate address for every game action. */
typedef struct {
    unsigned char *p;
} scm_t;

void scm_begin(scm_t *s);
void scm_op(scm_t *s, unsigned opcode);
void scm_i8(scm_t *s, int v);
void scm_i32(scm_t *s, int v);
void scm_f(scm_t *s, float v);
void scm_str(scm_t *s, const char *text); /* variable-length string */
void scm_local(scm_t *s, unsigned index); /* result or argument in local var */
void scm_run(scm_t *s);
void scm_raw(scm_t *s, const volatile unsigned char *p, unsigned n);
int  scm_get_local(unsigned index);
void scm_set_local(unsigned index, int v);
