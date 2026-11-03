/* SPDX-License-Identifier: MIT
 * Explicit ARM + one target-VBUS edge -> one fixed key -> descriptor-only USB.
 * No automatic retransmission, HID requests, MSC/SCSI, loader or flash access.
 * Weak-pullup line samples are observations, NEVER accepted as proof of ACK.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/adc.h"
#include "pio_usb.h"
#include "tusb.h"
#include "host/usbh_pvt.h"
#include "key_wave.pio.h"
#include "../cm_wave_scope/wave_plan.h"
#include "stable_power_gate.h"
#include "../cm_rom_probe/ack_observer.h"
enum { BIAS=10,DM=12,DP=13,VBUS_ADC=26 };
static unsigned key_delay_us=1000;
typedef struct { char text[768]; } message_t;
static queue_t messages;
static power_gate_t gate;
static uint16_t adc_raw;
static bool adc_on_checked,adc_off_checked;
static uint64_t last_sample,max_gap;
static bool host_requested,fault;
static bool bias_active,bias_checked;
static unsigned frames;
static const PIO engine=pio0;
static uint sm,offset;
static int dma;
static bool wave_owned;

static void event(const char *format,...) __attribute__((format(printf,1,2)));
static void event(const char *format,...)
{
    message_t m; va_list args; va_start(args,format);
    vsnprintf(m.text,sizeof(m.text),format,args); va_end(args);
    queue_try_add(&messages,&m);
}
static void pulls_off(void) { gpio_disable_pulls(DP); gpio_disable_pulls(DM); }
static void bias_off(void)
{ gpio_set_dir(BIAS,GPIO_IN); gpio_disable_pulls(BIAS); bias_active=false; }
static void bias_on(void)
{ gpio_put(BIAS,1); gpio_set_dir(BIAS,GPIO_OUT); bias_active=true; }
static void release_wave(void)
{
    if (!wave_owned) return;
    pio_sm_set_enabled(engine,sm,false); dma_channel_abort(dma);
    pio_sm_set_pindirs_with_mask(engine,sm,0,3u<<DM);
    pio_remove_program(engine,&t3p_scope_key_program,offset);
    pio_sm_unclaim(engine,sm); dma_channel_unclaim(dma);
    gpio_init(DP); gpio_init(DM); pulls_off(); wave_owned=false;
}
static void status(void)
{
    event("{\"program\":\"t3p_cm_power_probe\",\"version\":3,\"event\":\"status\","
          "\"board\":\"RP2350-USB-CM\",\"key_clock_gpio\":13,\"key_data_gpio\":12,"
          "\"armed\":%s,\"used\":%s,\"host_requested\":%s,\"fault\":%s,\"frames\":%u,"
          "\"clock_hz\":%lu,\"bias_gpio\":10,\"bias_active\":%s,\"bias_checked\":%s,"
          "\"adc_gpio\":26,\"adc_channel\":0,\"adc_raw\":%u,\"adc_on_checked\":%s,"
          "\"adc_off_checked\":%s,\"key_delay_us\":%u,\"max_armed_sample_gap_us\":%llu,"
          "\"power_policy\":\"stable_before_first_key\",\"power_settle_limit_us\":50000,\"power_bounces\":%u}\n",
          gate.armed?"true":"false",gate.used?"true":"false",host_requested?"true":"false",
          fault?"true":"false",frames,(unsigned long)clock_get_hz(clk_sys),
          bias_active?"true":"false",bias_checked?"true":"false",adc_raw,
          adc_on_checked?"true":"false",adc_off_checked?"true":"false",key_delay_us,(unsigned long long)max_gap,(unsigned)gate.bounces);
}
static void adc_check(bool on)
{
    unsigned lo=4095,hi=0;
    for (unsigned i=0;i<32;i++) {
        unsigned sample=adc_read(); if(sample<lo)lo=sample; if(sample>hi)hi=sample;
        power_poll(&gate,(uint16_t)sample,time_us_64());
        busy_wait_us_32(5);
    }
    adc_raw=adc_read();
    bool passed=on?(lo>=POWER_ON_MIN && hi<=3352):
        (hi<=POWER_OFF_MAX && gate.off_tracking && time_us_64()-gate.off_since>=POWER_OFF_US);
    if (on) { adc_on_checked=passed; adc_off_checked=false; }
    else adc_off_checked=adc_on_checked && passed;
    event("{\"event\":\"adc_check_finished\",\"on\":%s,\"min_raw\":%u,\"max_raw\":%u,"
          "\"passed\":%s,\"adc_on_checked\":%s,\"adc_off_checked\":%s}\n",
          on?"true":"false",lo,hi,passed?"true":"false",
          adc_on_checked?"true":"false",adc_off_checked?"true":"false");
}
static void bias_check(void)
{
    gpio_pull_down(DP); gpio_pull_down(DM); busy_wait_us_32(1000);
    if (gpio_get(DP) || gpio_get(DM)) {
        pulls_off(); event("{\"event\":\"bias_check_rejected\",\"reason\":\"T3P must be disconnected\"}\n"); return;
    }
    bias_on(); busy_wait_us_32(2000);
    bool high=gpio_get(DP);
    bias_off(); busy_wait_us_32(2000);
    bool low=!gpio_get(DP);
    bias_checked=high && low && !gpio_is_dir_out(BIAS);
    pulls_off();
    event("{\"event\":\"bias_check_finished\",\"high_seen\":%s,\"low_seen\":%s,"
          "\"bias_released\":%s,\"passed\":%s}\n",high?"true":"false",low?"true":"false",
          !gpio_is_dir_out(BIAS)?"true":"false",bias_checked?"true":"false");
}

/* Claim descriptors without opening endpoints or sending class commands. */
static bool descriptor_init(void) { return true; }
static bool descriptor_open(uint8_t rh,uint8_t addr,const tusb_desc_interface_t *itf,uint16_t len)
{
    (void)rh;
    if (!itf || len<sizeof(*itf)) return false;
    uint16_t vid=0,pid=0; tuh_vid_pid_get(addr,&vid,&pid);
    if (!((vid==0x4c4a && pid==0x2942) || (vid==0x0483 && pid==0x5750))) return false;
    event("{\"event\":\"interface_descriptor\",\"vid\":%u,\"pid\":%u,\"class\":%u,"
          "\"subclass\":%u,\"protocol\":%u,\"endpoints\":%u,\"class_requests\":false}\n",
          vid,pid,itf->bInterfaceClass,itf->bInterfaceSubClass,itf->bInterfaceProtocol,itf->bNumEndpoints);
    return true;
}
static bool descriptor_config(uint8_t addr,uint8_t itf)
{ usbh_driver_set_config_complete(addr,itf); return true; }
static bool descriptor_transfer(uint8_t addr,uint8_t ep,xfer_result_t result,uint32_t bytes)
{ (void)addr;(void)ep;(void)result;(void)bytes; return false; }
static void descriptor_close(uint8_t addr) { (void)addr; }
static const usbh_class_driver_t descriptor_driver={
    .name="descriptors only",.init=descriptor_init,.deinit=descriptor_init,
    .open=descriptor_open,.set_config=descriptor_config,
    .xfer_cb=descriptor_transfer,.close=descriptor_close
};
usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *count)
{ *count=1; return &descriptor_driver; }
void tuh_mount_cb(uint8_t addr)
{
    uint16_t vid=0,pid=0; tuh_vid_pid_get(addr,&vid,&pid);
    event("{\"event\":\"device_mounted\",\"vid\":%u,\"pid\":%u,\"rom_identity_match\":%s,"
          "\"stock_identity_match\":%s}\n",vid,pid,
          vid==0x4c4a && pid==0x2942?"true":"false",vid==0x0483 && pid==0x5750?"true":"false");
}
void tuh_umount_cb(uint8_t addr)
{ event("{\"event\":\"device_removed\",\"address\":%u}\n",addr); }
static void host_main(void)
{
    pio_usb_configuration_t cfg=PIO_USB_DEFAULT_CONFIG;
    cfg.pin_dp=DP; cfg.pinout=PIO_USB_PINOUT_DMDP;
    if (!tuh_configure(1,TUH_CFGID_RPI_PIO_USB_CONFIGURATION,&cfg) || !tuh_init(1)) {
        event("{\"event\":\"host_init_failed\"}\n"); for (;;) tight_loop_contents();
    }
    event("{\"event\":\"host_started\",\"descriptor_only\":true}\n");
    for (;;) tuh_task();
}
static void start_host(void)
{
    gate.armed=false; bias_off(); pulls_off(); host_requested=true;
    release_wave();
    multicore_launch_core1(host_main);
}
static void key_and_handoff(void)
{
    uint32_t words[WAVE_WORDS]; wave_plan(words);
    pio_sm_set_enabled(engine,sm,false); pio_sm_clear_fifos(engine,sm);
    pio_sm_restart(engine,sm); pio_sm_exec(engine,sm,pio_encode_jmp(offset));
    pio_interrupt_clear(engine,0);
    pio_sm_set_pins_with_mask(engine,sm,0,3u<<DM);
    pio_sm_set_pindirs_with_mask(engine,sm,3u<<DM,3u<<DM);
    dma_channel_config cfg=dma_channel_get_default_config(dma);
    channel_config_set_transfer_data_size(&cfg,DMA_SIZE_32);
    channel_config_set_read_increment(&cfg,true); channel_config_set_write_increment(&cfg,false);
    channel_config_set_dreq(&cfg,pio_get_dreq(engine,sm,true));
    dma_channel_configure(dma,&cfg,&engine->txf[sm],words,WAVE_WORDS,true);
    uint64_t deadline=time_us_64()+20000;
    while (pio_sm_is_tx_fifo_empty(engine,sm) && time_us_64()<deadline) tight_loop_contents();
    uint32_t stall=1u<<(PIO_FDEBUG_TXSTALL_LSB+sm); engine->fdebug=stall;
    adc_raw=adc_read();
    if (adc_raw<POWER_HOLD_MIN) {
        pio_sm_set_pindirs_with_mask(engine,sm,0,3u<<DM); dma_channel_abort(dma);
        fault=true; bias_off();
        event("{\"event\":\"power_lost_before_key\",\"host_started\":false}\n"); return;
    }
    uint64_t pio_enable_us=time_us_64();
    pio_sm_set_enabled(engine,sm,true);
    while (!pio_interrupt_get(engine,0) && time_us_64()<deadline) tight_loop_contents();
    bool complete=pio_interrupt_get(engine,0),underrun=(engine->fdebug&stall)!=0;
    unsigned oe=(engine->dbg_padoe>>DM)&3;
    pio_sm_set_enabled(engine,sm,false); dma_channel_abort(dma);
    pio_sm_set_pindirs_with_mask(engine,sm,0,3u<<DM);
    fault=!complete || underrun || oe!=0 || ((engine->dbg_padoe>>DM)&3)!=0;
    if (complete) frames++;
    /* All direct D+/D- drivers are released BEFORE the separate resistor bias.
     * GPIO10 reaches D+ only through the separately measured 5.098k resistor.
     */
    ack_observer_t observed={0};
    adc_raw=adc_read();
    if (adc_raw<POWER_HOLD_MIN) fault=true;
    if (!fault) { bias_on(); gpio_disable_pulls(DP); ack_begin(&observed,time_us_64()); }
    event("{\"event\":\"key_start_timing\",\"power_edge_us\":%llu,\"pio_enable_us\":%llu,"
          "\"edge_detected\":%s,\"actual_enable_delay_us\":%llu}\n",
          (unsigned long long)gate.edge_at,(unsigned long long)pio_enable_us,
          gate.edge_seen?"true":"false",
          (unsigned long long)(gate.edge_seen?pio_enable_us-gate.edge_at:0));
    event("{\"event\":\"key_finished\",\"completed\":%s,\"underrun\":%s,"
          "\"pio_oe_before_cpu_release\":%u,\"frames\":%u,\"fault\":%s}\n",
          complete?"true":"false",underrun?"true":"false",oe,frames,fault?"true":"false");
    /* Explicitly free all waveform resources before the USB host claims PIO/DMA. */
    release_wave();
    if (fault) { bias_off(); return; }
    while (!observed.done) {
        adc_raw=adc_read();
        if (adc_raw<POWER_HOLD_MIN) { bias_off(); fault=true; break; }
        ack_sample(&observed,gpio_get(DP),time_us_64());
    }
    if (fault) { bias_off(); event("{\"event\":\"power_lost_after_key\",\"host_started\":false}\n"); return; }
    bool high_with_bias=gpio_get(DP);
    bias_off();
    bool bias_released=!gpio_is_dir_out(BIAS);
    if (!bias_released) fault=true;
    event("{\"event\":\"biased_line_observation\",\"low_pulse_candidate\":%s,"
          "\"observed_low_us\":%llu,\"ended_high_with_bias\":%s,\"bias_released\":%s,\"rom_ack_proven\":false}\n",
          ack_low_candidate(&observed)?"true":"false",(unsigned long long)observed.low_us,
          high_with_bias?"true":"false",bias_released?"true":"false");
    if (fault) { event("{\"event\":\"bias_release_failed\",\"host_started\":false}\n"); return; }
    /* Do not start a USB driver while the externally biased line remains low. */
    if (observed.saw_low && !ack_low_candidate(&observed)) {
        fault=true; event("{\"event\":\"line_held_low_timeout\",\"host_started\":false}\n"); return;
    }
    start_host();
}
int main(void)
{
    gpio_init(BIAS); bias_off();
    adc_init(); adc_gpio_init(VBUS_ADC); adc_select_input(0);
    gpio_init(DP); gpio_init(DM); pulls_off(); set_sys_clock_khz(120000,true);
    gpio_set_drive_strength(DP,GPIO_DRIVE_STRENGTH_2MA);
    gpio_set_drive_strength(DM,GPIO_DRIVE_STRENGTH_2MA);
    gpio_set_slew_rate(DP,GPIO_SLEW_RATE_SLOW); gpio_set_slew_rate(DM,GPIO_SLEW_RATE_SLOW);
    sm=pio_claim_unused_sm(engine,true); offset=pio_add_program(engine,&t3p_scope_key_program);
    pio_gpio_init(engine,DM); pio_gpio_init(engine,DP);
    pio_sm_config cfg=t3p_scope_key_program_get_default_config(offset);
    sm_config_set_out_pins(&cfg,DM,2); sm_config_set_set_pins(&cfg,DM,2);
    sm_config_set_out_shift(&cfg,true,false,32); sm_config_set_fifo_join(&cfg,PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&cfg,1.0f); pio_sm_init(engine,sm,offset,&cfg);
    pio_sm_set_pindirs_with_mask(engine,sm,0,3u<<DM);
    dma=dma_claim_unused_channel(true);
    wave_owned=true;
    queue_init(&messages,sizeof(message_t),24); tud_init(0);
    char line[32]; unsigned used=0; bool overflow=false;
    message_t pending; size_t sent=0,total=0; uint64_t next=0;
    for (;;) {
        tud_task();
        if (!tud_cdc_connected()) {
            if (gate.armed) { power_disarm(&gate); bias_off(); pulls_off(); }
            adc_raw=adc_read(); power_poll(&gate,adc_raw,time_us_64());
            total=0; used=0; overflow=false; next=0;
            message_t discard; while (queue_try_remove(&messages,&discard)) {}
            continue;
        }
        adc_raw=adc_read(); uint64_t sampled=time_us_64();
        if (gate.armed && last_sample && sampled-last_sample>max_gap) max_gap=sampled-last_sample;
        last_sample=sampled;
        unsigned action=power_poll(&gate,adc_raw,sampled);
        if (action==POWER_FIRE) {
            event("{\"event\":\"power_edge\",\"sample_us\":%llu,\"adc_raw\":%u,\"bounces_before_key\":%u}\n",
                  (unsigned long long)gate.edge_at,adc_raw,(unsigned)gate.bounces);
            key_and_handoff();
        } else if (action==POWER_CANDIDATE && gate.bounces<4) {
            event("{\"event\":\"power_candidate\",\"sample_us\":%llu,\"adc_raw\":%u}\n",
                  (unsigned long long)sampled,adc_raw);
        } else if (action==POWER_BOUNCE && gate.bounces<=4) {
            event("{\"event\":\"power_bounce\",\"sample_us\":%llu,\"adc_raw\":%u,\"count\":%u}\n",
                  (unsigned long long)sampled,adc_raw,(unsigned)gate.bounces);
        } else if (action==POWER_UNSTABLE) {
            fault=true; bias_off(); pulls_off();
            event("{\"event\":\"power_unstable_timeout\",\"host_started\":false,\"bounces\":%u}\n",(unsigned)gate.bounces);
        } else if (action==POWER_EXPIRED) {
            bias_off(); pulls_off(); event("{\"event\":\"arm_expired\"}\n");
        }
        if (time_us_64()>=next) { status(); next=time_us_64()+1000000; }
        if (!total && queue_try_remove(&messages,&pending)) { total=strlen(pending.text); sent=0; }
        if (total) {
            sent+=tud_cdc_write(pending.text+sent,(uint32_t)(total-sent)); tud_cdc_write_flush();
            if (sent==total) total=0;
        }
        while (tud_cdc_available()) {
            char ch=(char)tud_cdc_read_char();
            if (ch=='\r' || ch=='\n') {
                line[used]=0;
                if (!used && !overflow) continue;
                if (overflow) event("{\"event\":\"line_too_long\"}\n");
                else if (!strcmp(line,"STATUS")) status();
                else if (!strcmp(line,"BOOTSEL")) reset_usb_boot(0,0);
                else if (!strcmp(line,"DISARM")) { power_disarm(&gate); bias_off(); if (!host_requested) pulls_off(); status(); }
                else if ((!strcmp(line,"DELAY_100") || !strcmp(line,"DELAY_1000") || !strcmp(line,"DELAY_5000")) && !host_requested && !fault && !gate.used && !gate.armed) {
                    key_delay_us=!strcmp(line,"DELAY_100")?100:!strcmp(line,"DELAY_1000")?1000:5000;
                    event("{\"event\":\"delay_set\",\"key_delay_us\":%u,\"signal_sent\":false}\n",key_delay_us);
                }
                else if (!strcmp(line,"ADC_ON_CHECK") && !host_requested && !fault && !gate.used && !gate.armed) adc_check(true);
                else if (!strcmp(line,"ADC_OFF_CHECK") && !host_requested && !fault && !gate.used && !gate.armed) adc_check(false);
                else if (!strcmp(line,"BIAS_CHECK") && adc_on_checked && adc_raw>=POWER_ON_MIN && !host_requested && !fault && !gate.used && !gate.armed) bias_check();
                else if (!strcmp(line,"HOST_START") && adc_on_checked && adc_off_checked && adc_raw>=POWER_ON_MIN && !host_requested && !fault && !gate.used && !gate.armed) {
                    gate.used=true; start_host();
                }
                else if (!strcmp(line,"BENCH_FRAME_AND_HOST") && adc_on_checked && adc_off_checked && adc_raw>=POWER_ON_MIN && bias_checked && !host_requested && !fault && !gate.used && !gate.armed) {
                    /* Explicit disconnected-target bench test, one frame per boot. */
                    gpio_pull_down(DP); gpio_pull_down(DM); busy_wait_us_32(100);
                    if (!gpio_get(DP) && !gpio_get(DM)) { gate.used=true; key_and_handoff(); }
                    else { pulls_off(); event("{\"event\":\"bench_rejected\"}\n"); }
                }
                else if (!strcmp(line,"ARM_STABLE_POWER_ONCE") && adc_on_checked && adc_off_checked && bias_checked && !host_requested && !fault && !gate.used && !gate.armed) {
                    /* No pullups or bias while target power is off. */
                    bias_off(); pulls_off(); adc_raw=adc_read();
                    if (power_arm(&gate,adc_raw,time_us_64(),key_delay_us)) {
                        max_gap=0; last_sample=time_us_64();
                        event("{\"event\":\"armed\",\"timeout_s\":600,\"key_delay_us\":%u,\"settle_limit_us\":50000,\"single_frame\":true}\n",key_delay_us);
                    } else event("{\"event\":\"arm_rejected\",\"reason\":\"needs 100ms powered-off baseline\"}\n");
                } else event("{\"event\":\"unknown_or_repeated_command\"}\n");
                used=0; overflow=false;
            } else if (ch>=32 && ch<127) {
                if (used<sizeof(line)-1 && !overflow) line[used++]=ch; else overflow=true;
            }
        }
    }
}
