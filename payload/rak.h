#pragma once
/* Client side of SA-MP's RakNet dialect. Platform-independent: the caller supplies
 * the datagram transport and a millisecond clock, so the same code runs on the
 * host (for testing against a real server) and on the EE. */

enum { RAK_UNRELIABLE = 6, RAK_UNRELIABLE_SEQ = 7, RAK_RELIABLE = 8,
       RAK_RELIABLE_ORDERED = 9, RAK_RELIABLE_SEQ = 10 };

enum { RAK_IDLE, RAK_OPENING, RAK_OPENED, RAK_AUTHING, RAK_CONNECTED, RAK_FAILED, RAK_CLOSED };

enum { RAK_FAIL_NONE, RAK_FAIL_TIMEOUT, RAK_FAIL_FULL, RAK_FAIL_BANNED, RAK_FAIL_PASSWORD,
       RAK_FAIL_AUTH, RAK_FAIL_LOST, RAK_FAIL_KICKED };

typedef struct {
    void (*send)(const unsigned char *data, unsigned len);
    void (*on_connected)(unsigned token, unsigned player_index);
    void (*on_packet)(const unsigned char *data, unsigned bits);
    void (*on_closed)(int reason);
} rak_cb_t;

void rak_connect(unsigned server_ip, unsigned server_port, const char *password, const rak_cb_t *cb);
void rak_disconnect(void);
void rak_datagram(const unsigned char *data, unsigned len, unsigned now);
void rak_tick(unsigned now);
int  rak_send(const void *data, int bits, int reliability, int channel);
int  rak_rpc(int id, const void *data, int bits, int reliability, int channel);
int  rak_state(void);
int  rak_fail_reason(void);
unsigned rak_ping(void);
