/* SPDX-License-Identifier: MIT
 * Auxiliary Waveshare RP2350-USB-CM: manual-start HID query probe.
 * Native USB CDC on core0; PIO USB host on core1, only after HOST_START.
 * Only two fixed stock read queries after exact HID descriptor verification.
 * No setting changes, SCSI, RAM loader, USB_KEY or power control.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "pio_usb.h"
#include "tusb.h"

enum { DP=13, DM=12 };
typedef struct { char text[384]; } message_t;
static queue_t messages;
static queue_t commands;
enum { QUERY_NONE, QUERY_SETTINGS, QUERY_MEASUREMENT };
/* The following query state is owned only by core1. */
static uint8_t target_address,target_instance;
static bool target_match,query_fault;
static unsigned pending_query;
static uint64_t query_started;
/* Accessed only by core0. Host events cross cores only through the queue. */
static bool host_requested;

static void event(const char *format,...)
{
    message_t message;
    va_list args;
    va_start(args,format);
    vsnprintf(message.text,sizeof(message.text),format,args);
    va_end(args);
    queue_try_add(&messages,&message);
}

static void status(void)
{
    event("{\"program\":\"t3p_cm_host_probe\",\"version\":2,"
          "\"board\":\"RP2350-USB-CM\",\"event\":\"status\","
          "\"dp_gpio\":13,\"dm_gpio\":12,\"host_requested\":%s,"
          "\"dp\":%u,\"dm\":%u,\"uptime_ms\":%llu}\n",
          host_requested?"true":"false",(unsigned)gpio_get(DP),
          (unsigned)gpio_get(DM),(unsigned long long)(time_us_64()/1000));
}

static void query_start(unsigned kind)
{
    static const uint8_t settings[]={0x55,0x05,0x09,0x03,0,0,0,0xee,0xff};
    static const uint8_t measurement[]={0x55,0x05,0x22,0x05,0x0b,0,0x8c,0xee,0xff};
    uint8_t packet[64]={0};
    if (!target_match || query_fault || pending_query ||
        (kind!=QUERY_SETTINGS && kind!=QUERY_MEASUREMENT)) {
        event("{\"event\":\"query_rejected\"}\n");
        return;
    }
    if (!tuh_hid_receive_ready(target_address,target_instance) ||
        !tuh_hid_send_ready(target_address,target_instance)) {
        query_fault=true;
        event("{\"event\":\"query_endpoint_not_ready\"}\n");
        return;
    }
    memcpy(packet,kind==QUERY_SETTINGS?settings:measurement,sizeof(settings));
    pending_query=kind;
    query_started=time_us_64();
    /* Interrupt OUT copies 64 payload bytes; report_id=0 adds no prefix byte. */
    if (!tuh_hid_receive_report(target_address,target_instance) ||
        !tuh_hid_send_report(target_address,target_instance,0,packet,sizeof(packet))) {
        tuh_hid_receive_abort(target_address,target_instance);
        query_fault=true; pending_query=QUERY_NONE;
        event("{\"event\":\"query_transfer_failed\"}\n");
        return;
    }
    event("{\"event\":\"query_sent\",\"kind\":\"%s\",\"bytes\":64}\n",
          kind==QUERY_SETTINGS?"settings":"measurement");
}

static void host_main(void)
{
    pio_usb_configuration_t cfg=PIO_USB_DEFAULT_CONFIG;
    cfg.pin_dp=DP;
    cfg.pinout=PIO_USB_PINOUT_DMDP;
    if (!tuh_configure(1,TUH_CFGID_RPI_PIO_USB_CONFIGURATION,&cfg) || !tuh_init(1)) {
        event("{\"event\":\"host_init_failed\"}\n");
        for (;;) tight_loop_contents();
    }
    event("{\"event\":\"host_started\",\"clock_khz\":120000,\"dp_gpio\":13,\"dm_gpio\":12}\n");
    for (;;) {
        tuh_task();
        unsigned command;
        if (queue_try_remove(&commands,&command)) query_start(command);
        if (pending_query && time_us_64()-query_started>1000000) {
            tuh_hid_receive_abort(target_address,target_instance);
            pending_query=QUERY_NONE; query_fault=true;
            event("{\"event\":\"query_timeout\"}\n");
        }
    }
}

void tuh_mount_cb(uint8_t address)
{
    uint16_t vid=0,pid=0;
    tuh_vid_pid_get(address,&vid,&pid);
    event("{\"event\":\"device_mounted\",\"address\":%u,\"vid\":%u,\"pid\":%u,"
          "\"expected_t3p\":%s}\n",address,vid,pid,
          vid==0x0483 && pid==0x5750?"true":"false");
}

void tuh_umount_cb(uint8_t address)
{
    if (address==target_address) {
        target_match=false;
        if (pending_query) query_fault=true;
        pending_query=QUERY_NONE;
    }
    event("{\"event\":\"device_removed\",\"address\":%u}\n",address);
}

void tuh_hid_mount_cb(uint8_t address,uint8_t instance,const uint8_t *descriptor,uint16_t length)
{
    static const uint8_t stock[]={0x06,0x02,0xff,0x09,0x02,0xa1,0x01,0x09,0x03,0x15,0x00,
        0x26,0xff,0x00,0x75,0x08,0x95,0x40,0x81,0x00,0x09,0x04,0x15,0x00,0x26,0xff,
        0x00,0x75,0x08,0x95,0x40,0x91,0x00,0xc0};
    uint16_t vid=0,pid=0;
    tuh_vid_pid_get(address,&vid,&pid);
    bool match=vid==0x0483 && pid==0x5750 && descriptor && length==sizeof(stock)
               && !memcmp(descriptor,stock,sizeof(stock));
    if (match) { target_address=address; target_instance=instance; target_match=true; }
    char hex[129];
    unsigned shown=length>64?64:length;
    if (!descriptor) shown=0;
    for (unsigned i=0;i<shown;i++) snprintf(hex+i*2,3,"%02x",descriptor[i]);
    hex[shown*2]=0;
    event("{\"event\":\"hid_descriptor\",\"address\":%u,\"instance\":%u,\"vid\":%u,"
          "\"pid\":%u,\"length\":%u,\"stock_t3p_match\":%s,\"hex\":\"%s\"}\n",
          address,instance,vid,pid,length,match?"true":"false",hex);
    /* Queries require an explicit console command after this exact match. */
}

void tuh_hid_umount_cb(uint8_t address,uint8_t instance)
{
    event("{\"event\":\"hid_removed\",\"address\":%u,\"instance\":%u}\n",address,instance);
}

void tuh_hid_report_received_cb(uint8_t address,uint8_t instance,const uint8_t *report,uint16_t length)
{
    if (!pending_query || !target_match || address!=target_address || instance!=target_instance) return;
    unsigned kind=pending_query;
    pending_query=QUERY_NONE;
    bool valid=length==64 && report && report[0]==0xaa &&
        (kind==QUERY_SETTINGS?(report[1]==0x05 && report[2]==0x40 && report[3]==0x03 &&
                              report[62]==0xee && report[63]==0xff):
                             (report[1]==0x40 && report[2]==0x62 && report[3]==0x05));
    if (!valid) query_fault=true;
    char hex[129]; unsigned shown=report?(length>64?64:length):0;
    for (unsigned i=0;i<shown;i++) snprintf(hex+i*2,3,"%02x",report[i]);
    hex[shown*2]=0;
    event("{\"event\":\"query_reply\",\"kind\":\"%s\",\"valid\":%s,"
          "\"bytes\":%u,\"elapsed_us\":%llu,\"hex\":\"%s\"}\n",
          kind==QUERY_SETTINGS?"settings":"measurement",valid?"true":"false",length,
          (unsigned long long)(time_us_64()-query_started),hex);
}

int main(void)
{
    gpio_init(DP); gpio_init(DM);
    gpio_disable_pulls(DP); gpio_disable_pulls(DM);
    set_sys_clock_khz(120000,true);
    queue_init(&messages,sizeof(message_t),16);
    queue_init(&commands,sizeof(unsigned),4);
    tud_init(0);
    char line[32]; unsigned used=0; bool overflow=false;
    message_t pending; size_t sent=0,total=0;
    uint64_t next_status=0;
    for (;;) {
        tud_task();
        if (!tud_cdc_connected()) {
            total=0; used=0; overflow=false;
            message_t discard;
            while (queue_try_remove(&messages,&discard)) {}
            next_status=0;
            continue;
        }
        if (time_us_64()>=next_status) { status(); next_status=time_us_64()+1000000; }
        if (!total && queue_try_remove(&messages,&pending)) { total=strlen(pending.text); sent=0; }
        if (total) {
            sent+=tud_cdc_write(pending.text+sent,(uint32_t)(total-sent));
            tud_cdc_write_flush();
            if (sent==total) total=0;
        }
        while (tud_cdc_available()) {
            char ch=(char)tud_cdc_read_char();
            if (ch=='\r' || ch=='\n') {
                if (used || overflow) {
                    line[used]=0;
                    if (overflow) event("{\"event\":\"line_too_long\"}\n");
                    else if (!strcmp(line,"STATUS")) status();
                    else if (!strcmp(line,"HOST_START") && !host_requested) {
                        host_requested=true;
                        multicore_launch_core1(host_main);
                    } else if ((!strcmp(line,"Q_SETTINGS") || !strcmp(line,"Q_MEASUREMENT")) && host_requested) {
                        unsigned command=!strcmp(line,"Q_SETTINGS")?QUERY_SETTINGS:QUERY_MEASUREMENT;
                        if (!queue_try_add(&commands,&command)) event("{\"event\":\"command_queue_full\"}\n");
                    } else if (!strcmp(line,"BOOTSEL")) {
                        reset_usb_boot(0,0);
                    } else event("{\"event\":\"unknown_or_repeated_command\"}\n");
                }
                used=0; overflow=false;
            } else if (ch>=32 && ch<127) {
                if (used<sizeof(line)-1 && !overflow) line[used++]=ch;
                else overflow=true;
            }
        }
    }
}
