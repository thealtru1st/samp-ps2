#pragma once
/* Writes the 40-character reply plus a terminator. Returns 0, or -1 if the
 * challenge has an unusable length. */
int samp_auth_answer(const unsigned char *challenge, unsigned len, char out[41]);
