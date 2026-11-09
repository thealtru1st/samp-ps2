#pragma once
#include "browser.h"

/* Real sockets. UDP goes only to addresses passed to host_allow_udp: everything
 * else is refused and counted, so a stray public address cannot be queried. */
void host_xport_init(browser_transport_t *t, unsigned dns_ip);
void host_allow_udp(unsigned ip);
extern int host_udp_blocked;
