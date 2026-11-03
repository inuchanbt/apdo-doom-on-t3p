/* SPDX-License-Identifier: MIT */
/* PC-facing native USB CDC + GPIO12/13 input sampling only.
 * PULLCHECK briefly enables weak internal pulls with GPIO outputs disabled.
 * No PIO host, USB_KEY signal, target traffic, or target power switching.
 * Intended first test: CM board alone, male port empty.
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/bootrom.h"

enum { PIN_TARGET_DP=13, PIN_TARGET_DM=12 };
static const char *pulls="disabled";

static void status(void)
{
    printf("{\"program\":\"t3p_cm_input_check\",\"version\":2,"
           "\"board\":\"RP2350-USB-CM\",\"mode\":\"inputs_only\","
           "\"dp_gpio\":13,\"dm_gpio\":12,\"dp\":%u,\"dm\":%u,"
           "\"pulls\":\"%s\",\"uptime_ms\":%llu}\n",
           (unsigned)gpio_get(PIN_TARGET_DP),(unsigned)gpio_get(PIN_TARGET_DM),
           pulls,
           (unsigned long long)(time_us_64()/1000));
}

static void pull_check(void)
{
    /* Bench test only: male port empty. These are weak input pulls, not
     * USB host termination or a USB_KEY transmitter. Always finish released. */
    gpio_pull_down(PIN_TARGET_DP);
    gpio_pull_down(PIN_TARGET_DM);
    pulls="down";
    sleep_ms(5);
    status();
    gpio_pull_up(PIN_TARGET_DP);
    gpio_pull_up(PIN_TARGET_DM);
    pulls="up";
    sleep_ms(5);
    status();
    gpio_pull_down(PIN_TARGET_DP);
    gpio_pull_up(PIN_TARGET_DM);
    pulls="dp_down_dm_up";
    sleep_ms(5);
    status();
    gpio_pull_up(PIN_TARGET_DP);
    gpio_pull_down(PIN_TARGET_DM);
    pulls="dp_up_dm_down";
    sleep_ms(5);
    status();
    gpio_disable_pulls(PIN_TARGET_DP);
    gpio_disable_pulls(PIN_TARGET_DM);
    pulls="disabled";
    sleep_ms(5);
    status();
    puts("PULLCHECK complete; weak pulls disabled");
}

int main(void)
{
    /* Set direction to input before selecting the SIO function; output
     * latches are irrelevant while output enable remains cleared. */
    gpio_init(PIN_TARGET_DP);
    gpio_init(PIN_TARGET_DM);
    gpio_set_dir(PIN_TARGET_DP,GPIO_IN);
    gpio_set_dir(PIN_TARGET_DM,GPIO_IN);
    gpio_disable_pulls(PIN_TARGET_DP);
    gpio_disable_pulls(PIN_TARGET_DM);
    stdio_init_all();
    bool was_connected=false;
    uint64_t next=0;
    char line[32];
    unsigned used=0;
    bool overflow=false;
    for (;;) {
        bool connected=stdio_usb_connected();
        if (!connected) {
            was_connected=false; used=0; overflow=false;
            sleep_ms(10);
            continue;
        }
        if (!was_connected) {
            puts("T3P CM input-only check. Male port must be empty for this bench test.");
            puts("Commands: STATUS, HELP, PULLCHECK, BOOTSEL. GPIO outputs stay disabled.");
            next=0; was_connected=true;
        }
        uint64_t now=time_us_64();
        if (now>=next) { status(); next=now+1000000; }
        int ch=getchar_timeout_us(0);
        if (ch==PICO_ERROR_TIMEOUT) { sleep_ms(1); continue; }
        if (ch=='\r' || ch=='\n') {
            if (used || overflow) {
                line[used]=0;
                if (overflow) puts("ERROR line too long");
                else if (!strcmp(line,"STATUS")) status();
                else if (!strcmp(line,"HELP")) puts("STATUS HELP PULLCHECK BOOTSEL; PULLCHECK requires male port empty");
                else if (!strcmp(line,"PULLCHECK")) pull_check();
                else if (!strcmp(line,"BOOTSEL")) {
                    puts("Rebooting this CM board to BOOTSEL");
                    sleep_ms(50);
                    reset_usb_boot(0,0);
                } else puts("ERROR unknown command");
            }
            used=0; overflow=false;
        } else if (ch>=32 && ch<127) {
            if (used<sizeof(line)-1 && !overflow) line[used++]=(char)ch;
            else overflow=true;
        }
    }
}
