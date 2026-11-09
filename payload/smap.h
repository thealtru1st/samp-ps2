#pragma once
int smap_init(void);
int smap_send(const void *buf, unsigned len);
int smap_recv(void *buf, unsigned max); /* 0 = nothing pending, else frame length */
