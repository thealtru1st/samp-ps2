#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    int adapter_errors;      /* Console.Error calls from the real adapter code */
    int bad_ip_csum, bad_l4_csum;
    int tcp_data_segments, dns_queries, udp_sent, udp_blocked;
    int netpacket_overflows;
    int zero_window_acks;    /* segments from us advertising a closed window */
    int refuse_tx, tx_refused; /* set refuse_tx to make smap_send fail that many times */
    int refuse_bare_acks;      /* the same, but only for pure ACKs */
    unsigned long fifo_peak_frames;
} rig_counters_t;
extern rig_counters_t rig;

void rig_start(int verbose);
void rig_stop(void);
void rig_allow_udp(unsigned ip);
int  rig_tcp_sessions(void);
int  rig_udp_sessions(void);
void rig_drop_next_tcp_data(void);
void rig_replay_last_tcp_data(void);
#ifdef __cplusplus
}
#endif
