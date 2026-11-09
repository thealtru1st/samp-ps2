/* Minimal ethernet / IPv4 / UDP / client TCP for the DEV9 Sockets adapter.
 *
 * The Sockets adapter is a virtual NAT: the PS2 is always 192.0.2.100, the
 * gateway is 192.0.2.1 with a fixed MAC, and it answers for every off-subnet
 * address. So no DHCP client and no ARP cache are needed. IP checksums are
 * verified by the adapter (SocketAdapter::SendIP), so they must be correct.
 * UDP and TCP checksums are not checked there, but are still computed properly.
 *
 * The TCP here is one client connection, shaped by TCP_Session in PCSX2:
 *  - The adapter terminates TCP itself and relays to a host socket, so this link
 *    is an in-order memory path. It never retransmits to us: a segment missing
 *    here is gone, so a sequence gap aborts rather than waiting.
 *  - Frames are not dropped on the way in. NetRxThread stops while the SMAP FIFO
 *    is full, which is the backpressure, and our window bounds it further.
 *  - SendConnected ignores the payload of the first packet after the SYN-ACK, so
 *    the handshake ACK must be its own segment, with data strictly after it.
 *  - We must not retransmit data. CheckNumbers treats a repeat of a recently seen
 *    data seq as Bad and resets the connection. smap_send either hands a frame to
 *    the adapter synchronously or refuses it, so "unsent" is all we ever retry.
 *    Repeating a SYN is explicitly fine.
 *  - A FIN must not carry data (ValidateEmptyPacket resets on it), and a failed
 *    host connect is silent, so connecting can end in a timeout with no RST.
 *  - No timestamps or window scale are offered: without them the adapter sends
 *    full MSS segments and no per-packet options.
 */
#include "net.h"
#include "smap.h"
#include "util.h"

#define MY_IP 0xC0000264u /* 192.0.2.100 */

static const unsigned char gw_mac[6] = {0x76, 0x6D, 0xF4, 0x63, 0x30, 0x31};
static const unsigned char my_mac[6] = {0x00, 0x04, 0x1F, 0x82, 0x53, 0x41};

static udp_rx_fn on_udp;
static unsigned short ip_id;
static unsigned char frame[1536] __attribute__((aligned(16)));
/* Larger than the MTU: the host reassembles fragmented UDP and the adapter passes
 * the whole datagram on as one frame, up to its 2044 byte NetPacket. */
static unsigned char rxbuf[2048] __attribute__((aligned(16)));

unsigned net_rx_frames, net_tx_frames, net_rx_dropped;

static void put16(unsigned char *p, unsigned v) { p[0] = v >> 8; p[1] = v; }
static void put32(unsigned char *p, unsigned v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
static unsigned get16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
static unsigned get32(const unsigned char *p)
{
    return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static unsigned csum_add(unsigned sum, const unsigned char *p, unsigned len)
{
    unsigned i;
    for (i = 0; i + 1 < len; i += 2)
        sum += (p[i] << 8) | p[i + 1];
    if (i < len)
        sum += p[i] << 8;
    return sum;
}

static unsigned csum_fold(unsigned sum)
{
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum & 0xFFFF;
}

void net_init(udp_rx_fn rx)
{
    on_udp = rx;
    smap_init();
}

/* Fills in the ethernet and IPv4 headers of the shared transmit frame. */
static void ip_header(unsigned proto, unsigned dst_ip, unsigned total)
{
    unsigned char *eth = frame, *ip = frame + 14;

    memcpy(eth, gw_mac, 6);
    memcpy(eth + 6, my_mac, 6);
    put16(eth + 12, 0x0800);

    ip[0] = 0x45; ip[1] = 0;
    put16(ip + 2, total);
    put16(ip + 4, ++ip_id);
    put16(ip + 6, 0x4000); /* DF */
    ip[8] = 64; ip[9] = proto;
    put16(ip + 10, 0);
    put32(ip + 12, MY_IP);
    put32(ip + 16, dst_ip);
    put16(ip + 10, csum_fold(csum_add(0, ip, 20)));
}

int net_udp_send(unsigned dst_ip, unsigned dst_port, unsigned src_port,
                 const void *data, unsigned len)
{
    unsigned char *ip = frame + 14, *udp = frame + 34;
    unsigned ulen = 8 + len, sum;
    int r;

    if (len > 1472)
        return -1;
    ip_header(17, dst_ip, 20 + ulen);

    put16(udp, src_port);
    put16(udp + 2, dst_port);
    put16(udp + 4, ulen);
    put16(udp + 6, 0);
    memcpy(udp + 8, data, len);

    sum = csum_add(0, ip + 12, 8); /* pseudo header: src + dst */
    sum += 17 + ulen;
    sum = csum_fold(csum_add(sum, udp, ulen));
    put16(udp + 6, sum ? sum : 0xFFFF);

    r = smap_send(frame, 34 + ulen);
    if (r == 0)
        net_tx_frames++;
    return r;
}

/* ---- TCP ---- */

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

#define TCP_RX_SIZE 8192 /* power of two; also the largest window we advertise */
#define TCP_TX_SIZE 512
#define TCP_MSS     1460
#define TCP_MIN_MSS 536
#define TCP_SYN_RETRY_MS 1000
#define TCP_CONNECT_MS   10000

enum { T_CLOSED, T_SYN_SENT, T_OPEN, T_DEAD };

static struct {
    unsigned char state, hs_acked, need_ack, peer_fin, fin_sent;
    int err;
    unsigned ip, rport, lport;
    unsigned snd_nxt, snd_una, snd_wnd, mss, rcv_nxt, adv;
    unsigned t_open, t_syn;
    unsigned rx_head, rx_len, tx_off, tx_len;
    unsigned char rx[TCP_RX_SIZE], tx[TCP_TX_SIZE];
} t;
static unsigned tcp_serial;

static int tcp_xmit(unsigned flags, unsigned seq, const void *data, unsigned len)
{
    unsigned char *ip = frame + 14, *tcp = frame + 34;
    unsigned hlen = (flags & TCP_SYN) ? 24 : 20;
    unsigned tlen = hlen + len, win = TCP_RX_SIZE - t.rx_len, sum;
    int r;

    ip_header(6, t.ip, 20 + tlen);

    put16(tcp, t.lport);
    put16(tcp + 2, t.rport);
    put32(tcp + 4, seq);
    put32(tcp + 8, (flags & TCP_ACK) ? t.rcv_nxt : 0);
    tcp[12] = (hlen / 4) << 4;
    tcp[13] = flags;
    put16(tcp + 14, win);
    put16(tcp + 16, 0);
    put16(tcp + 18, 0);
    if (flags & TCP_SYN) {
        tcp[20] = 2; tcp[21] = 4; /* MSS */
        put16(tcp + 22, TCP_MSS);
    }
    if (len)
        memcpy(tcp + hlen, data, len);

    sum = csum_add(0, ip + 12, 8);
    sum += 6 + tlen;
    put16(tcp + 16, csum_fold(csum_add(sum, tcp, tlen)));

    r = smap_send(frame, 34 + tlen);
    if (r == 0) {
        net_tx_frames++;
        if (flags & TCP_ACK) {
            t.adv = win;
            t.need_ack = 0;
        }
    }
    return r;
}

/* Unrecoverable: tell the adapter so it drops the host socket straight away.
 * Bytes already received stay readable. */
static void tcp_abort(int err)
{
    tcp_xmit(TCP_RST | TCP_ACK, t.snd_nxt, 0, 0);
    t.state = T_DEAD;
    t.err = err;
}

/* Sends whatever is owed, in the only order the adapter accepts. Anything
 * smap_send refuses simply stays owed for the next call. */
static void tcp_output(void)
{
    unsigned room;

    /* Nothing may follow our FIN: the adapter closes the session on seeing it, and
     * a stray ACK would land on a dead session. */
    if (t.state != T_OPEN || t.fin_sent)
        return;
    if (!t.hs_acked) {
        if (tcp_xmit(TCP_ACK, t.snd_nxt, 0, 0))
            return;
        t.hs_acked = 1;
    }
    while (t.tx_len) {
        unsigned n = t.tx_len, used = t.snd_nxt - t.snd_una;
        room = t.snd_wnd > used ? t.snd_wnd - used : 0;
        if (n > room) n = room;
        if (n > t.mss) n = t.mss;
        if (n == 0)
            break;
        if (tcp_xmit(TCP_ACK | TCP_PSH, t.snd_nxt, t.tx + t.tx_off, n))
            return;
        t.snd_nxt += n;
        t.tx_off += n;
        t.tx_len -= n;
    }
    if (t.peer_fin && !t.fin_sent && !t.tx_len) {
        if (tcp_xmit(TCP_FIN | TCP_ACK, t.snd_nxt, 0, 0))
            return;
        t.snd_nxt++;
        t.fin_sent = 1;
    }
    /* The adapter stops at a closed window and never probes, so reopening it is
     * our job. Otherwise only speak up once a full segment of room has appeared. */
    room = TCP_RX_SIZE - t.rx_len;
    if (t.need_ack || (room > t.adv && (t.adv == 0 || room - t.adv >= t.mss)))
        tcp_xmit(TCP_ACK, t.snd_nxt, 0, 0);
}

static void tcp_store(const unsigned char *d, unsigned n)
{
    unsigned pos = (t.rx_head + t.rx_len) & (TCP_RX_SIZE - 1), first = TCP_RX_SIZE - pos;

    if (first > n)
        first = n;
    memcpy(t.rx + pos, d, first);
    memcpy(t.rx, d + first, n - first);
    t.rx_len += n;
}

static void tcp_handshake(const unsigned char *s, unsigned hlen, unsigned flags,
                          unsigned seq, unsigned ack)
{
    unsigned i = 20;

    if ((flags & (TCP_SYN | TCP_ACK)) != (TCP_SYN | TCP_ACK) || ack != t.snd_nxt)
        return;
    t.rcv_nxt = seq + 1;
    t.snd_una = ack;
    t.snd_wnd = get16(s + 14);
    t.mss = TCP_MIN_MSS;
    while (i + 1 < hlen && s[i]) { /* peer MSS; other options are of no use to us */
        if (s[i] == 1) {
            i++;
            continue;
        }
        if (s[i + 1] < 2)
            break;
        if (s[i] == 2 && s[i + 1] == 4 && i + 4 <= hlen)
            t.mss = get16(s + i + 2);
        i += s[i + 1];
    }
    if (t.mss > TCP_MSS)
        t.mss = TCP_MSS;
    if (t.mss < TCP_MIN_MSS)
        t.mss = TCP_MIN_MSS;
    t.state = T_OPEN; /* tcp_output sends the bare ACK */
}

static void tcp_input(unsigned src_ip, const unsigned char *s, unsigned len)
{
    unsigned hlen, flags, seq, ack, n;
    const unsigned char *data;
    int behind;

    if ((t.state != T_SYN_SENT && t.state != T_OPEN) || src_ip != t.ip ||
        get16(s) != t.rport || get16(s + 2) != t.lport)
        return;
    hlen = (s[12] >> 4) * 4;
    if (hlen < 20 || hlen > len)
        return;
    flags = s[13];
    seq = get32(s + 4);
    ack = get32(s + 8);
    data = s + hlen;
    n = len - hlen;

    if (flags & TCP_RST) {
        t.state = T_DEAD;
        if (!t.peer_fin) /* after a FIN it is only teardown: everything arrived */
            t.err = NET_TCP_E_RESET;
        return;
    }
    if (t.state == T_SYN_SENT) {
        tcp_handshake(s, hlen, flags, seq, ack);
        return;
    }
    if ((flags & TCP_ACK) && (int)(ack - t.snd_una) >= 0 && (int)(t.snd_nxt - ack) >= 0) {
        t.snd_una = ack;
        t.snd_wnd = get16(s + 14);
    }
    behind = (int)(t.rcv_nxt - seq);
    if ((n || (flags & TCP_FIN)) && behind < 0) {
        tcp_abort(NET_TCP_E_GAP); /* see the header comment: this data is never coming */
        return;
    }
    if (n) {
        if ((unsigned)behind < n) {
            unsigned fresh = n - behind;
            if (fresh > TCP_RX_SIZE - t.rx_len) { /* overran our window: same as a gap */
                tcp_abort(NET_TCP_E_GAP);
                return;
            }
            tcp_store(data + behind, fresh);
            t.rcv_nxt += fresh;
        }
        t.need_ack = 1;
    }
    if ((flags & TCP_FIN) && !t.peer_fin && seq + n == t.rcv_nxt) {
        t.rcv_nxt++;
        t.peer_fin = 1;
        t.need_ack = 1;
    }
}

int net_tcp_open(unsigned dst_ip, unsigned dst_port, unsigned now_ms)
{
    if (t.state == T_SYN_SENT || t.state == T_OPEN)
        return NET_TCP_E_BUSY;
    memset(&t, 0, sizeof t);
    tcp_serial++;
    t.ip = dst_ip;
    t.rport = dst_port;
    /* A fresh port each time: the adapter keys sessions on the port pair. */
    t.lport = 49152 + tcp_serial % 16000;
    t.snd_una = ((now_ms << 8) ^ (tcp_serial << 20)) & 0x3FFFFFFF;
    t.snd_nxt = t.snd_una + 1;
    t.t_open = t.t_syn = now_ms;
    t.state = T_SYN_SENT;
    if (tcp_xmit(TCP_SYN, t.snd_una, 0, 0))
        t.t_syn = now_ms - TCP_SYN_RETRY_MS;
    return 0;
}

int net_tcp_status(void)
{
    if (t.state == T_CLOSED)
        return NET_TCP_E_CLOSED;
    if (t.err)
        return t.err;
    if (t.peer_fin)
        return NET_TCP_PEER_DONE;
    return t.state == T_SYN_SENT ? NET_TCP_CONNECTING : NET_TCP_OPEN;
}

int net_tcp_send(const void *data, unsigned len)
{
    if (t.state != T_OPEN || t.peer_fin)
        return NET_TCP_E_CLOSED;
    if (t.tx_len == 0)
        t.tx_off = 0;
    if (len > TCP_TX_SIZE - t.tx_off - t.tx_len)
        len = TCP_TX_SIZE - t.tx_off - t.tx_len;
    memcpy(t.tx + t.tx_off + t.tx_len, data, len);
    t.tx_len += len;
    tcp_output();
    return len;
}

int net_tcp_recv(void *data, unsigned max)
{
    unsigned char *d = data;
    unsigned n = t.rx_len < max ? t.rx_len : max, first = TCP_RX_SIZE - t.rx_head;

    if (t.state == T_CLOSED)
        return NET_TCP_E_CLOSED;
    if (n == 0)
        return 0;
    if (first > n)
        first = n;
    memcpy(d, t.rx + t.rx_head, first);
    memcpy(d + first, t.rx, n - first);
    t.rx_head = (t.rx_head + n) & (TCP_RX_SIZE - 1);
    t.rx_len -= n;
    tcp_output();
    return n;
}

void net_tcp_close(void)
{
    /* RST, not FIN: after a FIN from us the adapter keeps the session until the
     * remote also closes, which may be never. Once both FINs are out there is
     * nothing left to tear down. */
    if ((t.state == T_OPEN && !t.fin_sent) || t.state == T_SYN_SENT)
        tcp_xmit(TCP_RST | TCP_ACK, t.snd_nxt, 0, 0);
    t.state = T_CLOSED;
}

void net_tcp_tick(unsigned now_ms)
{
    if (t.state == T_SYN_SENT) {
        if ((int)(now_ms - t.t_open) >= TCP_CONNECT_MS) {
            tcp_abort(NET_TCP_E_TIMEOUT);
        } else if ((int)(now_ms - t.t_syn) >= TCP_SYN_RETRY_MS) {
            t.t_syn = now_ms;
            tcp_xmit(TCP_SYN, t.snd_una, 0, 0);
        }
    }
    tcp_output();
}

static void handle_frame(const unsigned char *f, unsigned len)
{
    const unsigned char *ip, *udp;
    unsigned ihl, total, ulen;

    net_rx_frames++;
    if (len < 42 || get16(f + 12) != 0x0800)
        return;
    ip = f + 14;
    ihl = (ip[0] & 15) * 4;
    total = get16(ip + 2);
    if ((ip[0] >> 4) != 4 || ihl < 20 || total + 14 > len || total < ihl + 8) {
        net_rx_dropped++;
        return;
    }
    if (get16(ip + 6) & 0x3FFF) { /* no fragments */
        net_rx_dropped++;
        return;
    }
    if (ip[9] == 6 && total >= ihl + 20) {
        tcp_input(get32(ip + 12), ip + ihl, total - ihl);
        return;
    }
    if (ip[9] != 17) {
        net_rx_dropped++;
        return;
    }
    udp = ip + ihl;
    ulen = get16(udp + 4);
    if (ulen < 8 || ihl + ulen > total) {
        net_rx_dropped++;
        return;
    }
    if (on_udp)
        on_udp(get32(ip + 12), get16(udp), get16(udp + 2), udp + 8, ulen - 8);
}

void net_poll(void)
{
    int n, budget = 32; /* bounded so a flood cannot stall a game frame */
    while (budget-- > 0 && (n = smap_recv(rxbuf, sizeof rxbuf)) > 0)
        handle_frame(rxbuf, n);
    tcp_output(); /* one cumulative ACK for the whole batch */
}
