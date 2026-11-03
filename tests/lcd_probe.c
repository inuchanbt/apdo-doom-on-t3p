#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void t3p_probe_draw(void *, int, int, int, int);
void t3p_probe_continue(void *, int, int, int, int);
volatile unsigned char probe_busy;
static unsigned short pixels[6408];
static void *expected_buffer;
static int bounds[4], continuing, stage;

void probe_stock_prepare(void)
{ assert(stage == 0); assert(pixels[4] == 0x5aa5); stage = 1; }
void probe_stock_wait(void)
{ assert(continuing && stage == 1); assert(pixels[4] == 0x5aa5); stage = 2; }

static void received(void *buffer, int xs, int xe, int ys, int ye, int cont)
{
    assert(cont == continuing);
    assert(stage == (cont ? 2 : 1));
    assert(probe_busy == cont);
    assert(buffer == expected_buffer);
    assert(xs == bounds[0] && xe == bounds[1]);
    assert(ys == bounds[2] && ye == bounds[3]);
    unsigned short *data = buffer;
    for (int y = ys; y <= ye; ++y)
        for (int x = xs; x <= xe; ++x) {
            /* Coordinate reference independent of the rectangle partition. */
#ifdef T3P_GEOMETRY_HOST
            unsigned short expected;
            if (x > 239 || y > 239) expected = 0xffe0;
            else if (x == 0 || x == 1 || x == 238 || x == 239 ||
                     y < 12 || y == 238 || y == 239) expected = 0xffff;
            else {
                static const unsigned short primary[] = {0xf800,0x07e0,0x001f};
                expected = primary[x/80];
            }
            assert(*data++ == expected);
#else
            static const unsigned short colors[] = {0xf81f, 0x07e0};
            assert(*data++ == colors[(x / 32) % 2]);
#endif
        }
    stage = 3;
}

void probe_stock_draw(void *b, int a, int c, int d, int e)
{ received(b, a, c, d, e, 0); }
void probe_stock_continue(void *b, int a, int c, int d, int e)
{ received(b, a, c, d, e, 1); }

static void call_rect(int cont, int xs, int xe, int ys, int ye)
{
    for (unsigned i = 0; i < 6408; ++i) pixels[i] = 0x5aa5;
    expected_buffer = pixels + 4;
    bounds[0] = xs; bounds[1] = xe; bounds[2] = ys; bounds[3] = ye;
    stage = 0; probe_busy = 0; continuing = cont;
    (cont ? t3p_probe_continue : t3p_probe_draw)(expected_buffer, xs, xe, ys, ye);
    assert(stage == 3);
    unsigned end = 4u + (unsigned)(xe-xs+1) * (unsigned)(ye-ys+1);
    for (unsigned i = 0; i < 4; ++i) assert(pixels[i] == 0x5aa5);
    for (unsigned i = end; i < 6408; ++i) assert(pixels[i] == 0x5aa5);
}

int main(void)
{
    unsigned rectangles = 0, rejected = 0;
    for (int cont = 0; cont != 2; ++cont) {
        /* Stock 20-line strips and smaller partial updates, including edges. */
        for (int y = 0; y < 320; y += 20) {
            call_rect(cont, 0, 319, y, y+19); ++rectangles;
        }
        for (int x = 0; x < 320; x += 7)
            for (int y = 0; y < 320; y += 19) {
                int xe = x + 14 < 320 ? x + 14 : 319;
                int ye = y + 6 < 320 ? y + 6 : 319;
                call_rect(cont, x, xe, y, ye); ++rectangles;
            }
        call_rect(cont, 319, 319, 319, 319); ++rectangles;
        const int bad[][4] = {
            {-1, 1, 0, 1}, {0, 320, 0, 0}, {3, 2, 0, 0},
            {0, 0, -1, 0}, {0, 0, 0, 320}, {0, 0, 2, 1},
            {0, 319, 0, 20}, {0, 319, 0, 319},
            {INT_MIN, INT_MAX, 0, 0}, {0, 0, INT_MIN, INT_MAX}
        };
        for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i) {
            memset(pixels, 0xa5, sizeof pixels);
            stage = 0; probe_busy = 0;
            (cont ? t3p_probe_continue : t3p_probe_draw)(pixels+4,
                    bad[i][0], bad[i][1], bad[i][2], bad[i][3]);
            assert(stage == 0 && probe_busy == 0);
            for (unsigned j = 0; j < 6408; ++j) assert(pixels[j] == 0xa5a5);
            ++rejected;
        }
        for (int bad_pointer = 0; bad_pointer < 2; ++bad_pointer) {
            stage = 0;
            void *p = bad_pointer ? (void *)((unsigned char *)pixels+1) : NULL;
            (cont ? t3p_probe_continue : t3p_probe_draw)(p, 0, 0, 0, 0);
            assert(stage == 0); ++rejected;
        }
    }
    printf("PASS: %u rectangles, %u rejected inputs, buffer guards and SPI-call order\n",
           rectangles, rejected);
    return 0;
}
