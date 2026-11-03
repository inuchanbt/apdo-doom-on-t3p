/* Generated from exact stock3.0.3 ST7789V initialization records. */
#include "t3p_bsp.h"
int t3p_bsp_lcd_sequence(struct t3p_bsp *b)
{
    if(t3p_bsp_command(b,0x01,0,0))return -2;
    if(t3p_bsp_delay(b,120))return -2;
    if(t3p_bsp_command(b,0x11,0,0))return -2;
    if(t3p_bsp_delay(b,120))return -2;
    static const uint8_t data4[]={0x0};
    if(t3p_bsp_command(b,0x36,data4,1))return -2;
    static const uint8_t data5[]={0x5};
    if(t3p_bsp_command(b,0x3a,data5,1))return -2;
    static const uint8_t data6[]={0xc,0xc,0x0,0x33,0x33};
    if(t3p_bsp_command(b,0xb2,data6,5))return -2;
    static const uint8_t data7[]={0x22};
    if(t3p_bsp_command(b,0xb7,data7,1))return -2;
    static const uint8_t data8[]={0x36};
    if(t3p_bsp_command(b,0xbb,data8,1))return -2;
    static const uint8_t data9[]={0x1};
    if(t3p_bsp_command(b,0xc2,data9,1))return -2;
    static const uint8_t data10[]={0x19};
    if(t3p_bsp_command(b,0xc3,data10,1))return -2;
    static const uint8_t data11[]={0x20};
    if(t3p_bsp_command(b,0xc4,data11,1))return -2;
    static const uint8_t data12[]={0xf};
    if(t3p_bsp_command(b,0xc6,data12,1))return -2;
    static const uint8_t data13[]={0xa4,0xa1};
    if(t3p_bsp_command(b,0xd0,data13,2))return -2;
    static const uint8_t data14[]={0xd0,0x4,0xd,0x11,0x13,0x2b,0x3f,0x54,0x4c,0x18,0xd,0xb,0x1f,0x23};
    if(t3p_bsp_command(b,0xe0,data14,14))return -2;
    static const uint8_t data15[]={0xd0,0x4,0xc,0x11,0x13,0x2c,0x3f,0x44,0x51,0x2f,0x1f,0x1f,0x20,0x23};
    if(t3p_bsp_command(b,0xe1,data15,14))return -2;
    if(t3p_bsp_command(b,0x21,0,0))return -2;
    static const uint8_t data17[]={0x0,0x0,0x0,0xef};
    if(t3p_bsp_command(b,0x2a,data17,4))return -2;
    static const uint8_t data18[]={0x0,0x0,0x0,0xef};
    if(t3p_bsp_command(b,0x2b,data18,4))return -2;
    if(t3p_bsp_command(b,0x29,0,0))return -2;
    if(t3p_bsp_delay(b,20))return -2;
    if(t3p_bsp_command(b,0x2c,0,0))return -2;
    if(t3p_bsp_delay(b,20))return -2;
    return 0;
}
