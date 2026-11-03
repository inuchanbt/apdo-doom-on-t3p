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
    struct t3p_sensor_bus bus={mock_read,&m};
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
    input_tests(); sensor_tests();
    puts("PASS: input transitions, wraparound, touch zones, sensor decoding and errors (mock data only)");
    return 0;
}
