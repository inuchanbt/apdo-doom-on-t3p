/* Stock 3.0.3 drawing hook, not a standalone startup or a DOOM firmware.
 * The original SPI command, DMA and interrupt routines remain in the image.
 * Build only at the two version-specific function addresses in lcd_probe.ld.
 */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
extern void probe_stock_prepare(void);
extern void probe_stock_wait(void);
extern void probe_stock_draw(void *, int, int, int, int);
extern void probe_stock_continue(void *, int, int, int, int);
#ifdef T3P_PROBE_HOST
extern volatile u8 probe_busy;
#define BUSY probe_busy
#else
#define BUSY (*(volatile u8 *)0x00102ca8u)
#endif
#define PREPARE() probe_stock_prepare()
#define WAIT() probe_stock_wait()
#define DRAW(b,a,c,d,e) probe_stock_draw(b,a,c,d,e)
#define CONTINUE(b,a,c,d,e) probe_stock_continue(b,a,c,d,e)

/* Both dimensions and the total transfer must fit the stock 12,800 B buffer.
 * Validate before any subtraction/multiplication that could wrap.
 */
static __attribute__((always_inline)) inline int valid(
        void *buffer, int xs, int xe, int ys, int ye)
{
    return buffer && !((__UINTPTR_TYPE__)buffer & 1u) &&
        (u32)xs < 320u && (u32)xe < 320u && xs <= xe &&
        (u32)ys < 320u && (u32)ye < 320u && ys <= ye &&
        (u32)(xe-xs+1) * (u32)(ye-ys+1) <= 6400u;
}

static __attribute__((always_inline)) inline void pattern(
        u16 *buffer, int xs, int xe, int ys, int ye)
{
    for (int y = ys; y <= ye; ++y) {
        for (int x = xs; x <= xe; ++x) {
            /* 32-pixel magenta/green bands align across partial rectangles. */
            *buffer++ = (x & 32) ? 0x07e0u : 0xf81fu;
        }
    }
}

__attribute__((section(".probe_draw"), used))
void t3p_probe_draw(void *buffer, int xs, int xe, int ys, int ye)
{
    if (!valid(buffer, xs, xe, ys, ye)) return;
    PREPARE();
    pattern(buffer, xs, xe, ys, ye);
    DRAW(buffer, xs, xe, ys, ye);
}

__attribute__((section(".probe_continue"), used))
void t3p_probe_continue(void *buffer, int xs, int xe, int ys, int ye)
{
    if (!valid(buffer, xs, xe, ys, ye)) return;
    PREPARE();
    WAIT();
    BUSY = 1;
    pattern(buffer, xs, xe, ys, ye);
    CONTINUE(buffer, xs, xe, ys, ye);
}
