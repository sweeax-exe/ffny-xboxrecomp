#include "input_host.h"
#include "input_trace.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static HANDLE paused, resume_poll;
static volatile LONG pause_stage;
/* Deterministically model OS preemption between acquisition and publication.
 * Only the test sink blocks; the production trace sink is nonblocking. */
static void trace_pause(const char *stage,int slot,const void *data,unsigned n,uint64_t a,uint64_t b)
{
    (void)data;(void)n;(void)a;(void)b;
    if ((!strcmp(stage,"host_raw") && slot==0 && InterlockedCompareExchange(&pause_stage,2,1)==1) ||
        (!strcmp(stage,"host_poll_end") && InterlockedCompareExchange(&pause_stage,3,2)==2)) {
        SetEvent(paused); CHECK(WaitForSingleObject(resume_poll,2000)==WAIT_OBJECT_0);
    }
}

/* The other player's agreed pad in an online test: START and Y-shoulder bits, packet 7. */
static DWORD remote_pad(int slot,XBOX_INPUT_STATE *s)
{
    (void)slot;
    memset(s,0,sizeof *s);
    s->dwPacketNumber=7;
    s->Gamepad.wButtons=0x1010;
    return 0;
}

static void set_x(int slot,int pressed)
{
    uint8_t held[INPUT_CONTROL_COUNT] = {0};
    DWORD deadline = GetTickCount()+500;
    InputRaw raw;
    held[INPUT_X] = pressed != 0;
    xbox_HostInputScriptVirtual(slot,held);
    do {
        if (xbox_HostInputRawPad(slot,&raw) && !!raw.src[INPUT_SRC_WEST]==!!pressed) return;
        Sleep(1);
    } while ((int32_t)(deadline-GetTickCount())>0);
    CHECK(0);
}

int main(void)
{
    InputHostConfig cfg;
    XBOX_INPUT_STATE state;
    uint64_t token, first;
    unsigned i;
    CHECK(SDL_Init(SDL_INIT_GAMEPAD));
    xbox_HostInputDefaults(&cfg);
    cfg.no_pads=1;cfg.virtual_pads=2;cfg.keyboard=INPUT_HOST_KEYBOARD_OFF;
    cfg.rumble_on_connect=0;
    xbox_HostInputEnableConsumedEdges(1);
    CHECK(xbox_HostInputStart(&cfg)==2);
    CHECK(xbox_HostInputPeekState(0,&state,&token)==0);
    CHECK(!state.Gamepad.bAnalogButtons[XBOX_BUTTON_LTRIGGER]);
    CHECK(!state.Gamepad.bAnalogButtons[XBOX_BUTTON_RTRIGGER]);
    set_x(0,1);set_x(0,0);set_x(0,1);set_x(0,0);
    CHECK(xbox_HostInputPeekState(0,&state,&first)==0 && first);
    CHECK(xbox_HostInputCurrentState(0,&state)==0 && !state.Gamepad.bAnalogButtons[XBOX_BUTTON_X]);
    CHECK(xbox_HostInputPeekState(0,&state,&token)==0 && token==first && state.Gamepad.bAnalogButtons[XBOX_BUTTON_X]);
    /* A read or failed/uncommitted transfer alone cannot consume the press. */
    xbox_HostInputConsumePacket(0,1,&state.Gamepad,18);
    CHECK(xbox_HostInputPeekState(0,&state,&token)==0 && token==first);
    for(i=0;i<4;i++) {
        CHECK(xbox_HostInputPeekState(0,&state,&token)==0 && token);
        CHECK(!!state.Gamepad.bAnalogButtons[XBOX_BUTTON_X]==!(i&1));
        xbox_HostInputReportDelivered(0,token);
        xbox_HostInputConsumePacket(0,i+2,&state.Gamepad,18);
    }
    CHECK(xbox_HostInputPeekState(0,&state,&token)==0 && !token && !state.Gamepad.bAnalogButtons[XBOX_BUTTON_X]);
    set_x(1,1);
    CHECK(xbox_HostInputPeekState(0,&state,&token)==0 && !state.Gamepad.bAnalogButtons[XBOX_BUTTON_X]);
    CHECK(xbox_HostInputPeekState(1,&state,&token)==0 && state.Gamepad.bAnalogButtons[XBOX_BUTTON_X]);
    xbox_HostInputSetUiActive(1);
    CHECK(xbox_HostInputPeekState(1,&state,&token)==0 && !token && !state.Gamepad.bAnalogButtons[XBOX_BUTTON_X]);
    xbox_HostInputSetUiActive(0);
    Sleep(8);
    xbox_HostInputFocus(0);
    CHECK(xbox_HostInputPeekState(1,&state,&token)==0 && !token && !state.Gamepad.bAnalogButtons[XBOX_BUTTON_X]);
    Sleep(8);
    xbox_HostInputFocus(1);
    set_x(1,0);
    xbox_HostInputCancelEdges(1);
    CHECK(xbox_HostInputPeekState(1,&state,&token)==0 && !token && !state.Gamepad.bAnalogButtons[XBOX_BUTTON_X]);
    paused=CreateEvent(NULL,FALSE,FALSE,NULL);resume_poll=CreateEvent(NULL,FALSE,FALSE,NULL);
    CHECK(paused && resume_poll);
    xbox_InputTraceSetSink(trace_pause);
    InterlockedExchange(&pause_stage,1);
    { uint8_t held[INPUT_CONTROL_COUNT]={0}; held[INPUT_X]=1; xbox_HostInputScriptVirtual(0,held); }
    CHECK(WaitForSingleObject(paused,2000)==WAIT_OBJECT_0);
    xbox_HostInputSetUiActive(1);xbox_HostInputSetUiActive(0);
    SetEvent(resume_poll);
    CHECK(WaitForSingleObject(paused,2000)==WAIT_OBJECT_0); /* paused after stale sample commit */
    CHECK(xbox_HostInputPeekState(0,&state,&token)==0 && !token && !state.Gamepad.bAnalogButtons[XBOX_BUTTON_X]);
    SetEvent(resume_poll);
    xbox_InputTraceSetSink(NULL);
    set_x(0,0);xbox_HostInputCancelEdges(0);
    cfg.ignore_focus=1;xbox_HostInputConfigure(&cfg);
    set_x(0,1);set_x(0,0);
    xbox_HostInputFocus(0);Sleep(8);
    CHECK(xbox_HostInputPeekState(0,&state,&token)==0 && token && state.Gamepad.bAnalogButtons[XBOX_BUTTON_X]);
    xbox_HostInputFocus(1);xbox_HostInputCancelEdges(0);
    set_x(0,0);xbox_HostInputCancelEdges(0);
    { uint64_t old=xbox_HostInputEdgeGeneration(0), fresh;
      set_x(0,1);
      CHECK(xbox_HostInputPeekState(0,&state,&token)==0 && token);
      xbox_HostInputCancelEdges(0);set_x(0,0);set_x(0,1);
      CHECK(xbox_HostInputPeekState(0,&state,&token)==0 && token);
      fresh=xbox_HostInputEdgeGeneration(0);CHECK(fresh && fresh!=old);
      xbox_HostInputReportDelivered(0,token);
      xbox_HostInputConsumeGeneration(0,100,&state.Gamepad,18,old);
      CHECK(xbox_HostInputPeekState(0,&state,&first)==0 && first==token);
      xbox_HostInputConsumeGeneration(0,101,&state.Gamepad,18,fresh);
      CHECK(xbox_HostInputPeekState(0,&state,&token)==0 && !token);
    }
    /* Online play: no retained transitions while it is on (the legacy USB path decides), this PC's
     * devices on its port and the agreed pad on the other, START held back in a lockstep fight. */
    set_x(0,0);xbox_HostInputCancelEdges(0);
    CHECK(xbox_HostInputConsumedEdges());
    xbox_HostInputSetNetplayMulti(1,0,2u,remote_pad);
    CHECK(!xbox_HostInputConsumedEdges());
    set_x(0,1);set_x(0,0);
    CHECK(xbox_HostInputPeekState(0,&state,&token)==0 && !token && !state.Gamepad.bAnalogButtons[XBOX_BUTTON_X]);
    set_x(0,1);
    CHECK(xbox_HostInputPeekState(0,&state,&token)==0 && !token && state.Gamepad.bAnalogButtons[XBOX_BUTTON_X]);
    CHECK(xbox_HostInputGetState(1,&state)==0 && state.dwPacketNumber==7 && (state.Gamepad.wButtons&0x1010)==0x1010);
    xbox_HostInputSetNetplayFight(0x0010);
    CHECK(xbox_HostInputPeekState(1,&state,&token)==0 && (state.Gamepad.wButtons&0x1010)==0x1000);
    CHECK(xbox_HostInputCurrentState(2,&state)==0 && !state.Gamepad.wButtons);
    xbox_HostInputSetNetplayFight(0);
    CHECK(xbox_HostInputPeekState(2,&state,&token)!=0);
    /* A spectator of a match of four: no slot reads this PC's devices, every slot the agreed pad;
     * GetLocalState still reads them (a spectator's START leaves the match) */
    xbox_HostInputSetNetplayMulti(1,-1,15u,remote_pad);
    CHECK(!xbox_HostInputConsumedEdges());
    for (i=0;i<4;i++) {
        CHECK(xbox_HostInputGetState((int)i,&state)==0 && state.dwPacketNumber==7);
        CHECK(!state.Gamepad.bAnalogButtons[XBOX_BUTTON_X]);
    }
    CHECK(xbox_HostInputGetLocalState(&state)==0 && state.Gamepad.bAnalogButtons[XBOX_BUTTON_X]);
    CHECK(xbox_HostInputRumble(0,1000,1000)==0);
    xbox_HostInputSetNetplayMulti(1,-2,15u,remote_pad);     /* refused: netplay off */
    CHECK(xbox_HostInputConsumedEdges());
    xbox_HostInputSetNetplay(0,0,0,NULL);
    CHECK(xbox_HostInputConsumedEdges());
    set_x(0,0);
    CHECK(xbox_HostInputPeekState(0,&state,&token)==0 && token && !state.Gamepad.bAnalogButtons[XBOX_BUTTON_X]);
    xbox_HostInputReportDelivered(0,token);
    xbox_HostInputConsumePacket(0,200,&state.Gamepad,18);
    set_x(0,1);set_x(0,0);
    CHECK(xbox_HostInputPeekState(0,&state,&token)==0 && token && state.Gamepad.bAnalogButtons[XBOX_BUTTON_X]);
    xbox_HostInputCancelEdges(0);
    { uint64_t before=xbox_HostInputEdgeGeneration(0);
      xbox_HostInputStop();
      CHECK(xbox_HostInputStart(&cfg)==2);
      CHECK(xbox_HostInputEdgeGeneration(0)>before);
      xbox_HostInputStop();
    }
    CloseHandle(paused);CloseHandle(resume_poll);
    SDL_Quit();
    puts("input_host_edges: all checks passed");
    return 0;
}
