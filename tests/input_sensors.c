/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host tests use synthetic data exclusively; they do not connect to hardware. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "t3p_input.h"
#include "t3p_sensors.h"

static uint32_t tick(struct t3p_input *s, struct t3p_input_sample *p, uint32_t now)
{
    p->now_ms=now;
    return t3p_input_update(s,p);
}
static void input_tests(void)
{
    struct t3p_input state;
    struct t3p_input_sample p={0};
    t3p_input_reset(&state);
    assert(tick(&state,&p,0)==0);
    p.buttons_valid=1; p.buttons=1;
    assert(tick(&state,&p,10)==0);
    p.buttons=0; assert(tick(&state,&p,15)==0); /* bounce */
    p.buttons=1; assert(tick(&state,&p,20)==0);
    assert(tick(&state,&p,39)==0);
    assert(tick(&state,&p,40)==T3P_FIRE);
    p.buttons_valid=0; assert(tick(&state,&p,41)==0); /* no stuck fire */
    p.buttons_valid=1; p.buttons=3;
    assert(tick(&state,&p,50)==0);
    assert(tick(&state,&p,70)==0);
    assert(tick(&state,&p,769)==0);
    assert(tick(&state,&p,770)==T3P_ESCAPE);
    assert(tick(&state,&p,771)==0); /* once per hold */
    p.buttons_valid=0; assert(tick(&state,&p,772)==0);
    t3p_input_reset(&state);
    p.buttons_valid=1; p.buttons=3;
    tick(&state,&p,0xfffffff0u); tick(&state,&p,4);
    assert(tick(&state,&p,704)==T3P_ESCAPE); /* clock wraps */

    t3p_input_reset(&state); memset(&p,0,sizeof(p));
    p.tilt_valid=1; p.tilt_x=400; p.tilt_y=-400;
    assert(tick(&state,&p,0)==(T3P_RIGHT|T3P_FORWARD));
    p.touch_valid=p.touch_down=1; p.touch_x=0; p.touch_y=600;
    assert(tick(&state,&p,1)==(T3P_LEFT|T3P_FORWARD)); /* override */
    p.touch_valid=0; p.tilt_valid=0;
    assert(tick(&state,&p,2)==0); /* disconnect releases everything */
    p.tilt_valid=1; p.tilt_x=400; p.tilt_y=400; p.menu=1;
    assert(tick(&state,&p,3)==0); /* menu does not follow tilt */
    p.touch_valid=p.touch_down=1; p.touch_x=800; p.touch_y=400;
    assert(tick(&state,&p,4)==T3P_ENTER);
    p.menu=0; p.tilt_valid=0; p.touch_x=1023; p.touch_y=0;
    assert(tick(&state,&p,5)==(T3P_WEAPON_1<<6));
    p.touch_x=128; assert(tick(&state,&p,6)==T3P_WEAPON_1);
    p.touch_x=1024; assert(tick(&state,&p,7)==0);
    p.touch_x=800; p.touch_y=400;
    assert(tick(&state,&p,8)==T3P_FIRE);
    p.touch_down=0; assert(tick(&state,&p,9)==0);

    /* Sweep coordinates and tilt to catch contradictory direction states and
     * out-of-range weapon bit shifts across all zone boundaries. */
    for (unsigned x=0;x<1200;x+=13) for (unsigned y=0;y<1200;y+=17) {
        p.touch_x=(uint16_t)x; p.touch_y=(uint16_t)y; p.touch_down=1;
        p.tilt_valid=1; p.tilt_x=(int16_t)((int)x-600); p.tilt_y=(int16_t)((int)y-600);
        uint32_t keys=tick(&state,&p,x+y);
        assert((keys&(T3P_LEFT|T3P_RIGHT)) != (T3P_LEFT|T3P_RIGHT));
        assert((keys&(T3P_FORWARD|T3P_BACK)) != (T3P_FORWARD|T3P_BACK));
        assert(!(keys & ~((1u<<17)-1)));
    }
}

struct mock { unsigned address,reg,count; int returned; uint8_t bytes[6]; };
static void board_button_tests(void)
{
    struct t3p_buttons s = {0};
    const unsigned left = T3P_BUTTON_LEFT, right = T3P_BUTTON_RIGHT, both = T3P_BUTTON_BOTH;

    t3p_buttons_sample(&s, left, 10);
    t3p_buttons_sample(&s, 0, 18);
    t3p_buttons_sample(&s, 0, 38);
    assert(t3p_buttons_take(&s, 0) == 0); /* bounce */
    t3p_buttons_sample(&s, left, 100);
    t3p_buttons_sample(&s, left, 120);
    assert(t3p_buttons_take(&s, 0) == 0); /* wait briefly for the other button */
    t3p_buttons_sample(&s, 0, 135);
    t3p_buttons_sample(&s, 0, 155);
    assert(t3p_buttons_take(&s, 0) == T3P_FIRE); /* tap during rendering */
    assert(t3p_buttons_take(&s, 0) == 0);

    t3p_buttons_sample(&s, left, 200);
    t3p_buttons_sample(&s, left, 249);
    assert(t3p_buttons_take(&s, 0) == 0);
    t3p_buttons_sample(&s, left, 250);
    assert(t3p_buttons_take(&s, 0) == T3P_FIRE);
    assert(t3p_buttons_take(&s, 0) == T3P_FIRE); /* held to repeat */
    t3p_buttons_sample(&s, 0, 270);
    t3p_buttons_sample(&s, 0, 290);
    assert(t3p_buttons_take(&s, 0) == 0);

    t3p_buttons_sample(&s, right, 300);
    t3p_buttons_sample(&s, right, 350);
    assert(t3p_buttons_take(&s, 0) == T3P_USE);
    assert(t3p_buttons_take(&s, 0) == 0); /* held right only uses once */
    t3p_buttons_sample(&s, 0, 360);
    t3p_buttons_sample(&s, 0, 380);
    t3p_buttons_sample(&s, right, 400);
    t3p_buttons_sample(&s, right, 420);
    t3p_buttons_sample(&s, 0, 431);
    t3p_buttons_sample(&s, 0, 451);
    assert(t3p_buttons_take(&s, 0) == T3P_USE);
    t3p_buttons_sample(&s, right, 460);
    t3p_buttons_sample(&s, right, 480);
    t3p_buttons_sample(&s, 0, 492);
    t3p_buttons_sample(&s, 0, 512);
    assert(t3p_buttons_take(&s, 0) == 0); /* force release between queued uses */
    assert(t3p_buttons_take(&s, 0) == T3P_USE);
    assert(t3p_buttons_take(&s, 0) == 0);

    t3p_buttons_sample(&s, left, 600);
    t3p_buttons_sample(&s, left, 620);
    t3p_buttons_sample(&s, both, 637);
    t3p_buttons_sample(&s, both, 657);
    t3p_buttons_sample(&s, both, 1356);
    assert(t3p_buttons_take(&s, 0) == 0);
    t3p_buttons_sample(&s, both, 1357);
    assert(t3p_buttons_take(&s, 0) == T3P_ESCAPE);
    assert(t3p_buttons_take(&s, 1) == 0);
    t3p_buttons_sample(&s, right, 1400);
    t3p_buttons_sample(&s, right, 1420);
    assert(t3p_buttons_take(&s, 1) == 0); /* partial release cannot close menu */
    t3p_buttons_sample(&s, both, 1430);
    t3p_buttons_sample(&s, both, 2150);
    assert(t3p_buttons_take(&s, 1) == 0); /* require full release to rearm */
    t3p_buttons_sample(&s, 0, 2160);
    t3p_buttons_sample(&s, 0, 2180);

    t3p_buttons_sample(&s, left, 2200);
    t3p_buttons_sample(&s, left, 2250);
    assert(t3p_buttons_take(&s, 1) == T3P_ENTER);
    assert(t3p_buttons_take(&s, 1) == 0);
    t3p_buttons_sample(&s, 0, 2260);
    t3p_buttons_sample(&s, 0, 2280);
    t3p_buttons_sample(&s, right, 2300);
    t3p_buttons_sample(&s, right, 2350);
    assert(t3p_buttons_take(&s, 1) == T3P_ESCAPE);
    assert(t3p_buttons_take(&s, 0) == 0); /* closing the menu cannot also use */

    s = (struct t3p_buttons){0};
    t3p_buttons_sample(&s, right, 1);
    t3p_buttons_sample(&s, right, 21);
    t3p_buttons_sample(&s, both, 30);
    t3p_buttons_sample(&s, both, 50);
    t3p_buttons_sample(&s, left, 200);
    t3p_buttons_sample(&s, left, 220);
    t3p_buttons_sample(&s, left, 800);
    assert(t3p_buttons_take(&s, 0) == 0); /* aborted chord cannot fire or use */

    s = (struct t3p_buttons){0};
    t3p_buttons_sample(&s, both, UINT32_MAX - 30);
    t3p_buttons_sample(&s, both, UINT32_MAX - 10);
    t3p_buttons_sample(&s, both, 689);
    t3p_buttons_sample(&s, 0, 700);
    t3p_buttons_sample(&s, 0, 720);
    assert(t3p_buttons_take(&s, 0) == T3P_ESCAPE); /* wraps and survives release */
    assert(t3p_buttons_take(&s, 1) == 0);
}

static int mock_read(void *ctx,uint8_t address,uint8_t reg,uint8_t *data,unsigned count)
{
    struct mock *m=ctx;
    assert(address==m->address && reg==m->reg && count==m->count);
    memcpy(data,m->bytes,count);
    return m->returned;
}
static void sensor_tests(void)
{
    struct mock m={T3P_SC7A20,0xa8,6,6,{0,0x80,0xf0,0x7f,0xf0,0xff}};
    struct t3p_sensor_bus bus={.read=mock_read,.ctx=&m};
    struct t3p_accel_raw accel;
    assert(t3p_accel_read(&bus,&accel)==0);
    assert(accel.x==-2048 && accel.y==2047 && accel.z==-1);
    m.returned=5; assert(t3p_accel_read(&bus,&accel)==-1);
    assert(t3p_accel_read(0,&accel)==-1);
    assert(t3p_accel_read(&bus,0)==-1);
    m=(struct mock){T3P_CST816D,2,5,5,{1,0xa1,0x23,0xb2,0x34}};
    struct t3p_touch_raw touch;
    assert(t3p_touch_read(&bus,&touch)==0);
    assert(touch.down && touch.x==0x123 && touch.y==0x234);
    m.returned=-1; assert(t3p_touch_read(&bus,&touch)==-1 && !touch.down);
    m.returned=5; m.bytes[0]=0;
    assert(t3p_touch_read(&bus,&touch)==0 && !touch.down);
    m.bytes[0]=2; assert(t3p_touch_read(&bus,&touch)==-1 && !touch.down);
    m=(struct mock){T3P_BMP280,0xf7,6,6,{0x65,0x5a,0xc0,0x7e,0xed,0x00}};
    struct t3p_pressure_raw pressure;
    assert(t3p_pressure_read(&bus,&pressure)==0);
    assert(pressure.pressure==415148 && pressure.temperature==519888);
    m.bytes[0]=0x80; m.bytes[1]=m.bytes[2]=0;
    assert(t3p_pressure_read(&bus,&pressure)==-1);
    m.returned=0; assert(t3p_pressure_read(&bus,&pressure)==-1);
}
int main(void)
{
    input_tests(); board_button_tests(); sensor_tests();
    puts("PASS: button taps, holds, chords, menu, wraparound, touch zones and sensor decoding (mock data only)");
    return 0;
}
