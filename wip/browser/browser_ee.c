/* Binds the browser to net.c on the EE. */
#include "browser_ee.h"
#include "net.h"

#define ADAPTER_DNS 0xC0000201u /* 192.0.2.1: PCSX2's internal DNS server */

static int ee_udp_send(void *c, unsigned ip, unsigned port, const void *d, unsigned n)
{
    (void)c;
    return net_udp_send(ip, port, BROWSER_LOCAL_PORT, d, n);
}

static void ee_poll(void *c, unsigned now) { (void)c; net_tcp_tick(now); }

static int ee_open(void *c, unsigned ip, unsigned port, unsigned now)
{
    (void)c;
    return net_tcp_open(ip, port, now);
}

/* NET_TCP_* and HTTP_TCP_* use the same values on purpose. */
static int ee_status(void *c) { (void)c; return net_tcp_status(); }
static int ee_send(void *c, const void *d, unsigned n) { (void)c; return net_tcp_send(d, n); }
static int ee_recv(void *c, void *d, unsigned n) { (void)c; return net_tcp_recv(d, n); }
static void ee_close(void *c) { (void)c; net_tcp_close(); }

static const browser_transport_t ee_transport = {
    0, ee_udp_send, ee_poll,
    { 0, ee_open, ee_status, ee_send, ee_recv, ee_close },
    ADAPTER_DNS
};

/* Always available, whatever happens to the list source. The adapter forwards
 * 127.0.0.1 to the PC, so this is a server on the same machine as PCSX2. */
static const char bundled[] = "127.0.0.1:7777\n";

void browser_ee_init(void)
{
    browser_init(&ee_transport, 0);
    browser_fav_import(bundled, sizeof bundled - 1);
}
