// READ-ONLY periodic watcher of the modem/ISA hardware register block (phys
// 0x20040000..0x20040400) in LIVE Symbian, to catch the periodic "APE awake"
// refresh that keeps the modem's ~200s supervision from resetting the chip.
// WHY: securetrace proved a bare-metal spin (IRQs masked, touches nothing) still
// dies ~200s => Symbian refreshes an "APE alive/awake" signal within ~200s that
// decays without it. It is NOT in the modem control block (e6mtrc: static 5min),
// NOT CBUS/PMIC (e6cbus: 0 writes), NOT a fast modem IRQ (klog: only timer 242).
// The remaining place is this modem/ISA register block, written by the ASIC
// vtable methods (runtime, not in the static sources). We snapshot every word and
// log the ones that CHANGE with a timestamp: any register ticking on a regular
// cadence over a few minutes = the keepalive we must replicate from Linux.
// reads only (ReadPhys = Kern maps + reads the periph word), ~5 min, no writes.
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\modwatch.log");

// a curated set of modem/ISA registers the FIRMWARE itself reads (from the
// karap/ecust/power literal pools) = known safe to read, no read-to-clear guess.
// watch these for a periodic tick = the "APE awake" keepalive.
static const TUint32 KRegs[] = {
	0x20040180, 0x20040184, 0x20040188, 0x2004018c,   // modem power (karap+power)
	0x20040280, 0x20040284, 0x20040288, 0x2004028c,   // modem/ISA status (FUN_800b828c)
	0x20040300, 0x20040384, 0x20040388, 0x20040400,   // ctrl + reset-reason
	0x20040580, 0x20040584, 0x20040780, 0x20040784,   // ecust iface
};
const TInt KN = sizeof(KRegs)/sizeof(KRegs[0]);
const TInt KMs = 200; // poll period
const TInt KSecs = 300; // total watch time

static RFile gLog;
static void L(const TDesC8& m){ gLog.Write(m); gLog.Flush(); }

GLDEF_C TInt E32Main(){
	RFs fs;
	if(fs.Connect()!=KErrNone){return KErrGeneral;}
	if(gLog.Replace(fs, KLog, EFileWrite)!=KErrNone){fs.Close(); return KErrGeneral;}
	L(_L8("modwatch: read-only modem/ISA keepalive watcher (uncached Peek)\n"));

	User::FreeLogicalDevice(KRunBlobName);
	TInt rLoad=User::LoadLogicalDevice(KRunBlobLdd);
	TBuf8<96> b; b.Format(_L8("Load=%d\n"), rLoad); L(b);
	if(rLoad!=KErrNone && rLoad!=KErrAlreadyExists){gLog.Close(); fs.Close(); return rLoad;}

	RRunBlob dev;
	if(dev.Open()!=KErrNone){gLog.Close(); fs.Close(); return KErrGeneral;}

	// prime the baseline via uncached Peek, verify it works + print the picture
	static TUint32 prev[64];
	TBool ok=EFalse;
	for(TInt i=0;i<KN;++i){
		TUint32 v=0; TInt r=dev.Peek(KRegs[i], &v);
		if(r>=0){ ok=ETrue; } prev[i]=v;
		b.Format(_L8("  base %08x = %08x (r=%d)\n"), KRegs[i], v, r); L(b);
	}
	if(!ok){ L(_L8("Peek failed on all regs. abort.\n")); dev.Close(); gLog.Close(); fs.Close(); return KErrNone; }
	L(_L8("--- watching for changes ---\n"));

	TInt iters=(KSecs*1000)/KMs;
	for(TInt t=0; t<iters; ++t){
		User::After(KMs*1000);
		TInt ms=t*KMs;
		for(TInt i=0;i<KN;++i){
			TUint32 v=0;
			if(dev.Peek(KRegs[i], &v)<0){ continue; }
			if(v!=prev[i]){
				b.Format(_L8("[%6d ms] %08x: %08x -> %08x\n"), ms, KRegs[i], prev[i], v);
				L(b);
				prev[i]=v;
			}
		}
	}
	L(_L8("done. any register changing on a regular cadence = the keepalive. send E:\\modwatch.log\n"));

	dev.Close(); gLog.Close(); fs.Close();
	return KErrNone;
}