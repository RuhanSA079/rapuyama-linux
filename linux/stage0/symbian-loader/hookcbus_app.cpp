// patches karap's cbus-write fn (0x800b8494) via EControlHookCbus so EVERY
// PMIC/CBUS write Symbian makes is logged {dev,reg,val}, then polls the ring for
// ~4 min in LIVE Symbian and writes each new write with a timestamp to
// E:\cbus.log, then removes the patch. goal: find a hidden PERIODIC pet (a reg
// written on a ~tens-of-s..~210s period that we don't replicate = the thing that
// keeps the modem/pmic from resetting us). the known 3 s kick is reg 0x17=0x30 +
// (soc counter, not cbus); anything else periodic is the lead.
// CRITICAL: always HookCbusRemove before exit (restores live kernel rom); never
// FreeLogicalDevice while patched. a power cycle also clears the patch.
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\cbus.log");
const TInt KPolls=480;   // ~4 min at 500 ms
const TInt KSlots=64;

static RFile gLog;
static void L(const TDesC8& m){ gLog.Write(m); gLog.Flush(); }

GLDEF_C TInt E32Main(){
	RFs fs;
	if(fs.Connect()!=KErrNone){return KErrGeneral;}
	if(gLog.Replace(fs, KLog, EFileWrite)!=KErrNone){fs.Close(); return KErrGeneral;}
	L(_L8("cbus: logging every PMIC write via rom hook on 0x800b8494\n"));

	User::FreeLogicalDevice(KRunBlobName);
	TInt rLoad=User::LoadLogicalDevice(KRunBlobLdd);
	TBuf8<160> b; b.Format(_L8("Load=%d\n"), rLoad); L(b);
	if(rLoad!=KErrNone && rLoad!=KErrAlreadyExists){gLog.Close(); fs.Close(); return rLoad;}
	RRunBlob dev;
	if(dev.Open()!=KErrNone){gLog.Close(); fs.Close(); return KErrGeneral;}

	TInt rI=dev.HookCbus();
	b.Format(_L8("HookCbus=%d (patched; logging. 0=ok, -21=rom not writable)\n"), rI); L(b);
	if(rI==KErrNone){
		TUint32 ring[0x400];
		TUint32 seen=0, t0=User::TickCount();
		for(TInt poll=0; poll<KPolls; ++poll){
			TPtr8 rp((TUint8*)ring, 0, sizeof(ring));
			if(dev.HookCbusRead(rp)==KErrNone){
				TUint32 count=ring[0];
				TUint32 ms=(User::TickCount()-t0)*1000/64;
				while(seen<count){
					TUint32 s=seen&(KSlots-1);
					TUint32 dev1=ring[s*4+1], reg=ring[s*4+2], val=ring[s*4+3];
					b.Format(_L8("%u.%03us  #%u dev=%u reg=0x%02x val=0x%08x\n"),
						ms/1000, ms%1000, seen, dev1, reg, val);
					L(b);
					seen++;
				}
			}
			User::After(500000); // 0.5 s
		}
		b.Format(_L8("captured %u writes\n"), seen); L(b);
	}

	TInt rR=dev.HookCbusRemove();
	b.Format(_L8("HookCbusRemove=%d\n"), rR); L(b);
	dev.Close();
	gLog.Close(); fs.Close();
	return KErrNone;
}