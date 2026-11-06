/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_bsp.h"
#include "t3p_input.h"
#include "t3p_sensors.h"
#include "t3p_touch.h"

/* Stock 3.0.3 uses PA06/PA05 for touch and PB02/PB01 for the
 * accelerometer. The second pin pair is in the table at 0x0c11bcfc,
 * copied by the software I2C initializer at 0x0c0ea578. */
enum {
    PORT_A = 0xfd3000, PORT_B = 0xfd3100,
    OUT_BSR = 0x34, DIR_BSR = 0x38, DIE_BSR = 0x3c,
    PU0_BSR = 0x44, PU1_BSR = 0x48, PD0_BSR = 0x4c, PD1_BSR = 0x50,
    TIMER1_CON = 0xfd0614, TIMER1_CNT = 0xfd0618, TIMER1_PRD = 0xfd061c,
    TOUCH_RESET = 1u << 2,
    TICKS_PER_US = 24, HALF_BIT_US = 5, STRETCH_US = 80, POLL_LIMIT = 2048,
    TOUCH_INTERVAL_MS = 20, DEAD_ZONE = 24,
};

static uint32_t read_reg(const struct t3p_bsp_io *io, uint32_t address)
{
    return io->read32(io->context, address);
}

static void write_reg(const struct t3p_bsp_io *io, uint32_t address, uint32_t value)
{
    io->write32(io->context, address, value);
}

static void line(const struct t3p_sensor_gpio *gpio, unsigned pin, int high)
{
    /* DIR=1 releases the pin. Its output latch always contains zero. */
    write_reg(gpio->io, gpio->port + DIR_BSR, high ? pin : pin << 16);
}

static uint16_t ticks(const struct t3p_bsp_io *io)
{
    return (uint16_t)read_reg(io, TIMER1_CNT);
}

static int delay_half_bit(const struct t3p_bsp_io *io)
{
    uint16_t start = ticks(io);
    for (unsigned poll = 0; poll < POLL_LIMIT; ++poll)
        if ((uint16_t)(ticks(io) - start) >= HALF_BIT_US * TICKS_PER_US)
            return 0;
    return -1;
}

static int release_clock(const struct t3p_sensor_gpio *gpio)
{
    const struct t3p_bsp_io *io = gpio->io;
    line(gpio, gpio->scl, 1);
    uint16_t start = ticks(io);
    for (unsigned poll = 0; poll < POLL_LIMIT; ++poll) {
        if (read_reg(io, gpio->port) & gpio->scl)
            return 0;
        if ((uint16_t)(ticks(io) - start) >= STRETCH_US * TICKS_PER_US)
            break;
    }
    return -1;
}

static int start_transfer(const struct t3p_sensor_gpio *gpio)
{
    const struct t3p_bsp_io *io = gpio->io;
    line(gpio, gpio->sda, 1);
    if (delay_half_bit(io) || release_clock(gpio) || delay_half_bit(io) ||
        !(read_reg(io, gpio->port) & gpio->sda))
        return -1;
    line(gpio, gpio->sda, 0);
    if (delay_half_bit(io))
        return -1;
    line(gpio, gpio->scl, 0);
    return 0;
}

static int stop_transfer(const struct t3p_sensor_gpio *gpio)
{
    const struct t3p_bsp_io *io = gpio->io;
    line(gpio, gpio->scl, 0);
    line(gpio, gpio->sda, 0);
    int failed = delay_half_bit(io) || release_clock(gpio) || delay_half_bit(io);
    line(gpio, gpio->sda | gpio->scl, 1);
    return failed;
}

static int transfer_bit(const struct t3p_sensor_gpio *gpio, int output)
{
    const struct t3p_bsp_io *io = gpio->io;
    line(gpio, gpio->sda, output);
    if (delay_half_bit(io) || release_clock(gpio) || delay_half_bit(io))
        return -1;
    int input = !!(read_reg(io, gpio->port) & gpio->sda);
    line(gpio, gpio->scl, 0);
    return input;
}

static int send_byte(const struct t3p_sensor_gpio *gpio, unsigned byte)
{
    for (unsigned bit = 0x80; bit; bit >>= 1)
        if (transfer_bit(gpio, byte & bit) < 0)
            return -1;
    return transfer_bit(gpio, 1) == 0 ? 0 : -1;
}

static int receive_byte(const struct t3p_sensor_gpio *gpio, int last)
{
    unsigned byte = 0;
    for (unsigned bit = 0; bit < 8; ++bit) {
        int value = transfer_bit(gpio, 1);
        if (value < 0)
            return -1;
        byte = (byte << 1) | (unsigned)value;
    }
    return transfer_bit(gpio, last) < 0 ? -1 : (int)byte;
}

static int sensor_bus_read(void *context, uint8_t address, uint8_t reg,
                           uint8_t *data, unsigned count)
{
    const struct t3p_sensor_gpio *gpio = context;
    unsigned read = 0;
    if (!start_transfer(gpio) && !send_byte(gpio, address << 1) && !send_byte(gpio, reg) &&
        !start_transfer(gpio) && !send_byte(gpio, (address << 1) | 1)) {
        for (; read < count; ++read) {
            int value = receive_byte(gpio, read + 1 == count);
            if (value < 0)
                break;
            data[read] = (uint8_t)value;
        }
    }
    return stop_transfer(gpio) || read != count ? -1 : (int)read;
}

static int sensor_bus_write(void *context, uint8_t address, uint8_t reg, uint8_t data)
{
    const struct t3p_sensor_gpio *gpio = context;
    int failed = start_transfer(gpio) || send_byte(gpio, address << 1) ||
                 send_byte(gpio, reg) || send_byte(gpio, data);
    return stop_transfer(gpio) || failed ? -1 : 0;
}

static void init_gpio(struct t3p_sensor_gpio *gpio, const struct t3p_bsp_io *io,
                      uint32_t port, uint32_t sda, uint32_t scl)
{
    *gpio = (struct t3p_sensor_gpio){io, port, sda, scl};
    unsigned pins = sda | scl;
    line(gpio, pins, 1);
    write_reg(io, port + OUT_BSR, pins << 16);
    write_reg(io, port + DIE_BSR, pins);
    write_reg(io, port + PU0_BSR, pins);
    write_reg(io, port + PU1_BSR, pins << 16);
    write_reg(io, port + PD0_BSR, pins << 16);
    write_reg(io, port + PD1_BSR, pins << 16);
}

struct t3p_sensor_bus t3p_accel_bus_init(struct t3p_sensor_gpio *gpio, struct t3p_bsp *board)
{
    init_gpio(gpio, &board->io, PORT_B, 1u << 1, 1u << 2);
    return (struct t3p_sensor_bus){sensor_bus_read, gpio, sensor_bus_write};
}

void t3p_touch_init(struct t3p_touch *touch, struct t3p_bsp *board)
{
    *touch = (struct t3p_touch){0};
    const struct t3p_bsp_io *io = &board->io;
    write_reg(io, TIMER1_CON, 0x4000);
    write_reg(io, TIMER1_CNT, 0);
    write_reg(io, TIMER1_PRD, 0xffff);
    write_reg(io, TIMER1_CON, (6u << 10) | 1u);  /* 24 MHz, divider 1 */
    if (delay_half_bit(io)) {
        write_reg(io, TIMER1_CON, 0x4000);
        return;
    }

    init_gpio(&touch->gpio, io, PORT_A, 1u << 5, 1u << 6);
    write_reg(io, PORT_A + OUT_BSR, TOUCH_RESET);
    write_reg(io, PORT_A + DIR_BSR, TOUCH_RESET << 16);
    if (t3p_bsp_delay(board, 10))
        return;
    write_reg(io, PORT_A + OUT_BSR, TOUCH_RESET << 16);
    int failed = t3p_bsp_delay(board, 10);
    write_reg(io, PORT_A + OUT_BSR, TOUCH_RESET);
    if (failed || t3p_bsp_delay(board, 100))
        return;

    touch->ready = 1;
}

uint32_t t3p_touch_poll(struct t3p_touch *touch, struct t3p_bsp *board)
{
    if (!touch->ready)
        return 0;
    uint32_t now = t3p_bsp_millis(board);
    if ((uint32_t)(now - touch->last_poll_ms) < TOUCH_INTERVAL_MS)
        return touch->keys;
    touch->last_poll_ms = now;

    const struct t3p_sensor_bus bus = {sensor_bus_read, &touch->gpio, 0};
    struct t3p_touch_raw sample;
    touch->keys = 0;
    if (t3p_touch_read(&bus, &sample) || !sample.down) {
        touch->held = 0;
        return 0;
    }
    if (!touch->held) {
        touch->origin_x = sample.x;
        touch->origin_y = sample.y;
        touch->held = 1;
        return 0;
    }

    /* Relative motion avoids a guessed panel coordinate range. */
    int dx = (int)sample.x - touch->origin_x;
    int dy = (int)sample.y - touch->origin_y;
    if (dx <= -DEAD_ZONE) touch->keys |= T3P_LEFT;
    if (dx >= DEAD_ZONE) touch->keys |= T3P_RIGHT;
    if (dy <= -DEAD_ZONE) touch->keys |= T3P_FORWARD;
    if (dy >= DEAD_ZONE) touch->keys |= T3P_BACK;
    return touch->keys;
}
