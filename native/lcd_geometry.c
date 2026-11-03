/* Coordinate/color experiment on the proven stock3.0.3 draw hooks.
 * Source and output are separate from the successful stripe probe.
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

__attribute__((section(".probe_validate"), noinline, used))
int t3p_geometry_valid(void *buffer, int xs, int xe, int ys, int ye)
{
    return buffer && !((__UINTPTR_TYPE__)buffer & 1u) &&
        (u32)xs < 320u && (u32)xe < 320u && xs <= xe &&
        (u32)ys < 320u && (u32)ye < 320u && ys <= ye &&
        (u32)(xe-xs+1) * (u32)(ye-ys+1) <= 6400u;
}

__attribute__((section(".probe_pattern"), noinline, used))
void t3p_geometry_paint(u16 *buffer, int xs, int xe, int ys, int ye)
{
    for (int y = ys; y <= ye; ++y) {
        for (int x = xs; x <= xe; ++x) {
            u16 color = x < 80 ? 0xf800 : x < 160 ? 0x07e0 : 0x001f;
            if (x >= 240 || y >= 240) color = 0xffe0;
            else if (x < 2 || y < 12 || x >= 238 || y >= 238) color = 0xffff;
            *buffer++ = color;
        }
    }
}

__attribute__((section(".probe_draw"), used))
void t3p_probe_draw(void *buffer, int xs, int xe, int ys, int ye)
{
    if (!t3p_geometry_valid(buffer, xs, xe, ys, ye)) return;
    probe_stock_prepare();
    t3p_geometry_paint(buffer, xs, xe, ys, ye);
    probe_stock_draw(buffer, xs, xe, ys, ye);
}

__attribute__((section(".probe_continue"), used))
void t3p_probe_continue(void *buffer, int xs, int xe, int ys, int ye)
{
    if (!t3p_geometry_valid(buffer, xs, xe, ys, ye)) return;
    probe_stock_prepare();
    probe_stock_wait();
    BUSY = 1;
    t3p_geometry_paint(buffer, xs, xe, ys, ye);
    probe_stock_continue(buffer, xs, xe, ys, ye);
}
