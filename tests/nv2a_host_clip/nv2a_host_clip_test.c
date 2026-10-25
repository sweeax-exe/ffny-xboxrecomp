/* Submit synthetic triangles through the real executor's host forwarding path.
 * A translator stub checks its clip at draw time. Console AA changes physical
 * software surfaces, while host clipping stays in logical pixels. Cover AA
 * off/2x1/2x2, centred 2D placement and unchanged 3D placement at 4:3/16:9.
 * Guest memory is a synthetic buffer; no game data is used. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nv2a_backend.h"
#include "../../src/nv2a/nv2a_pgraph_d3d11.h"

#define MEM_BYTES 0x10000u
static uint8_t s_mem[MEM_BYTES];

/* ---- what the executor expects the runtime to provide ------------------ */
ptrdiff_t xbox_GetMemoryOffset(void) { return (ptrdiff_t)s_mem; }
uint32_t g_xbox_image_lo, g_xbox_image_hi;
uint32_t g_xbox_stack_base, g_xbox_stack_size;   /* #189 made the stack placement runtime state */
uint32_t xbox_ContiguousAllocatedBytes(void) { return 0; }
void xbox_FramebufferWindowSet(uint32_t va, uint32_t pitch) { (void)va; (void)pitch; }
void xbox_FramebufferWindowSetAA(uint32_t sy) { (void)sy; }
static uint32_t s_fence_va;                           /* the mirrored fence word, if any */
uint32_t xbox_Nv2aFenceWordVa(void) { return s_fence_va; }
int nv2a_pb_dma_walk(void) { return 1; }             /* RECOMP_PB_WALK=dma: 0x0310 is latched */
void xbox_FramebufferWindowStart(void) {}
void xbox_FramebufferWindowFrameStats(uint32_t draws) { (void)draws; }
void xbox_FramebufferWindowPresent(uint32_t va, uint32_t pitch) { (void)va; (void)pitch; }
void xbox_Nv2aFrameCounterFlip(void) {}
/* The flip hold (nv2a_flip_hold.c, linked in). No vblank moves here, so the
 * executor never holds a flip; the walk is never inside a pushbuffer CALL. */
int nv2a_pb_scan_in_call(void) { return 0; }
uint32_t xbox_VblankCount(void) { return 0; }
uint64_t xbox_HostNowNs(void) { return 0; }
uint32_t xbox_Nv2aFrameCountersOwned(void) { return 0; }

static volatile uint32_t s_pgraph_400b10;
volatile uint32_t *xbox_Nv2aRegPtr(uint32_t offset)
{
    return offset == 0x400B10u ? &s_pgraph_400b10 : NULL;
}

/* ---- driving the executor ---------------------------------------------- */
extern void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t param);
static void m(uint32_t method, uint32_t param) { nv2a_pb_exec_method(0, method, param); }
static void put32(uint32_t at, uint32_t v) { memcpy(s_mem + at, &v, 4); }
static void putf(uint32_t at, float f) { memcpy(s_mem + at, &f, 4); }

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); s_fail = 1; } } while (0)

extern void nv2a_ws_configure(float k, float reach);
static uint32_t clip[4], expected[4], draws;
void pgraph_d3d11_set_surface_clip(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{ clip[0] = x; clip[1] = y; clip[2] = w; clip[3] = h; }
void pgraph_d3d11_draw_ready(uint32_t mode, const PgraphReadyVertex *v, uint32_t n)
{
    uint32_t i; (void)mode; (void)v;
    CHECK(n == 3); ++draws;
    for (i = 0; i < 4; i++)
        CHECK(clip[i] == expected[i]);
}
int main(void)
{
    uint32_t aa, wide, spatial, i;
#ifdef _WIN32
    _putenv_s("RECOMP_PB_D3D11", "1");
#else
    setenv("RECOMP_PB_D3D11", "1", 1);
#endif
    /* Direct host forwarding uses pretransformed, logical pixel positions. */
    m(0x1E94, 0);
    putf(0x2000, 150); putf(0x2004, 100); putf(0x2008, 0);
    putf(0x200C, 200); putf(0x2010, 100); putf(0x2014, 0);
    putf(0x2018, 170); putf(0x201C, 150); putf(0x2020, 0);
    for (i = 0; i < 3; i++)
        put32(0x3000 + 4 * i, 0xFFFFFFFFu);
    m(0x1720, 0x2000); m(0x1760, 2 | (3 << 4) | (12 << 8));
    m(0x172C, 0x3000); m(0x176C, 0 | (4 << 4) | (4 << 8));
    m(0x020C, 5120); m(0x0210, 0x8000);
    for (aa = 0; aa <= 2; aa++) for (wide = 0; wide < 2; wide++) for (spatial = 0; spatial < 2; spatial++) {
        putf(0x2014, spatial ? 0.5f : 0.0f);
        m(0x0208, 0x128 | (aa << 12));
        m(0x0200, 49u | (270u << 16)); m(0x0204, 35u | (369u << 16));
        nv2a_ws_configure(wide ? .75f : 1.0f, 1.0f);
        expected[0] = wide && !spatial ? 117 : 49; expected[1] = 35;
        expected[2] = wide && !spatial ? 203 : 270; expected[3] = 369;
        m(0x17FC, 5); m(0x1810, 2u << 24); m(0x17FC, 0);
    }
    CHECK(draws == 12);
    return s_fail;
}
