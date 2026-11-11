/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "../native/t3p_usb_keys.c"

/* Inject USB transactions at the SIE registers. DMA buffers are filled here
 * as the controller would fill them. No device or OS USB stack is involved. */
static uint32_t regs[0x600 / 4], port_regs[0x40 / 4];
static unsigned out_rearms;

static uint32_t *reg(unsigned addr)
{
    if (addr >= 0xfd3500)
        return &port_regs[(addr - 0xfd3500) / 4];
    assert(addr >= 0xfd2000 && addr < 0xfd2600);
    return &regs[(addr - 0xfd2000) / 4];
}

static uint32_t read_reg(void *context, uint32_t addr)
{
    (void)context;
    uint32_t value = *reg(addr);
    if (addr == TX_IRQ || addr == RX_IRQ || addr == BUS_IRQ)
        *reg(addr) = 0;
    return value;
}

static void write_reg(void *context, uint32_t addr, uint32_t value)
{
    (void)context;
    if (addr == RX_CSR && (value & RX_FLUSH) && (value & RX_READY)) {
        assert(*reg(EP2_RX_DMA) == (uint32_t)(uintptr_t)rx);
        ++out_rearms;
        value &= ~(RX_FLUSH | RX_READY);
    }
    *reg(addr) = value;
}

static void control(unsigned type, unsigned request, unsigned value,
                    unsigned length, uint32_t now)
{
    ep0[0] = (uint8_t)type;
    ep0[1] = (uint8_t)request;
    ep0[2] = (uint8_t)value;
    ep0[3] = (uint8_t)(value >> 8);
    ep0[4] = ep0[5] = 0;
    ep0[6] = (uint8_t)length;
    ep0[7] = (uint8_t)(length >> 8);
    *reg(COUNT0) = 8;
    *reg(CSR0) = RX_READY;
    *reg(TX_IRQ) = 1;
    t3p_usb_keys_poll(now);
}

static void send_mask(unsigned keys, uint32_t now)
{
    rx[0] = 'T'; rx[1] = '3'; rx[2] = 'P'; rx[3] = '1'; rx[4]++;
    rx[5] = (uint8_t)keys; rx[6] = (uint8_t)(keys >> 8);
    *reg(RX_COUNT) = PACKET;
    *reg(RX_CSR) = 1;
    *reg(TX_CSR) = 0;
    t3p_usb_keys_poll(now);
}

int main(void)
{
    const struct t3p_bsp_io io = {.read32 = read_reg, .write32 = write_reg};
    *reg(PORT_PU0) = 2;
    *reg(PORT_PD0) = 3;
    t3p_usb_keys_start(&io, 0);
    t3p_usb_keys_poll(99);
    assert(*reg(POWER) == 0);
    t3p_usb_keys_poll(100);
    assert(*reg(PORT_PU0) == 1 && *reg(PORT_PD0) == 0);
    assert(*reg(POWER) == 0x40 && (*reg(PORT_CON) & 1));
    *reg(BUS_IRQ) = BUS_RESET;
    t3p_usb_keys_poll(0);
    control(0x80, 6, 0x0100, 8, 1);
    assert(*reg(EP0_WRITE_SIZE) == 8 && ep0[1] == 1 && ep0[7] == 64);
    control(0x80, 6, 0x0200, 255, 2);
    assert(*reg(EP0_WRITE_SIZE) == 41 && ep0[29] == 0x82 && ep0[36] == 2);
#ifdef T3P_USB_CAPTURE_FAST
    assert(ep0[33] == 1 && ep0[40] == 4);
#else
    assert(ep0[33] == 4 && ep0[40] == 4);
#endif
    control(0x81, 6, 0x2200, 255, 3);
    assert(*reg(EP0_WRITE_SIZE) == sizeof(report_desc) && ep0[1] == 0x61);
#ifdef T3P_USB_MANUFACTURER_DESC
    control(0x80, 6, 0x0302, 255, 3);
    assert(*reg(EP0_WRITE_SIZE) == sizeof(manufacturer_desc));
    for (unsigned i = 0; i < sizeof(manufacturer_desc); ++i)
        assert(ep0[i] == manufacturer_desc[i]);
    control(0x80, 6, 0x0100, 18, 3);
    assert(ep0[14] == 2);
#endif
    control(0, 5, 7, 0, 4);
    assert(*reg(FADDR) == 0 && *reg(CSR0) == (SERVICE_RX | DATA_END));
    *reg(CSR0) = 0;
    t3p_usb_keys_poll(5);
    assert(*reg(FADDR) == 0);
    *reg(TX_IRQ) = 1;
    t3p_usb_keys_poll(6);
    assert(*reg(FADDR) == 7);
    control(0, 9, 1, 0, 7);
    assert(*reg(EP2_RX_LIMIT) == PACKET);
    *reg(CSR0) = 0;
    send_mask(T3P_FIRE | T3P_FORWARD, 100);
    assert(*reg(EP2_WRITE_SIZE) == PACKET && tx[4] == rx[4] && tx[5] == rx[5]);
    assert(t3p_usb_keys_take(102) == (T3P_FIRE | T3P_FORWARD));
    assert(t3p_usb_keys_take(599) == (T3P_FIRE | T3P_FORWARD));
    assert(t3p_usb_keys_take(600) == 0);
    send_mask(T3P_ESCAPE, 620);
    send_mask(0, 625);
    assert(t3p_usb_keys_take(630) == T3P_ESCAPE && t3p_usb_keys_take(630) == 0);
    send_mask(0x8000, 640);
    assert(*reg(TX_CSR) == 0 && !t3p_usb_keys_take(640));
    control(0x21, 9, 0x0200, PACKET, 645);
    assert(*reg(CSR0) == SERVICE_RX);
    for (unsigned i = 0; i < PACKET; ++i)
        ep0[i] = rx[i];
    ep0[5] = T3P_USE; ep0[6] = 0;
    *reg(COUNT0) = PACKET;
    *reg(CSR0) = RX_READY;
    t3p_usb_keys_poll(650);
    assert(t3p_usb_keys_take(650) == T3P_USE && tx[5] == T3P_USE);
    *reg(BUS_IRQ) = BUS_RESET;
    t3p_usb_keys_poll(660);
    assert(t3p_usb_keys_take(660) == 0 && *reg(FADDR) == 0);
    assert(t3p_usb_keys_debug(3) == 2);
#ifdef T3P_USB_MANUFACTURER_DESC
    assert(t3p_usb_keys_debug(4) == 8);
#else
    assert(t3p_usb_keys_debug(4) == 6);
#endif
    assert(out_rearms == 4 && t3p_usb_keys_debug(8) == 4);
    assert(t3p_usb_keys_debug(5) == 0x21090200 && t3p_usb_keys_debug(7) == 4);
#ifdef T3P_USB_CAPTURE
    control(0, 9, 1, 0, 661);
    *reg(CSR0) = 0;
    rx[7] = CAPTURE_START;
    send_mask(T3P_FORWARD, 662);
    assert(tx[3] == '1' && capture_requested && t3p_usb_keys_take(662) == T3P_FORWARD);
    uint8_t pixels[T3P_FRAME_PIXELS] = {0x71};
    uint16_t palette[256] = {0x5678};
    t3p_usb_capture_frame(pixels, palette);
    pixels[0] = 0x19;
    palette[0] = 0;
    unsigned chunks = (sizeof(capture) + CHUNK_DATA - 1) / CHUNK_DATA;
    for (unsigned i=0; i<chunks; ++i) {
        *reg(TX_CSR) = 0;
        t3p_usb_keys_poll(663 + i);
        assert(tx[3] == 'F' && tx[4] == rx[4]);
        assert(((unsigned)tx[5] | (unsigned)tx[6] << 8) == i);
        if (i == 0) assert(tx[7] == 0x78 && tx[8] == 0x56);
        if (i == 512 / CHUNK_DATA) assert(tx[7 + 512 % CHUNK_DATA] == 0x71);
    }
    assert(!capture_active);
    puts("PASS: complete capture from an immutable frame and palette");
#ifdef T3P_USB_CAPTURE_FAST
    for (unsigned x = 0; x < T3P_FRAME_SIDE; ++x)
        for (unsigned y = 0; y < T3P_FRAME_SIDE; ++y)
            pixels[x * T3P_FRAME_SIDE + y] = (uint8_t)(17 * x + y);
    rx[7] = CAPTURE_FAST;
    send_mask(0, 950);
    unsigned sequence = rx[4];
    t3p_usb_capture_frame(pixels, palette);
    memset(pixels, 0xff, sizeof(pixels));
    for (unsigned i = 0; i < 73; ++i) {
        if (i == 30) {
            rx[7] = 0;
            send_mask(T3P_USE, 980);
            assert(tx[3] == '1' && t3p_usb_keys_take(980) == T3P_USE);
        }
        *reg(TX_CSR) = 0;
        t3p_usb_keys_poll(951 + i);
        assert(tx[3] == 'F' && tx[4] == sequence && tx[5] == i && tx[6] == 0);
        for (unsigned j = 0; j < CHUNK_DATA; ++j) {
            unsigned offset = i * CHUNK_DATA + j;
            if (offset < PALETTE_BYTES) continue;
            offset -= PALETTE_BYTES;
            unsigned expected = offset < FAST_SIDE * FAST_SIDE ?
                34 * (offset / FAST_SIDE) + 2 * (offset % FAST_SIDE) : 0;
            assert(tx[CHUNK_HEADER + j] == (uint8_t)expected);
        }
    }
    assert(!capture_active);
    rx[7] = CAPTURE_FAST;
    send_mask(0, 1040);
    rx[7] = CAPTURE_STOP;
    send_mask(0, 1044);
    t3p_usb_capture_frame(pixels, palette);
    *reg(TX_CSR) = 0;
    t3p_usb_keys_poll(1045);
    assert(*reg(TX_CSR) == 0 && !capture_active && !capture_requested);
    puts("PASS: 73-packet 60x60 capture with correct sampling, interleaved key echo, and cancellation");
#endif
#endif
    puts("PASS: D+ attach pull-up, HID descriptors, deferred address, report echo, short presses and lease/reset release");
    return 0;
}
