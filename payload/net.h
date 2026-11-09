#pragma once
/* IPv4 addresses are host-order u32, e.g. 127.0.0.1 == 0x7F000001. */
typedef void (*udp_rx_fn)(unsigned src_ip, unsigned src_port, unsigned dst_port,
                          const unsigned char *data, unsigned len);

void net_init(udp_rx_fn rx);
int  net_udp_send(unsigned dst_ip, unsigned dst_port, unsigned src_port,
                  const void *data, unsigned len);
void net_poll(void);

/* One client TCP connection, polled. Call net_tcp_tick once per frame as well as
 * net_poll. All of these return immediately. */
#define NET_TCP_CONNECTING 0
#define NET_TCP_OPEN       1
#define NET_TCP_PEER_DONE  2    /* clean FIN seen; net_tcp_recv may still have data */
#define NET_TCP_E_CLOSED   (-1)
#define NET_TCP_E_BUSY     (-2)
#define NET_TCP_E_RESET    (-3)
#define NET_TCP_E_TIMEOUT  (-4)
#define NET_TCP_E_GAP      (-5) /* data lost; the adapter never retransmits */

int  net_tcp_open(unsigned dst_ip, unsigned dst_port, unsigned now_ms);
int  net_tcp_status(void);                         /* NET_TCP_* */
int  net_tcp_send(const void *data, unsigned len); /* bytes queued, or <0 */
int  net_tcp_recv(void *data, unsigned max);       /* bytes read, 0 if none, or <0 */
void net_tcp_close(void);                          /* always frees the connection */
void net_tcp_tick(unsigned now_ms);

extern unsigned net_rx_frames, net_tx_frames, net_rx_dropped;
