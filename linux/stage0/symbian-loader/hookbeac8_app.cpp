// patches the ASIC modem-send fn FUN_800beac8 (0x800beac8) via EControlHookBeac8
// so EVERY message Symbian sends the modem over the ASIC byte-FIFO (phys
// 0x40100000) is logged {descptr, channel, word0}, then polls the ring for
// ~4 min in LIVE Symbian and writes each new send with a timestamp to
// E:\beac8.log, then removes the patch. goal: GROUND TRUTH for the modem link -
// does Symbian actually use this FIFO path, on which channel, how often, and
// what message length (word0 & 0x0fffffff)? settles FIFO vs the 0x20040180
// mailbox as the real supervision channel, so the linux driver stops guessing.
// CRITICAL: always HookBeac8Remove before exit (restores live kernel rom); never
// FreeLogicalDevice while patched. a power cycle also clears the patch.
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\beac8.log");
const TInt KPolls=480;   // ~4 min at 500 ms
const TInt KSlots=32;

static RFile gLog;
static void L(const TDesC8& m){ gLog.Write(m); gLog.Flush(); }

GLDEF_C TInt E32Main(){
	RFs fs;
	if(fs.Connect()!=KErrNone){return KErrGeneral;}
	if(gLog.Replace(fs, KLog, EFileWrite)!=KErrNone){fs.Close(); return KErrGeneral;}
	L(_L8("beac8: logging every modem TX send via rom hook on 0x800beac8\n"));

	User::FreeLogicalDevice(KRunBlobName);
	TInt rLoad=User::LoadLogicalDevice(KRunBlobLdd);
	TBuf8<160> b; b.Format(_L8("Load=%d\n"), rLoad); L(b);
	if(rLoad!=KErrNone && rLoad!=KErrAlreadyExists){gLog.Close(); fs.Close(); return rLoad;}
	RRunBlob dev;
	if(dev.Open()!=KErrNone){gLog.Close(); fs.Close(); return KErrGeneral;}

	TInt rI=dev.HookBeac8();
	b.Format(_L8("HookBeac8=%d (patched; logging. 0=ok, -21=rom not writable)\n"), rI); L(b);
	if(rI==KErrNone){
		TUint32 ring[0x400];
		TUint32 seen=0, t0=User::TickCount();
		for(TInt poll=0; poll<KPolls; ++poll){
			TPtr8 rp((TUint8*)ring, 0, sizeof(ring));
			if(dev.HookBeac8Read(rp)==KErrNone){
				TUint32 count=ring[0];
				TUint32 ms=(User::TickCount()-t0)*1000/64;
				while(seen<count){
					TUint32 s=seen&(KSlots-1);
					TUint32 descptr=ring[s*4+1], chan=ring[s*4+2], word0=ring[s*4+3];
					b.Format(_L8("%u.%03us  #%u chan=%u descptr=0x%08x word0=0x%08x len=%u\n"),
						ms/1000, ms%1000, seen, chan, descptr, word0, word0&0x0fffffff);
					L(b);
					seen++;
				}
			}
			User::After(500000); // 0.5 s
		}
		b.Format(_L8("captured %u sends\n"), seen); L(b);
	}

	TInt rR=dev.HookBeac8Remove();
	b.Format(_L8("HookBeac8Remove=%d\n"), rR); L(b);
	dev.Close();
	gLog.Close(); fs.Close();
	return KErrNone;
}