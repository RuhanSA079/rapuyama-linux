// shared stuff between the runblob loader (kernel) and its user client
#ifndef RUNBLOB_H
#define RUNBLOB_H

#include <e32cmn.h>
#ifndef __KERNEL_MODE__
#include <e32std.h>
#endif

_LIT(KRunBlobName, "runblob");

// returned buffer = up to 32 words the blob wrote (sig/sum + cp15 cpu id)
const TInt KRunBlobOutBytes=32*4;

// params for EControlFbFill: fill iBytes of phys ram at iPhys with iColor
// (to find the framebuffer by watching the screen)
struct TFbFill{
	TUint32 iPhys;
	TUint32 iBytes;
	TUint32 iColor;
};

// timer probe output: [0]magic [1..4] before (tick,fc,mst cnt,mst reload)
// [5..36] 16x (fc, mst cnt) 100us apart [37..40] after 1 s sleep (tick,fc,mst cnt,mst reload)
const TInt KTimerProbeWords=41;
const TUint32 KTimerProbeMagic=0x7e570001u;
const TInt KRegProbeMax=64;

// booter params
// copy iLen bytes from user iSrc into boot chunk + iOffset
struct TLoadChunk{
	TUint32 iOffset;
	TUint32 iSrc;
	TUint32 iLen;
};
// offsets inside the boot chunk + boot args
struct TBootParams{
	TUint32 iZImageOff; // zImage entry offset (TEXT_OFFSET, 0x8000)
	TUint32 iDtbOff; // dtb offset in the chunk
	TUint32 iMach; // machine type (r1, ~0 for dt)
	TUint32 iWdog; // watchdog reg phys (0 = none)
};

class RRunBlob : public RBusLogicalChannel{
	public:
		enum TControl{EControlRunBlob, EControlIdentityTest, EControlTrampTest,
			EControlFullTramp, EControlFbFill, EControlTakeover,
			EControlAllocBoot, EControlLoadChunk, EControlBoot,
			EControlTimedDiff, EControlTimerProbe, EControlRegProbe, EControlRegWrite, EControlKickLoop, EControlReadPhys};
	#ifndef __KERNEL_MODE__
		TInt Open(){return DoCreate(KRunBlobName, TVersion(1,0,0), KNullUnit, NULL, NULL);}
		TInt Run(TDes8& aBuf){return DoControl(EControlRunBlob, (TAny*)&aBuf);}

		// builds an armv6 identity section mapping (virt==phys) in the page table and
		// checks it, WITHOUT turning the mmu off. aBuf gets 8 words:
		// [0]=status(0 ok, 1 va already mapped so aborted, <0 err) [1]=phys [2]=L1 idx
		// [3]=descriptor written [4]=old descriptor
		// [5]=read via identity va (expect DEADBEEF) [6]=read-back reverse [7]=0
		TInt IdentityTest(TDes8& aBuf){return DoControl(EControlIdentityTest, (TAny*)&aBuf);}

		// trampoline stage 2: identity-map an executable page, jump to a stub that
		// turns off irq+mmu+caches, writes a marker to PHYS ram, turns the mmu back
		// on and returns. aBuf gets 8 words:
		// [0]=status(0 ok, <0 err) [1]=phys [2]=marker written [3]=marker read back
		// [4]=descriptor [5]=0 [6]=0 [7]=0
		TInt TrampTest(TDes8& aBuf){return DoControl(EControlTrampTest, (TAny*)&aBuf);}

		// FULL trampoline (but reversible): load a fake "kernel" at base+0x8000,
		// identity-map it, kill the mmu, hand off with r0=0/r1=machtype/r2=dtb, the
		// payload echoes those regs back. aBuf gets 8 words:
		// [0]=status [1]=phys [2]=echo r0(=0) [3]=echo r1(machtype) [4]=echo r2(dtb)
		// [5]=ran signature(=5a000000) [6]=0 [7]=0
		TInt FullTramp(TDes8& aBuf){return DoControl(EControlFullTramp, (TAny*)&aBuf);}

		// fill aBytes of phys ram at aPhys with aColor (to find the live
		// framebuffer, watch the screen for the flash)
		TInt FbFill(TUint32 aPhys, TUint32 aBytes, TUint32 aColor){
			TFbFill f;
			f.iPhys=aPhys; f.iBytes=aBytes; f.iColor=aColor;
			return DoControl(EControlFbFill, (TAny*)&f);
		}

		// ONE-WAY: kills symbian (mmu off) and drives the display from bare metal.
		// never returns, battery pull to recover. no buffer
		TInt Takeover(){return DoControl(EControlTakeover, NULL);}

		// ONE-WAY watchdog test: kills symbian (mmu off) and kicks both watchdogs
		// forever from bare metal. never returns
		TInt KickLoop(){return DoControl(EControlKickLoop, NULL);}

		// booter: alloc a contiguous boot chunk, stream zImage+dtb in, boot
		TInt AllocBoot(TUint32 aSize, TUint32& aPhysOut){
			return DoControl(EControlAllocBoot, (TAny*)&aSize, (TAny*)&aPhysOut);
		}
		TInt LoadChunk(TUint32 aOffset, const TUint8* aSrc, TUint32 aLen){
			TLoadChunk c;
			c.iOffset=aOffset; c.iSrc=(TUint32)aSrc; c.iLen=aLen;
			return DoControl(EControlLoadChunk, (TAny*)&c);
		}
		// never returns if it works
		TInt Boot(TUint32 aZImageOff, TUint32 aDtbOff, TUint32 aMach, TUint32 aWdog){
			TBootParams b;
			b.iZImageOff=aZImageOff; b.iDtbOff=aDtbOff;
			b.iMach=aMach; b.iWdog=aWdog;
			return DoControl(EControlBoot, (TAny*)&b);
		}

		// reads aCount words at virt aVirt, spins aDelay loops, reads again. aBuf gets
		// A then B (2*aCount words). regs that change A->B are free-running
		// counters -> system timer and watchdog. (safe, 0x30000000 reads fine)
		TInt TimedDiff(TUint32 aVirt, TUint32 aCount, TUint32 aDelay, TDes8& aBuf){
			TFbFill p;
			p.iPhys=aVirt; p.iBytes=aCount; p.iColor=aDelay;
			return DoControl(EControlTimedDiff, (TAny*)&p, (TAny*)&aBuf);
		}

		// checks the timers reversed from the firmware (docs/driver-re.md), only
		// touches regs symbian itself uses: fast counter 0x20033350 (latch 0x354)
		// and the ms timer 0x200337a4/0x7a8. aBuf gets KTimerProbeWords words
		TInt TimerProbe(TDes8& aBuf){return DoControl(EControlTimerProbe, (TAny*)&aBuf);}

		// reads one word from each virt address in aAddrs (KRegProbeMax max, only
		// the c6 peripheral window, others give 0xdeadc6c6). aBuf gets one word each
		TInt RegProbe(const TDesC8& aAddrs, TDes8& aBuf){return DoControl(EControlRegProbe, (TAny*)&aAddrs, (TAny*)&aBuf);}

		// writes aVal to one virt address in the c6 peripheral window (KErrArgument otherwise)
		TInt RegWrite(TUint32 aVirt, TUint32 aVal){return DoControl(EControlRegWrite, (TAny*)aVirt, (TAny*)aVal);}

		// maps physical RAM at aPhys (page aligned) and copies aBuf.MaxLength() bytes into aBuf
		TInt ReadPhys(TUint32 aPhys, TDes8& aBuf){return DoControl(EControlReadPhys, (TAny*)aPhys, (TAny*)&aBuf);}
	#endif
};

#endif // RUNBLOB_H