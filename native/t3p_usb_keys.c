/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_bsp.h"
#include "t3p_input.h"
#include "t3p_usb_keys.h"
#ifdef T3P_USB_CAPTURE
#include <string.h>
#include "t3p_present.h"
#endif
#ifdef T3P_USB_CAPTURE_FAST
#define USB_REVISION 0x66
#define USB_REVISION_DIGIT '6'
#define USB_IN_INTERVAL 1
#elif defined(T3P_USB_CAPTURE)
#define USB_REVISION 0x65
#define USB_REVISION_DIGIT '5'
#define USB_IN_INTERVAL 4
#else
#define USB_REVISION 0x64
#define USB_REVISION_DIGIT '4'
#define USB_IN_INTERVAL 4
#endif

/* BR35 direct SIE registers. Cross-checked against stock 3.0.3 USB device
 * setup and the SDK register map. The app owns only the side USB device. */
enum {
    FADDR = 0xfd2000, POWER = 0xfd2004, TX_IRQ = 0xfd2008, RX_IRQ = 0xfd2010,
    BUS_IRQ = 0xfd2018, TX_ENABLE = 0xfd201c, RX_ENABLE = 0xfd2024,
    BUS_ENABLE = 0xfd202c, FRAME_LO = 0xfd2030, FRAME_HI = 0xfd2034, CSR0 = 0xfd2044, COUNT0 = 0xfd2058,
    TX_MAX = 0xfd2240, TX_CSR = 0xfd2244, TX_MODE = 0xfd2248,
    RX_MAX = 0xfd224c, RX_CSR = 0xfd2250, RX_MODE = 0xfd2254,
    RX_COUNT = 0xfd2258, RX_COUNT_HI = 0xfd225c,
    USB_CONTROL = 0xfd2500, USB_CONTROL1 = 0xfd2504,
    EP0_WRITE_SIZE = 0xfd2508, EP2_WRITE_SIZE = 0xfd2510,
    EP0_DMA = 0xfd251c, EP2_TX_DMA = 0xfd2528, EP2_RX_DMA = 0xfd252c,
    EP2_RX_LIMIT = 0xfd2548, PORT_IN = 0xfd3500, PORT_DIE = 0xfd350c,
    PORT_PU0 = 0xfd3514, PORT_PD0 = 0xfd351c, PORT_CON = 0xfd3530,
    RX_READY = 0x01, TX_READY = 0x02, SENT_STALL = 0x04, DATA_END = 0x08,
    SETUP_END = 0x10, SEND_STALL = 0x20, SERVICE_RX = 0x40, SERVICE_SETUP = 0x80,
    BUS_SUSPEND = 1, BUS_RESET = 4, BUS_DISCONNECT = 32,
    RX_FLUSH = 0x10,
    PACKET = 64, LEASE_MS = 500,
};

#if !defined(T3P_USB_VID) || !defined(T3P_USB_PID)
#error Supply the USB identity through build_native_board.py
#endif
#ifdef T3P_USB_MANUFACTURER_DESC
#define USB_MANUFACTURER_INDEX 2
static const uint8_t manufacturer_desc[] = { T3P_USB_MANUFACTURER_DESC };
_Static_assert(sizeof(manufacturer_desc) <= PACKET, "Manufacturer string fits one control packet");
#else
#define USB_MANUFACTURER_INDEX 0
#endif
static const uint8_t device_desc[] = {
    18, 1, 0x00, 0x02, 0, 0, 0, PACKET,
    T3P_USB_VID & 0xff, T3P_USB_VID >> 8, T3P_USB_PID & 0xff, T3P_USB_PID >> 8,
    USB_REVISION, 0x01, USB_MANUFACTURER_INDEX, 1, 0, 1
};
static const uint8_t report_desc[] = {
    0x06, 0x61, 0xff, 0x09, 0x01, 0xa1, 0x01,
    0x15, 0, 0x26, 0xff, 0, 0x75, 8, 0x95, PACKET,
    0x09, 2, 0x81, 2, 0x09, 3, 0x91, 2, 0xc0
};
static const uint8_t config_desc[] = {
    9, 2, 41, 0, 1, 1, 0, 0x80, 100,
    9, 4, 0, 0, 2, 3, 0, 0, 0,
    9, 0x21, 0x11, 0x01, 0, 1, 0x22, sizeof(report_desc), 0,
    7, 5, 0x82, 3, PACKET, 0, USB_IN_INTERVAL,
    7, 5, 0x02, 3, PACKET, 0, 4
};
static const uint8_t lang_desc[] = {4, 3, 9, 4};
static const uint8_t product_desc[] = {
    26, 3, 'T',0,'3',0,'P',0,' ',0,'D',0,'O',0,'O',0,'M',0,' ',0,'V',0,'6',0,USB_REVISION_DIGIT,0
};
_Static_assert(sizeof(config_desc) == 41 && sizeof(product_desc) == 26, "USB descriptor length");

static const struct t3p_bsp_io *usb;
static _Alignas(64) volatile uint8_t ep0[PACKET], rx[PACKET], tx[PACKET];
static uint16_t held, pending;
static uint32_t received_at, started_at;
static uint32_t resets, setups, last_setup, reports, out_packets, busy_echoes;
static uint8_t attached;
static uint8_t configured, report_pending, idle_rate, address_pending, address;

static uint32_t rd(unsigned reg) { return usb->read32(usb->context, reg); }
static void wr(unsigned reg, uint32_t value) { usb->write32(usb->context, reg, value); }
static void change(unsigned reg, uint32_t clear, uint32_t set)
{
    wr(reg, (rd(reg) & ~clear) | set);
}

static void release_keys(void) { held = pending = 0; }

#ifdef T3P_USB_CAPTURE
/* A request freezes one committed frame. The game can overwrite either of
 * its display buffers while this private copy is sent. All offsets are bytes. */
static uint8_t capture[512 + T3P_FRAME_PIXELS], echo[PACKET];
static uint16_t capture_offset, capture_size;
static uint8_t capture_id, capture_requested, capture_active, echo_waiting;
enum {
    CAPTURE_START = 1, CAPTURE_STOP = 2, CAPTURE_FAST = 3,
    PALETTE_BYTES = 512, FAST_SIDE = T3P_FRAME_SIDE / 2,
    CHUNK_HEADER = 7, CHUNK_DATA = PACKET - CHUNK_HEADER
};

void t3p_usb_capture_frame(const uint8_t *pixels, const uint16_t *palette)
{
    if (!capture_requested)
        return;
    memcpy(capture, palette, PALETTE_BYTES);
    uint8_t *image = capture + PALETTE_BYTES;
#ifdef T3P_USB_CAPTURE_FAST
    if (capture_requested == CAPTURE_FAST) {
        for (unsigned x = 0; x < FAST_SIDE; ++x)
            for (unsigned y = 0; y < FAST_SIDE; ++y)
                image[x * FAST_SIDE + y] = pixels[(2 * x) * T3P_FRAME_SIDE + 2 * y];
        capture_size = PALETTE_BYTES + FAST_SIDE * FAST_SIDE;
    } else
#endif
    {
        memcpy(image, pixels, T3P_FRAME_PIXELS);
        capture_size = sizeof(capture);
    }
    capture_offset = 0;
    capture_requested = 0;
    capture_active = 1;
}

static void send_pending(uint32_t now)
{
    if (now - received_at >= LEASE_MS)
        capture_active = capture_requested = echo_waiting = 0;
    if ((!echo_waiting && !capture_active) || (rd(TX_CSR) & 1u))
        return;
    if (echo_waiting) {
        for (unsigned i=0; i<PACKET; ++i)
            tx[i] = echo[i];
        echo_waiting = 0;
    } else {
        unsigned chunk = capture_offset / CHUNK_DATA;
        tx[0] = 'T'; tx[1] = '3'; tx[2] = 'P'; tx[3] = 'F';
        tx[4] = capture_id; tx[5] = (uint8_t)chunk; tx[6] = (uint8_t)(chunk >> 8);
        for (unsigned i=CHUNK_HEADER; i<PACKET; ++i)
            tx[i] = capture_offset < capture_size ? capture[capture_offset++] : 0;
        capture_active = capture_offset < capture_size;
    }
    wr(EP2_WRITE_SIZE, PACKET);
    wr(TX_CSR, 1);
}
#endif

static void endpoints(void)
{
    wr(EP2_TX_DMA, (uintptr_t)tx);
    wr(EP2_RX_DMA, (uintptr_t)rx);
    wr(EP2_RX_LIMIT, PACKET);
    wr(TX_MAX, 0x3ff);
    wr(RX_MAX, 0x3ff);
    wr(TX_MODE, 0);
    wr(RX_MODE, 0);
    wr(TX_CSR, 0x48); /* Flush and reset data toggle. */
    wr(RX_CSR, 0x90);
    change(USB_CONTROL, 1u << 21, 1u << 25);
}

void t3p_usb_keys_start(const struct t3p_bsp_io *io, uint32_t now)
{
    usb = io;
    started_at = now;
    attached = 0;
    /* Port direction and SIE handoff use the same pin settings as stock.
     * Keep the system PLL and any other port pins untouched. */
    change(PORT_DIE, 0, 3);
    change(PORT_CON, 0x105, 0);
    change(USB_CONTROL, 0x4074u | (1u << 10) | (1u << 11), 0);
    wr(USB_CONTROL1, 0);
    change(PORT_CON, 0, 1);
    /* Stock sets PU0 bit 0 for the D+ attach pull-up and clears both PD0 bits. */
    change(PORT_PD0, 3, 0);
    change(PORT_PU0, 3, 1);
    change(USB_CONTROL, 0, 0x3c);
    /* Keep the bus detached until the PHY has settled and the host has
     * observed a disconnect. Bind DMA before enabling the connection. */
    wr(POWER, 0);
}

static void attach(void)
{
    wr(FADDR, 0);
    wr(EP0_DMA, (uintptr_t)ep0);
    wr(TX_ENABLE, 1);
    wr(TX_ENABLE + 4, 0);
    wr(RX_ENABLE, 0);
    wr(RX_ENABLE + 4, 0);
    wr(BUS_ENABLE, BUS_RESET | BUS_SUSPEND | BUS_DISCONNECT);
    (void)rd(BUS_IRQ);
    (void)rd(TX_IRQ);
    (void)rd(RX_IRQ);
    wr(POWER, 0x40);
    /* Same SIE event enable as stock. The application keeps CPU interrupts
     * masked and services USB by polling, including between draw bands. */
    change(USB_CONTROL, 0, 1u << 11);
    attached = 1;
}

static void acknowledge(const volatile uint8_t *packet, unsigned length, uint32_t now)
{
    if (length != PACKET || packet[0] != 'T' || packet[1] != '3' ||
        packet[2] != 'P' || packet[3] != '1')
        return;
    unsigned keys = packet[5] | (unsigned)packet[6] << 8;
    if (keys & ~((T3P_WEAPON_1 << 1) - 1u))
        return;
    ++reports;
    if (now - received_at >= LEASE_MS)
        release_keys();
    pending |= keys & ~held;
    held = keys;
    received_at = now;
#ifdef T3P_USB_CAPTURE
    unsigned command = packet[7];
    if (command == CAPTURE_START
#ifdef T3P_USB_CAPTURE_FAST
        || command == CAPTURE_FAST
#endif
    ) {
        capture_id = packet[4];
        capture_active = 0;
        capture_requested = (uint8_t)command;
    } else if (packet[7] == CAPTURE_STOP) {
        capture_active = capture_requested = 0;
    }
    for (unsigned i=0; i<PACKET; ++i)
        echo[i] = packet[i];
    echo_waiting = 1;
#else
    if (!(rd(TX_CSR) & 1u)) {
        for (unsigned i = 0; i < PACKET; ++i)
            tx[i] = packet[i];
        wr(EP2_WRITE_SIZE, PACKET);
        wr(TX_CSR, 1);
    } else {
        ++busy_echoes;
    }
#endif
}

static void reply(const uint8_t *bytes, unsigned size, unsigned requested)
{
    unsigned count = size < requested ? size : requested;
    for (unsigned i = 0; i < count; ++i)
        ep0[i] = bytes[i];
    wr(CSR0, SERVICE_RX);
    wr(EP0_WRITE_SIZE, count);
    wr(CSR0, TX_READY | DATA_END);
}

static void setup(void)
{
    unsigned type = ep0[0], request = ep0[1];
    unsigned value = ep0[2] | (unsigned)ep0[3] << 8;
    unsigned index = ep0[4] | (unsigned)ep0[5] << 8;
    unsigned length = ep0[6] | (unsigned)ep0[7] << 8;
    ++setups;
    last_setup = (uint32_t)type << 24 | (uint32_t)request << 16 | value;
    uint8_t status[] = {0, 0};
    if ((type == 0x80 || type == 0x81) && request == 6 && length) {
        const uint8_t *bytes = 0;
        unsigned size = 0;
        switch (value) {
        case 0x0100: bytes = device_desc; size = sizeof(device_desc); break;
        case 0x0200: bytes = config_desc; size = sizeof(config_desc); break;
        case 0x0300: bytes = lang_desc; size = sizeof(lang_desc); break;
        case 0x0301: bytes = product_desc; size = sizeof(product_desc); break;
#ifdef T3P_USB_MANUFACTURER_DESC
        case 0x0302: bytes = manufacturer_desc; size = sizeof(manufacturer_desc); break;
#endif
        case 0x2100: bytes = config_desc + 18; size = 9; break;
        case 0x2200: bytes = report_desc; size = sizeof(report_desc); break;
        }
        if (bytes) {
            reply(bytes, size, length);
            return;
        }
    } else if (type == 0 && request == 5 && value <= 127 && !index && !length) {
        address = (uint8_t)value;
        address_pending = 1;
        wr(CSR0, SERVICE_RX | DATA_END);
        return;
    } else if (type == 0 && request == 9 && value <= 1 && !index && !length) {
        release_keys();
        configured = (uint8_t)value;
        if (configured)
            endpoints();
        wr(CSR0, SERVICE_RX | DATA_END);
        return;
    } else if (type == 0x80 && request == 8 && length == 1) {
        reply(&configured, 1, length);
        return;
    } else if (type >= 0x80 && type <= 0x82 && request == 0 && length == 2) {
        reply(status, sizeof(status), length);
        return;
    } else if (type == 0x81 && request == 10 && !index && length == 1) {
        reply(status, 1, length);
        return;
    } else if (type == 1 && request == 11 && !value && !index && !length) {
        wr(CSR0, SERVICE_RX | DATA_END);
        return;
    } else if (type == 0x21 && request == 10 && !index && !length) {
        idle_rate = (uint8_t)(value >> 8);
        wr(CSR0, SERVICE_RX | DATA_END);
        return;
    } else if (type == 0xa1 && request == 2 && !index && length == 1) {
        reply(&idle_rate, 1, length);
        return;
    } else if (type == 0xa1 && request == 1 && value == 0x0100 && !index && length) {
        uint8_t report[PACKET] = {'T', '3', 'P', '1'};
        reply(report, sizeof(report), length);
        return;
    } else if (configured && type == 0x21 && request == 9 && value == 0x0200 && !index && length == PACKET) {
        report_pending = 1;
        wr(CSR0, SERVICE_RX);
        return;
    }
    wr(CSR0, SERVICE_RX | SEND_STALL);
}

void t3p_usb_keys_poll(uint32_t now)
{
    if (!usb)
        return;
    if (!attached) {
        if (now - started_at < 100)
            return;
        attach();
    }
    unsigned bus = rd(BUS_IRQ), irq = rd(TX_IRQ);
    if (bus & BUS_RESET)
        ++resets;
    (void)rd(RX_IRQ);
    if (bus & (BUS_SUSPEND | BUS_RESET | BUS_DISCONNECT))
        release_keys();
#ifdef T3P_USB_CAPTURE
    if (bus & (BUS_SUSPEND | BUS_RESET | BUS_DISCONNECT))
        capture_requested = capture_active = echo_waiting = 0;
#endif
    if (bus & (BUS_RESET | BUS_DISCONNECT)) {
        configured = report_pending = address_pending = idle_rate = 0;
        wr(FADDR, 0);
        return;
    }
    unsigned csr = rd(CSR0);
    if (csr & SETUP_END) {
        report_pending = address_pending = 0;
        wr(CSR0, SERVICE_SETUP);
        csr = rd(CSR0);
    }
    if (csr & SENT_STALL) {
        wr(CSR0, 0);
        csr = rd(CSR0);
    }
    if (address_pending && (irq & 1u) && !(csr & (TX_READY | DATA_END))) {
        wr(FADDR, address);
        address_pending = 0;
    }
    if (csr & RX_READY) {
        unsigned count = rd(COUNT0) & 0xffu;
        if (report_pending && count == PACKET) {
            report_pending = 0;
            acknowledge(ep0, count, now);
            wr(CSR0, SERVICE_RX | DATA_END);
        } else if (count == 8) {
            report_pending = address_pending = 0;
            setup();
        } else {
            report_pending = 0;
            wr(CSR0, SERVICE_RX | DATA_END);
        }
    }
    if (configured && (rd(RX_CSR) & 1u)) {
        unsigned count = (rd(RX_COUNT) & 0xffu) | ((rd(RX_COUNT_HI) & 7u) << 8);
        ++out_packets;
        acknowledge(rx, count, now);
        /* BR35 advances the RX DMA cursor. Stock's EP2 OUT path flushes
         * each consumed packet (0x1035f4). Keep the USB data toggle intact. */
        wr(EP2_RX_DMA, (uintptr_t)rx);
        wr(RX_CSR, (rd(RX_CSR) & 0x8bu) | RX_FLUSH);
    }
#ifdef T3P_USB_CAPTURE
    if (configured)
        send_pending(now);
#endif
}

uint32_t t3p_usb_keys_take(uint32_t now)
{
    if (now - received_at >= LEASE_MS)
        release_keys();
    unsigned keys = held | pending;
    pending = 0;
    return keys;
}

/* Rows for the right-button startup diagnostics. Avoid reading the
 * read-to-clear interrupt registers here so the poller sees every event. */
uint32_t t3p_usb_keys_debug(unsigned row)
{
    switch (row) {
    case 0: return rd(USB_CONTROL);
    case 1: return (rd(PORT_CON) & 0xffffu) << 16 | (rd(PORT_IN) & 3u);
    case 2: return (rd(POWER) & 0xffu) << 24 | (rd(FADDR) & 0x7fu) << 16 |
                   (rd(FRAME_HI) & 7u) << 8 | (rd(FRAME_LO) & 0xffu);
    case 3: return resets;
    case 4: return setups;
    case 5: return last_setup;
    case 6: return (uint32_t)configured << 24 | (rd(CSR0) & 0xffu) << 16 |
                   (rd(COUNT0) & 0xffu);
    case 7: return reports;
    case 8: return (busy_echoes & 0xffffu) << 16 | (out_packets & 0xffffu);
    default: return 0;
    }
}
