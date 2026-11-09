/* Bare-metal SMAP (PS2 network adapter) driver running on the EE.
 *
 * PCSX2 exposes the DEV9 register space to the EE at physical 0x14000000
 * (Memory.cpp: tlb_fallback_7 -> DEV9read/write), reached here through kseg1.
 * That lets us use the adapter with no IOP module at all. Polled, PIO only:
 * interrupts stay masked so the IOP never sees anything from us.
 */
#include "smap.h"

#define REG(o)   (0xB4000000u + (o))
#define R8(o)    (*(volatile unsigned char  *)REG(o))
#define R16(o)   (*(volatile unsigned short *)REG(o))
#define R32(o)   (*(volatile unsigned int   *)REG(o))

#define TXFIFO_CTRL      0x1000
#define TXFIFO_WR_PTR    0x1004
#define TXFIFO_FRAME_INC 0x1010
#define TXFIFO_DATA      0x1100
#define RXFIFO_CTRL      0x1030
#define RXFIFO_RD_PTR    0x1034
#define RXFIFO_FRAME_CNT 0x103C
#define RXFIFO_FRAME_DEC 0x1040
#define RXFIFO_DATA      0x1200
#define EMAC3_TXMODE0_L  0x2008
#define EMAC3_TXMODE0_H  0x200A
#define BD_TX            0x3000
#define BD_RX            0x3200
#define BD_MODE          0x0102

#define BD_TX_READY  0x8000
#define BD_TX_GENFCS 0x0200
#define BD_TX_GENPAD 0x0100
#define BD_RX_EMPTY  0x8000
#define E3_TX_GNP_0  0x8000 /* top bit of the register, held in the L half */
#define FIFO_MASK    16383
#define NBD          64

static unsigned tx_ptr, tx_bd, rx_bd;

int smap_init(void)
{
    int i;
    R8(BD_MODE) = 0;
    R8(TXFIFO_CTRL) = 1;
    R8(RXFIFO_CTRL) = 1;
    for (i = 0; i < NBD; i++) {
        R16(BD_TX + i * 8 + 0) = 0;
        R16(BD_RX + i * 8 + 0) = BD_RX_EMPTY;
    }
    R8(RXFIFO_CTRL) = 1;
    tx_ptr = tx_bd = rx_bd = 0;
    return 0;
}

int smap_send(const void *buf, unsigned len)
{
    const unsigned char *p = buf;
    unsigned start = tx_ptr, o = BD_TX + tx_bd * 8, i;

    if (len > 1514)
        return -1;
    if (R16(o) & BD_TX_READY)
        return -2;
    for (i = 0; i < len; i += 4) {
        unsigned w = p[i];
        if (i + 1 < len) w |= (unsigned)p[i + 1] << 8;
        if (i + 2 < len) w |= (unsigned)p[i + 2] << 16;
        if (i + 3 < len) w |= (unsigned)p[i + 3] << 24;
        R32(TXFIFO_DATA) = w;
    }
    tx_ptr = (tx_ptr + ((len + 3) & ~3u)) & FIFO_MASK;
    R16(o + 4) = len;
    R16(o + 6) = 0x1000 + start;
    R16(o + 0) = BD_TX_READY | BD_TX_GENFCS | BD_TX_GENPAD;
    R8(TXFIFO_FRAME_INC) = 1;
    R32(EMAC3_TXMODE0_L) = E3_TX_GNP_0; /* the H half of this write is what fires TX */
    /* PCSX2 sends synchronously, so a slot still marked ready was never looked at.
     * That means the adapter is at a different slot: DEV9 is not part of savestates,
     * so loading one brings it back blank. Set it up again and resend once. */
    if (R16(o) & BD_TX_READY) {
        static int retrying;
        int r = -3;
        smap_init();
        if (!retrying) {
            retrying = 1;
            r = smap_send(buf, len);
            retrying = 0;
        }
        return r;
    }
    tx_bd = (tx_bd + 1) & (NBD - 1);
    return 0;
}

int smap_recv(void *buf, unsigned max)
{
    unsigned char *p = buf;
    unsigned o = BD_RX + rx_bd * 8, len, i;

    if (R16(o) & BD_RX_EMPTY)
        return 0;
    len = R16(o + 4);
    R32(RXFIFO_RD_PTR) = (R16(o + 6) - 0x4000) & FIFO_MASK;
    for (i = 0; i < len; i += 4) {
        unsigned w = R32(RXFIFO_DATA);
        if (i < max) p[i] = w;
        if (i + 1 < max) p[i + 1] = w >> 8;
        if (i + 2 < max) p[i + 2] = w >> 16;
        if (i + 3 < max) p[i + 3] = w >> 24;
    }
    R8(RXFIFO_FRAME_DEC) = 1;
    R16(o) = BD_RX_EMPTY;
    rx_bd = (rx_bd + 1) & (NBD - 1);
    return len > max ? (int)max : (int)len;
}
