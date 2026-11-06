/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_bsp.h"
#include "t3p_input.h"
#include "t3p_mic.h"

/* BR35 audio registers. Stock 3.0.3 opens MIC0 on PA04 in capless mode
 * (0x0c00f458). ADC start and completion are at 0x0c0c0134/0x0c0fe592. */
enum {
    AUDIO_CLOCK = 0xfd4020, PA_DIR_SET = 0xfd3038, PA_DIE_SET = 0xfd303c,
    PA_PU0_SET = 0xfd3044, PA_PU1_SET = 0xfd3048,
    PA_PD0_SET = 0xfd304c, PA_PD1_SET = 0xfd3050,
    AUD_CON0 = 0xf10100, AUD_CON1 = 0xf10104, AUD_COP = 0xf1010c,
    ADA_CON0 = 0xf11000, ADA_CON1 = 0xf11004, ADA_CON2 = 0xf11008,
    ADA_CON3 = 0xf1100c, ADA_CON4 = 0xf11010, ADA_CON5 = 0xf11014,
    ADC_CON0 = 0xf10300, ADC_CON1 = 0xf10304, ADC_CON2 = 0xf10308,
    ADC_COP = 0xf1030c, ADC_ADR = 0xf10318, ADC_LEN = 0xf1031c,
    ADC_PNS = 0xf10320, ADC_HWP = 0xf10324, ADC_SRP = 0xf10328, ADC_SRN = 0xf1032c,
    ADC_ACK = 1u << 6, ADC_PENDING = 1u << 7,
    ADC_16KHZ_MONO = 0x0800483a,
    SETTLE_BLOCKS = 16, NOISE_BLOCKS = 8, QUIET_BLOCKS = 3,
    FIRE_MIN_LEVEL = 4096, ACTIVITY_SAMPLES = 16, FIRE_MIN_ACTIVE_WINDOWS = 3,
    FIRE_COOLDOWN_MS = 300, FIRE_MAX_AGE_MS = 250,
};

static uint32_t read_reg(const struct t3p_bsp_io *io, uint32_t address)
{
    return io->read32(io->context, address);
}

static void write_reg(const struct t3p_bsp_io *io, uint32_t address, uint32_t value)
{
    io->write32(io->context, address, value);
}

static void change(const struct t3p_bsp_io *io, uint32_t address, uint32_t clear, uint32_t set)
{
    write_reg(io, address, (read_reg(io, address) & ~clear) | set);
}

static void disable_adc(const struct t3p_bsp_io *io)
{
    write_reg(io, ADC_CON0, ADC_ACK);
    change(io, AUD_CON1, 8, 0);
    change(io, AUD_CON0, 2, 0);
    change(io, ADA_CON0, 0x8400, 0); /* release the MIC0 input and bias path */
}

void t3p_mic_init(struct t3p_mic *mic, struct t3p_bsp *board, uint32_t now)
{
    const struct t3p_bsp_io *io = &board->io;
    mic->fired_ms = now - FIRE_COOLDOWN_MS;

    /* Configure the audio clock domain using the stock selector. The CPU
     * clock, LCD clock and global interrupt mask are owned by the runtime. */
    change(io, AUDIO_CLOCK, 3, 1u | (1u << 26));
    if (t3p_bsp_delay(board, 1))
        return;
    change(io, AUD_CON0, 0x3f0, 0x0f9);
    write_reg(io, AUD_COP, 0x0f);
    write_reg(io, ADC_CON0, ADC_ACK);

    const unsigned mic_pin = 1u << 4;
    write_reg(io, PA_DIR_SET, mic_pin);
    write_reg(io, PA_DIE_SET, mic_pin << 16);
    write_reg(io, PA_PU0_SET, mic_pin << 16);
    write_reg(io, PA_PU1_SET, mic_pin << 16);
    write_reg(io, PA_PD0_SET, mic_pin << 16);
    write_reg(io, PA_PD1_SET, mic_pin << 16);

    /* Preserve any LDO trim inherited in bits 10:8. Use the stock fallback
     * mic bias resistor index 8 and the stock analogue gain clamped to 5. */
    change(io, ADA_CON5, 0x8ffeb87f, 0x4880);
    write_reg(io, ADA_CON0, 0x005886c9);
    write_reg(io, ADA_CON1, 0x5d69d8f0);
    write_reg(io, ADA_CON2, 0x1532a4c5);
    write_reg(io, ADA_CON3, 0xc9dd6d04);
    write_reg(io, ADA_CON4, 0x11);

    write_reg(io, ADC_ADR, (uint32_t)(uintptr_t)mic->pcm);
    write_reg(io, ADC_LEN, 2 * T3P_MIC_BLOCK_SAMPLES);
    write_reg(io, ADC_PNS, T3P_MIC_BLOCK_SAMPLES);
    write_reg(io, ADC_HWP, 0);
    write_reg(io, ADC_SRP, 0);
    change(io, AUD_CON0, 0, 0x400);
    change(io, AUD_CON1, 0xf02, 5);
    write_reg(io, ADC_CON1, 0x00083223);
    write_reg(io, ADC_CON2, 0);
    /* Bit 5 enables peripheral completion. The CPU polls bit 7. */
    write_reg(io, ADC_CON0, ADC_16KHZ_MONO | ADC_ACK);
    change(io, AUD_CON0, 0, 2);
    if (t3p_bsp_delay(board, 1)) {
        disable_adc(io);
        return;
    }
    change(io, AUD_CON1, 0, 8);
    if (t3p_bsp_delay(board, 1)) {
        disable_adc(io);
        return;
    }
    write_reg(io, ADC_COP, 0x2f);
    mic->ready = 1;
}

unsigned t3p_mic_threshold(const struct t3p_mic *mic)
{
    unsigned threshold = mic->noise * 4 + 1024;
    return threshold < FIRE_MIN_LEVEL ? FIRE_MIN_LEVEL : threshold;
}

struct mic_level {
    unsigned peak, active_windows;
};

static struct mic_level measure_block(const volatile int16_t *samples, unsigned activity_threshold)
{
    int high = -32768, low = 32767;
    struct mic_level level = {0, 0};
    for (unsigned i = 0; i < T3P_MIC_BLOCK_SAMPLES; i += ACTIVITY_SAMPLES) {
        int window_high = -32768, window_low = 32767;
        for (unsigned j = 0; j < ACTIVITY_SAMPLES; ++j) {
            int sample = samples[i + j];
            if (sample > window_high) window_high = sample;
            if (sample < window_low) window_low = sample;
        }
        if (window_high > high) high = window_high;
        if (window_low < low) low = window_low;
        if ((unsigned)(window_high - window_low) >= activity_threshold)
            ++level.active_windows;
    }
    level.peak = (unsigned)(high - low);
    return level;
}

static void detect(struct t3p_mic *mic, struct mic_level level, unsigned threshold,
                   uint32_t now, int paused)
{
    if (mic->warmup < SETTLE_BLOCKS + NOISE_BLOCKS) {
        if (mic->warmup++ >= SETTLE_BLOCKS)
            mic->noise += level.peak;
        if (mic->warmup == SETTLE_BLOCKS + NOISE_BLOCKS)
            mic->noise /= NOISE_BLOCKS;
        return;
    }

    if (level.peak >= mic->peak_level) {
        mic->peak_level = (uint16_t)level.peak;
        mic->peak_windows = (uint8_t)level.active_windows;
    }

    if (level.peak < threshold / 2) {
        mic->noise = (mic->noise * 7 + level.peak) / 8;
        if (!paused && mic->quiet < QUIET_BLOCKS && ++mic->quiet == QUIET_BLOCKS)
            mic->armed = 1;
    } else {
        mic->quiet = 0;
        /* An isolated spike can be loud but occupies too few 1 ms windows. */
        if (level.peak >= threshold && level.active_windows >= FIRE_MIN_ACTIVE_WINDOWS) {
            if (mic->armed && now - mic->fired_ms >= FIRE_COOLDOWN_MS) {
                mic->pending = 1;
                mic->fired_ms = now;
            }
            mic->armed = 0;
        }
    }
}

void t3p_mic_poll(struct t3p_mic *mic, struct t3p_bsp *board, uint32_t now, enum t3p_mic_mode mode)
{
    if (mode != T3P_MIC_LISTEN) {
        mic->pending = 0;
        mic->quiet = mic->armed = 0;
    }
    if (now - mic->fired_ms > FIRE_MAX_AGE_MS)
        mic->pending = 0;
    if (!mic->ready)
        return;

    const struct t3p_bsp_io *io = &board->io;
    uint32_t status = read_reg(io, ADC_CON0);
    if (!(status & ADC_PENDING))
        return;

    /* A half stays owned by the reader until SRN advances the read pointer.
     * Validate both register values before letting them address the buffer. */
    write_reg(io, ADC_CON0, status | ADC_ACK);
    unsigned count = read_reg(io, ADC_PNS) & 0xffffu;
    unsigned offset = read_reg(io, ADC_SRP) & 0xffffu;
    if (count != T3P_MIC_BLOCK_SAMPLES || (offset != 0 && offset != T3P_MIC_BLOCK_SAMPLES)) {
        disable_adc(io);
        mic->ready = mic->pending = 0;
        return;
    }
    /* Drain motor noise without including it in the microphone calibration. */
    if (mode != T3P_MIC_DISCARD) {
        unsigned threshold = t3p_mic_threshold(mic);
        struct mic_level level = measure_block(mic->pcm + offset, threshold / 4);
        detect(mic, level, threshold, now, mode == T3P_MIC_PAUSE);
    }
    write_reg(io, ADC_SRN, count);
    write_reg(io, ADC_PNS, count);
    write_reg(io, ADC_CON0, status | ADC_ACK);
    change(io, ADC_CON1, 0, 1u << 29);
}

uint32_t t3p_mic_take(struct t3p_mic *mic)
{
    uint32_t keys = mic->pending ? T3P_FIRE : 0;
    mic->pending = 0;
    return keys;
}
