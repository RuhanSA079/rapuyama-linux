// neutralises rvct-only FRAME unwind directive macros
// built with -fno-unit-at-a-time so no CFI needs naked stubs
#undef __EH_FRAME_ADDRESS
#undef __EH_FRAME_PUSH2
#undef __EH_FRAME_SAVE1
#define __EH_FRAME_ADDRESS(r,o)
#define __EH_FRAME_PUSH2(a,b)
#define __EH_FRAME_SAVE1(r,o)