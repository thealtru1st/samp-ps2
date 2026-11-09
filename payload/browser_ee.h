#pragma once
#include "browser.h"

/* Call once after net_init. Then call browser_tick each frame after net_poll, and
 * pass datagrams for BROWSER_LOCAL_PORT to browser_on_udp from the UDP callback. */
void browser_ee_init(void);
