// neutralises rvct-only FRAME unwind directive macros (yuh)
#undef __EH_FRAME_ADDRESS
#undef __EH_FRAME_PUSH2
#undef __EH_FRAME_SAVE1
#define __EH_FRAME_ADDRESS(r,o)
#define __EH_FRAME_PUSH2(a,b)
#define __EH_FRAME_SAVE1(r,o)
// __declspec(naked) neutralised by -D__declspec(x)=
// restroe naked for the hand-written .cia assembly entry stubs
#undef __NAKED__
#undef ____ONLY_USE_NAKED_IN_CIA____
#define __NAKED__ __attribute__((naked))
#define ____ONLY_USE_NAKED_IN_CIA____ __attribute__((naked))