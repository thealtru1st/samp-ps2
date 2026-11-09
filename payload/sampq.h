#pragma once
/* SA-MP query protocol: packet building and reply parsing. Pure functions, no
 * I/O and no state. Every parser treats its input as hostile: all lengths are
 * bounds checked and strings are truncated and cleaned into fixed fields.
 *
 * Request: "SAMP" + 4 IP bytes + port (LE) + opcode [+ 4 byte nonce for 'p'].
 * The reply echoes those 11 bytes, then the opcode's body.
 * IPv4 addresses are host-order u32, as in net.h.
 */
#define SAMPQ_HDR      11
#define SAMPQ_PING_LEN 15
#define SAMPQ_HOST_LEN 64 /* open.mp caps hostname at 63, gamemode/language at 39 */
#define SAMPQ_MODE_LEN 40
#define SAMPQ_LANG_LEN 40
#define SAMPQ_RULE_NAME_LEN  32
#define SAMPQ_RULE_VALUE_LEN 64
#define SAMPQ_PLAYER_NAME_LEN 25

typedef struct {
    unsigned char  password;
    unsigned short players, max_players;
    char hostname[SAMPQ_HOST_LEN];
    char gamemode[SAMPQ_MODE_LEN];
    char language[SAMPQ_LANG_LEN];
} sampq_info_t;

typedef struct {
    char name[SAMPQ_RULE_NAME_LEN];
    char value[SAMPQ_RULE_VALUE_LEN];
} sampq_rule_t;

typedef struct {
    char name[SAMPQ_PLAYER_NAME_LEN];
    int  score;
} sampq_player_t;

/* Writes the request to out (SAMPQ_PING_LEN bytes is always enough). nonce is
 * only used for 'p'. Returns the length. */
unsigned sampq_build(unsigned char *out, unsigned ip, unsigned port, char op,
                     const unsigned char *nonce);

/* Returns the opcode if d is a reply to a query sent to ip:port, else -1. */
int sampq_check(const unsigned char *d, unsigned len, unsigned ip, unsigned port);

/* Each returns 0 on success, -1 if the body is malformed or truncated. */
int sampq_parse_info(const unsigned char *d, unsigned len, sampq_info_t *out);
int sampq_parse_ping(const unsigned char *d, unsigned len, const unsigned char *nonce);

/* Store up to max entries. *total gets the count the server declared, *stored
 * how many were kept. The whole reply is walked, so a truncated one fails. */
int sampq_parse_rules(const unsigned char *d, unsigned len, sampq_rule_t *out,
                      unsigned max, unsigned *stored, unsigned *total);
int sampq_parse_players(const unsigned char *d, unsigned len, sampq_player_t *out,
                        unsigned max, unsigned *stored, unsigned *total);
