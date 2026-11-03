/* SPDX-License-Identifier: MIT
 * Bench only: T3P must be disconnected. Outputs one fixed 0x16ef test frame
 * to a scope breakout, never auto-repeats. No USB host or target commands.
 * Default and final state: inputs with weak pulldowns; outputs released
 * inside PIO itself, independent of CPU USB interrupt latency.
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "key_wave.pio.h"
#include "wave_plan.h"

enum { DM=12,DP=13 };
static const PIO engine=pio0;
static uint sm,offset;
static int dma;
static bool fault;
static unsigned frames;

static void status(void)
{
    printf("{\"program\":\"t3p_cm_wave_scope\",\"version\":4,\"bench_only\":true,"
           "\"board\":\"RP2350-USB-CM\",\"event\":\"status\",\"dp_gpio\":13,\"dm_gpio\":12,"
           "\"dp\":%u,\"dm\":%u,\"pulls\":\"down\",\"pio_oe\":%u,"
           "\"clock_hz\":%lu,\"frames\":%u,\"fault\":%s,\"drive_2ma\":%s,\"slew_slow\":%s,"
           "\"key_clock_gpio\":13,\"key_data_gpio\":12}\n",
           (unsigned)gpio_get(DP),(unsigned)gpio_get(DM),
           (unsigned)((engine->dbg_padoe>>DM)&3),(unsigned long)clock_get_hz(clk_sys),
           frames,fault?"true":"false",
           gpio_get_drive_strength(DP)==GPIO_DRIVE_STRENGTH_2MA &&
           gpio_get_drive_strength(DM)==GPIO_DRIVE_STRENGTH_2MA?"true":"false",
           gpio_get_slew_rate(DP)==GPIO_SLEW_RATE_SLOW &&
           gpio_get_slew_rate(DM)==GPIO_SLEW_RATE_SLOW?"true":"false");
}

static void emit_once(void)
{
    if (fault || gpio_get(DP) || gpio_get(DM)) {
        puts("{\"event\":\"wave_rejected\",\"reason\":\"fault or externally biased input; T3P must be disconnected\"}");
        return;
    }
    uint32_t words[WAVE_WORDS];
    wave_plan(words);
    pio_sm_set_enabled(engine,sm,false);
    pio_sm_clear_fifos(engine,sm);
    pio_sm_restart(engine,sm);
    pio_sm_exec(engine,sm,pio_encode_jmp(offset));
    pio_interrupt_clear(engine,0);
    pio_sm_set_pins_with_mask(engine,sm,0,3u<<DM);
    pio_sm_set_pindirs_with_mask(engine,sm,3u<<DM,3u<<DM);
    dma_channel_config cfg=dma_channel_get_default_config(dma);
    channel_config_set_transfer_data_size(&cfg,DMA_SIZE_32);
    channel_config_set_read_increment(&cfg,true);
    channel_config_set_write_increment(&cfg,false);
    channel_config_set_dreq(&cfg,pio_get_dreq(engine,sm,true));
    dma_channel_configure(dma,&cfg,&engine->txf[sm],words,WAVE_WORDS,true);
    uint64_t deadline=time_us_64()+20000;
    while (pio_sm_is_tx_fifo_empty(engine,sm) && time_us_64()<deadline) tight_loop_contents();
    uint32_t stall_bit=1u<<(PIO_FDEBUG_TXSTALL_LSB+sm);
    engine->fdebug=stall_bit;
    pio_sm_set_enabled(engine,sm,true);
    while (!pio_interrupt_get(engine,0) && time_us_64()<deadline) tight_loop_contents();
    bool completed=pio_interrupt_get(engine,0);
    bool underrun=(engine->fdebug & stall_bit)!=0;
    unsigned hardware_oe=(engine->dbg_padoe>>DM)&3;
    pio_sm_set_enabled(engine,sm,false);
    dma_channel_abort(dma);
    /* Redundant CPU release as well as the mandatory hardware release. */
    pio_sm_set_pindirs_with_mask(engine,sm,0,3u<<DM);
    unsigned oe=(engine->dbg_padoe>>DM)&3;
    fault=!completed || underrun || hardware_oe!=0 || oe!=0;
    if (completed) frames++;
    printf("{\"event\":\"wave_finished\",\"completed\":%s,\"underrun\":%s,\"pio_oe\":%u,"
           "\"pio_oe_before_cpu_release\":%u,"
           "\"word\":\"16ef\",\"bits\":\"0001011011101111\",\"frames\":%u,"
           "\"setup_cycles\":15,\"high_cycles\":1160,\"low_cycles\":1155,"
           "\"release_cycles\":3}\n",completed?"true":"false",underrun?"true":"false",oe,hardware_oe,frames);
    status();
}

int main(void)
{
    gpio_init(DM); gpio_init(DP);
    gpio_set_drive_strength(DM,GPIO_DRIVE_STRENGTH_2MA);
    gpio_set_drive_strength(DP,GPIO_DRIVE_STRENGTH_2MA);
    gpio_set_slew_rate(DM,GPIO_SLEW_RATE_SLOW);
    gpio_set_slew_rate(DP,GPIO_SLEW_RATE_SLOW);
    gpio_pull_down(DM); gpio_pull_down(DP);
    set_sys_clock_khz(120000,true);
    sm=pio_claim_unused_sm(engine,true);
    offset=pio_add_program(engine,&t3p_scope_key_program);
    pio_gpio_init(engine,DM); pio_gpio_init(engine,DP);
    pio_sm_config cfg=t3p_scope_key_program_get_default_config(offset);
    sm_config_set_out_pins(&cfg,DM,2);
    sm_config_set_set_pins(&cfg,DM,2);
    sm_config_set_out_shift(&cfg,true,false,32);
    sm_config_set_fifo_join(&cfg,PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&cfg,1.0f);
    pio_sm_init(engine,sm,offset,&cfg);
    pio_sm_set_pindirs_with_mask(engine,sm,0,3u<<DM);
    dma=dma_claim_unused_channel(true);
    stdio_init_all();
    uint64_t next=0;
    char line[32]; unsigned used=0; bool overflow=false;
    for (;;) {
        if (!stdio_usb_connected()) { used=0; overflow=false; next=0; sleep_ms(1); continue; }
        if (time_us_64()>=next) { status(); next=time_us_64()+1000000; }
        int ch=getchar_timeout_us(0);
        if (ch==PICO_ERROR_TIMEOUT) { sleep_ms(1); continue; }
        if (ch=='\r' || ch=='\n') {
            if (used || overflow) {
                line[used]=0;
                if (overflow) puts("{\"event\":\"line_too_long\"}");
                else if (!strcmp(line,"STATUS")) status();
                else if (!strcmp(line,"SCOPE_KEY_ONCE")) emit_once();
                else if (!strcmp(line,"BOOTSEL")) reset_usb_boot(0,0);
                else puts("{\"event\":\"unknown_command\"}");
            }
            used=0; overflow=false;
        } else if (ch>=32 && ch<127) {
            if (used<sizeof(line)-1 && !overflow) line[used++]=(char)ch;
            else overflow=true;
        }
    }
}
