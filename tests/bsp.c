#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "t3p_bsp.h"
#include "t3p_boot.h"
#include "t3p_display.h"

struct mock {
    uint32_t dbi[64],pa[32],pc[32],clock,pg,tick,step;
    uint32_t operations,fail_at,wait_reads,pending,reads,writes,byte_writes;
    unsigned selected,mode,command,n,data_pos,x0,x1,y0,y1,draws;
    uint8_t payload[64],commands[128];
    uint16_t screen[240*240];
};
static struct mock mock;
static uint16_t expected[240*240];
static uint32_t *reg(struct mock *m,uint32_t a)
{
    if(a>=0xfe2200u && a<0xfe2300u && !(a%4))return &m->dbi[(a-0xfe2200)/4];
    if(a>=0xfd3000u && a<0xfd3080u && !(a%4))return &m->pa[(a-0xfd3000)/4];
    if(a>=0xfd3200u && a<0xfd3280u && !(a%4))return &m->pc[(a-0xfd3200)/4];
    if(a==0xfe1908)return &m->clock;
    if(a==0xfd3e60)return &m->pg;
    assert(!"Unexpected MMIO access, including USB/SFC/measurement/motor");return 0;
}
static uint32_t read32(void *context,uint32_t a)
{
    struct mock *m=context;m->reads++;
    if(a==0xfd0604){uint32_t t=m->tick;m->tick=(m->tick+m->step)&0xffff;return t;}
    if(a==0xfe2204 && m->pending) {
        m->wait_reads++;
        if(m->operations!=m->fail_at && !--m->pending)m->dbi[1]|=1;
    }
    return *reg(m,a);
}
static unsigned be(const uint8_t *p){return p[0]*256u+p[1];}
static void send_byte(struct mock *m,uint8_t byte,unsigned line)
{
    assert(m->selected);
    if(m->mode==2) {
        assert(line==1);m->command=byte;m->data_pos=0;
        if(m->n<128)m->commands[m->n++]=byte;
    } else {
        assert(m->mode==3 && line==0);
        if(m->command==0x2c) {
            unsigned at=m->data_pos/2,w=m->x1-m->x0+1;
            assert(m->x1<240 && m->y1<240 && at<w*(m->y1-m->y0+1));
            uint16_t *pixel=&m->screen[(m->y0+at/w)*240+m->x0+at%w];
            if(!(m->data_pos&1))*pixel=(uint16_t)(byte<<8);
            else *pixel|=byte;
        } else {assert(m->data_pos<64);m->payload[m->data_pos]=byte;}
        m->data_pos++;
    }
}
static void write32(void *context,uint32_t a,uint32_t v)
{
    struct mock *m=context;m->writes++;
    if(a>=0xfd0600 && a<=0xfd060c){if(a==0xfd0604)m->tick=v;return;}
    if(a==0xfe2208){m->dbi[1]&=~v;return;}
    if(a==0xfe22f4) {
        unsigned op=v&15;
        const uint32_t pins=(1u<<7)|(1u<<8)|(1u<<9)|(1u<<12);
        assert(!(m->pa[2]&pins) && (m->pa[3]&pins)==pins);
        assert(m->dbi[9]&1u); /* DBI released from reset. */
        assert(m->pending==0); /* No reuse while prior hardware op is outstanding. */
        m->operations++;m->pending=3;
        if(op==0){assert(!m->selected);m->selected=1;}
        else if(op==1) {
            assert(m->selected);
            if(m->command==0x2a || m->command==0x2b) {
                assert(m->data_pos==4);
                if(m->command==0x2a){m->x0=be(m->payload);m->x1=be(m->payload+2);}
                else {m->y0=be(m->payload);m->y1=be(m->payload+2);}
            }
            if(m->command==0x2c && m->data_pos) {
                assert(m->data_pos==2*(m->x1-m->x0+1)*(m->y1-m->y0+1));m->draws++;
            }
            m->selected=0;
        } else if(op==2 || op==3){assert(m->selected);m->mode=op;}
        else {
            assert(op==8);
            unsigned count=((v>>7)&3)+1;
            for(unsigned i=0;i<count;i++)
                send_byte(m,(uint8_t)(m->dbi[62]>>(8*i)),(v>>4)&7);
        }
    }
    *reg(m,a)=v;
}
static void write8(void *context,uint32_t a,uint8_t v)
{
    struct mock *m=context;
    assert((m->byte_writes==0 && a==0xf2c818 && v==0x40) ||
           (m->byte_writes==1 && a==0xf2c418 && v==0xef));
    m->byte_writes++;
}
static struct t3p_bsp board;
static void reset(void)
{
    memset(&mock,0,sizeof(mock));mock.step=6;mock.clock=0xa5a50100u;
    mock.pa[2]=0xffffffffu; /* Inputs on reset; init must explicitly drive LCD. */
    struct t3p_bsp_io io={&mock,read32,write32,write8};
    assert(!t3p_bsp_init(&board,&io));
}
static uint8_t ram[0x37000];
static unsigned boot_reads;
static int boot_read(void *context,uint32_t address,void *out,uint32_t count)
{
    (void)context;boot_reads++;
    assert(address>=0x100000 && address+count<=0x137000);
    memcpy(out,ram+address-0x100000,count);return 0;
}
static void boot_checks(void)
{
    struct t3p_boot_snapshot s;
    uint32_t context[]={0x200000,0x5100,0x106be0,0x12345678,0xaabbccdd,0xeeff0102};
    memcpy(ram+8,context,24);for(unsigned i=0;i<32;i++)ram[0x6be0+i]=(uint8_t)(0x70+i);
    assert(!t3p_boot_capture(&s,0x100008,boot_read,0));assert(s.valid==T3P_BOOT_MAGIC);
    assert(!memcmp(s.context,context,24));assert(!memcmp(s.auxiliary,ram+0x6be0,32));
    const uint32_t bad[]={0,0xffffffff,0x136fec,0xfe2200,0xc000100,0x100009};
    for(unsigned i=0;i<sizeof bad/sizeof *bad;i++) {
        unsigned reads=boot_reads;assert(t3p_boot_capture(&s,bad[i],boot_read,0)==-1);
        assert(!s.valid && boot_reads==reads);
    }
    context[2]=0xf2c818;memcpy(ram+8,context,24);
    unsigned reads=boot_reads;assert(!t3p_boot_capture(&s,0x100008,boot_read,0));
    assert(s.valid==T3P_BOOT_MAGIC && boot_reads==reads+1);
    for(unsigned i=0;i<32;i++)assert(!s.auxiliary[i]);
}
int main(void)
{
    boot_checks();reset();
    assert(mock.byte_writes==2 && mock.pg==0);
    uint32_t before=mock.clock;
    assert(!t3p_bsp_lcd_init(&board));
    /* Stock's display pins must be digital outputs, even under DBI control.
     * A transaction-only decoder cannot observe missing external drive. */
    const uint32_t lcd_pins=(1u<<7)|(1u<<8)|(1u<<9)|(1u<<12);
    assert(!(mock.pa[2]&lcd_pins));
    assert((mock.pa[3]&lcd_pins)==lcd_pins);
    assert((mock.clock&~0x6ffu)==(before&~0x6ffu));
    assert((mock.clock&0x7ffu)==0x7c3u);
    static const uint8_t commands[]={1,0x11,0x36,0x3a,0xb2,0xb7,0xbb,0xc2,0xc3,0xc4,0xc6,0xd0,0xe0,0xe1,0x21,0x2a,0x2b,0x29,0x2c};
    assert(mock.n==sizeof commands && !memcmp(mock.commands,commands,sizeof commands));
    assert(board.milliseconds>=400 && mock.wait_reads==mock.operations*3);
    struct t3p_display display;
    const struct t3p_display_bus bus={.context=&board,.rectangle=t3p_bsp_rectangle};
    const struct t3p_display_config cfg={240,240,0,0,240,240};
    assert(!t3p_display_init(&display,&bus,&cfg));
    uint16_t column[240];
    for(unsigned x=0;x<240;x++) {
        for(unsigned y=0;y<240;y++)expected[y*240+x]=column[y]=(uint16_t)(x*83+y*97);
        assert(!t3p_display_column(&display,x,0,240,column));
    }
    assert(!t3p_display_flush(&display));
    assert(mock.draws==30 && !memcmp(mock.screen,expected,sizeof expected));
    /* Partial batch, high/low RGB bytes and viewport edge through real DBI operations. */
    uint16_t rgb[]={0xf800,0x07e0,0x001f};
    assert(!t3p_bsp_rectangle(&board,239,239,237,239,rgb,3));
    for(unsigned i=0;i<3;i++)assert(mock.screen[(237+i)*240+239]==rgb[i]);
    unsigned writes=mock.writes;
    assert(t3p_bsp_rectangle(&board,240,240,0,0,rgb,1)==-1);
    assert(t3p_bsp_rectangle(&board,0,7,0,239,rgb,1)==-1);
    assert(mock.writes==writes);
    mock.step=0;mock.tick=16;board.previous_tick=65520;
    board.milliseconds=0;board.fraction=0;
    assert(t3p_bsp_millis(&board)==5 && board.fraction==173);
    board.milliseconds=0xfffffffe;board.fraction=0;mock.tick+=24;
    assert(t3p_bsp_millis(&board)==2);
    reset();assert(!t3p_bsp_lcd_init(&board));
    mock.fail_at=mock.operations+1;mock.dbi[1]=1;
    assert(t3p_bsp_command(&board,0x29,0,0)==-2 && board.failed);
    writes=mock.writes;
    assert(t3p_bsp_command(&board,0x29,0,0)==-2 && mock.writes==writes);
    assert(mock.dbi[10]==0); /* Output disabled on timeout. */
    reset();mock.step=0;
    assert(t3p_bsp_delay(&board,1)==-2 && board.failed);
    puts("PASS: boot snapshot bounds, watchdog/timer/clock MMIO, stock LCD init order, synchronous DBI/pixel roundtrip, timer wrap, stale-PND and frozen-clock failures");
}
