/* Synthetic guest surfaces: no game assets or GPU device. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nv2a_backend.h"

#define COLOR_AT 0x10000u
#define POS_AT   0x60000u
#define DIFF_AT  0x70000u
#define MEM_BYTES 0x80000u
static uint8_t s_mem[MEM_BYTES];

/* ---- what the executor expects the runtime to provide ------------------ */
ptrdiff_t xbox_GetMemoryOffset(void) { return (ptrdiff_t)s_mem; }
uint32_t g_xbox_image_lo, g_xbox_image_hi;
uint32_t g_xbox_stack_base, g_xbox_stack_size;
uint32_t xbox_ContiguousAllocatedBytes(void) { return 0; }
void xbox_FramebufferWindowSet(uint32_t va, uint32_t pitch) { (void)va; (void)pitch; }
void xbox_FramebufferWindowSetAA(uint32_t sy) { (void)sy; }
uint32_t xbox_Nv2aFenceWordVa(void) { return 0; }
int nv2a_pb_dma_walk(void) { return 1; }
void xbox_FramebufferWindowStart(void) {}
void xbox_FramebufferWindowFrameStats(uint32_t draws) { (void)draws; }
void xbox_FramebufferWindowPresent(uint32_t va, uint32_t pitch) { (void)va; (void)pitch; }
void xbox_Nv2aFrameCounterFlip(void) {}
/* The flip hold (#192, nv2a_flip_hold.c, linked in): no vblank moves here,
 * so the executor never holds a flip. */
int nv2a_pb_scan_in_call(void) { return 0; }
uint32_t xbox_VblankCount(void) { return 0; }
uint64_t xbox_HostNowNs(void) { return 0; }
uint32_t xbox_Nv2aFrameCountersOwned(void) { return 0; }
volatile uint32_t *xbox_Nv2aRegPtr(uint32_t offset) { (void)offset; return NULL; }


extern void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t param);
static void m(uint32_t method, uint32_t param) { nv2a_pb_exec_method(0,method,param); }
static void mf(uint32_t method,float f) { uint32_t u;memcpy(&u,&f,4);m(method,u); }
static int failures;
#define CHECK(c) do { if(!(c)){if(failures<8)fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);failures++;} } while(0)
static uint32_t get32(uint32_t at) {uint32_t v;memcpy(&v,s_mem+at,4);return v;}
static uint32_t pixel_index(uint32_t x,uint32_t y,uint32_t w,uint32_t h,int swizzled)
{
    uint32_t bit,index=0,out=0;
    if(!swizzled)return y*w+x;
    /* Independent rectangular Morton layout, not the production helper. */
    for(bit=0;(1u<<bit)<w || (1u<<bit)<h;bit++){
        if((1u<<bit)<w)index|=((x>>bit)&1u)<<out++;
        if((1u<<bit)<h)index|=((y>>bit)&1u)<<out++;
    }
    return index;
}
static void draw_quad(void)
{
    static const float pos[6][3]={{4,4,0},{12,4,0},{12,12,0},{4,4,0},{12,12,0},{4,12,0}};
    uint32_t i,color=0xFF4080C0u;
    for(i=0;i<6;i++){
        memcpy(s_mem+POS_AT+12*i,pos[i],12);
        memcpy(s_mem+DIFF_AT+4*i,&color,4);
    }
    m(0x1720,POS_AT);m(0x1760,2|(3<<4)|(12<<8));
    m(0x172C,DIFF_AT);m(0x176C,0|(4<<4)|(4<<8));
    m(0x17FC,5);m(0x1810,5u<<24);m(0x17FC,0);
}
static void surface(uint32_t format,uint32_t w,uint32_t h,uint32_t pitch,uint32_t x,uint32_t y,uint32_t at)
{
    m(0x0208,format);m(0x0200,(w<<16)|x);m(0x0204,(h<<16)|y);
    m(0x020C,pitch);m(0x0210,at);
}
static void check_capture(uint32_t w,uint32_t h,uint32_t format,int swizzled)
{
    uint32_t x,y,i,bytes=w*h*4;
    fprintf(stderr,"capture %ux%u format %08X\n",w,h,format);
    memset(s_mem+COLOR_AT-4,0xA5,bytes+8);
    surface(format,w,h,w*4,0,0,COLOR_AT);
    m(0x1D90,0x11223344);m(0x1D94,0xF0);
    for(i=0;i<w*h;i++)CHECK(get32(COLOR_AT+4*i)==0x11223344);
    draw_quad();
    for(y=0;y<h;y++)for(x=0;x<w;x++){
        uint32_t got=get32(COLOR_AT+4*pixel_index(x,y,w,h,swizzled));
        if(x>=4&&x<12&&y>=4&&y<12){
            CHECK((got&0xFF000000u)==0xFF000000u);
            CHECK(((got>>16)&255)>=61&&((got>>16)&255)<=67);
            CHECK(((got>>8)&255)>=125&&((got>>8)&255)<=131);
            CHECK((got&255)>=189&&(got&255)<=195);
        }else CHECK(got==0x11223344);
    }
    CHECK(get32(COLOR_AT-4)==0xA5A5A5A5u);
    CHECK(get32(COLOR_AT+bytes)==0xA5A5A5A5u);
}
static void excluded(uint32_t format,uint32_t w,uint32_t h,uint32_t pitch,uint32_t x,uint32_t y,uint32_t at)
{
    uint32_t i;
    fprintf(stderr,"excluded %ux%u format %08X target %08X\n",w,h,format,at);
    memset(s_mem+COLOR_AT,0xA5,0x40000);
    surface(format,w,h,pitch,x,y,at);
    m(0x1D90,0x11223344);m(0x1D94,0xF0);draw_quad();
    for(i=0;i<0x40000;i++)CHECK(s_mem[COLOR_AT+i]==0xA5);
}
int main(void)
{
    static const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    uint32_t i;
#ifdef _WIN32
    _putenv("RECOMP_PB_D3D11=1");_putenv("RECOMP_PB_SOFT_RASTER=");
#else
    setenv("RECOMP_PB_D3D11","1",1);unsetenv("RECOMP_PB_SOFT_RASTER");
#endif
    for(i=0;i<16;i++)mf(0x0680+4*i,identity[i]);
    m(0x1E94,0);m(0x0358,0x01010101);m(0x030C,0);m(0x035C,0);
    check_capture(32,32,0x108,0);
    check_capture(32,32,0x05050228,1);
    check_capture(128,128,0x07070228,1); /* exact 64 KiB boundary */
    check_capture(128,32,0x05070228,1); /* rectangular Morton addressing */
    excluded(0x030B0228,2048,8,8192,0,0,COLOR_AT); /* raster width bound */
    excluded(0x0B030228,8,2048,32,0,0,COLOR_AT); /* raster height bound */
    excluded(0x08080228,256,256,1024,0,0,COLOR_AT); /* full-size / over budget */
    excluded(0x07070228,32,32,128,0,0,COLOR_AT); /* clip inside larger allocation */
    excluded(0x05050228,32,32,512,0,0,COLOR_AT); /* not a complete tight surface */
    excluded(0x05050228,32,32,128,1,0,COLOR_AT);
    excluded(0x05050228,32,32,128,0,1,COLOR_AT);
    excluded(0x05050228,0,32,128,0,0,COLOR_AT);
    excluded(0x05050225,32,32,128,0,0,COLOR_AT); /* different channel layout */
    excluded(0x05050028,32,32,128,0,0,COLOR_AT); /* unknown surface type */
    /* The target starts outside the image but its full span overlaps it.
     * Starting inside would resolve as a physical mirror in the real runtime. */
    g_xbox_image_lo=COLOR_AT+0x800;g_xbox_image_hi=COLOR_AT+0x1000;
    excluded(0x05050228,32,32,128,0,0,COLOR_AT); /* loaded image protection */
    g_xbox_image_lo=g_xbox_image_hi=0;
    m(0x032C,1);
    excluded(0x05050228,32,32,128,0,0,COLOR_AT); /* software stencil unsupported */
    m(0x032C,0);
    excluded(0x05050228,32,32,128,0,0,0); /* null target */
    excluded(0x05050228,32,32,128,0,0,0xFD000000u); /* invalid guest span */
    printf("small capture surfaces: %s\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
