// READ-ONLY watcher of the APE-side modem control block (0xf0001000 =
// *(SuperPage+0x2c)) and the superpage head, polled every 200 ms in LIVE
// Symbian to catch the periodic "pet" the APE does to keep the modem from
// resetting us. the reset is silent (no irq fired in any Linux boot), so the
// pet is a periodic WRITE only Symbian makes - if it lands in this block we see
// which word changes and how often. safe: ReadVirt (Kern::SafeRead) of known
// superpage ram, no reset. logs every changed word with a timestamp to
// E:\mtrace.log. let it run a few minutes; watch for a word that ticks on a
// regular period (~tens of s to ~210 s = the supervision pet).
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\mtrace.log");
const TUint32 KBlock=0xf0001000u;  // modem control block (APE side)
const TUint32 KSuper=0xf0000000u;  // superpage head
const TInt KWords=0x40;            // 0x100 bytes of the block
const TInt KSWords=0x10;           // 0x40 bytes of the superpage head
const TInt KPolls=1500;            // ~5 min at 200 ms

static RFile gLog;
static void L(const TDesC8& m){ gLog.Write(m); gLog.Flush(); }

GLDEF_C TInt E32Main(){
	RFs fs;
	if(fs.Connect()!=KErrNone){return KErrGeneral;}
	if(gLog.Replace(fs, KLog, EFileWrite)!=KErrNone){fs.Close(); return KErrGeneral;}
	L(_L8("mtrace: watching modem block 0xf0001000 for the periodic pet\n"));

	User::FreeLogicalDevice(KRunBlobName);
	TInt rLoad=User::LoadLogicalDevice(KRunBlobLdd);
	TBuf8<160> b; b.Format(_L8("Load=%d\n"), rLoad); L(b);
	if(rLoad!=KErrNone && rLoad!=KErrAlreadyExists){gLog.Close(); fs.Close(); return rLoad;}
	RRunBlob dev;
	if(dev.Open()!=KErrNone){gLog.Close(); fs.Close(); return KErrGeneral;}

	TUint32 blk[0x40], sup[0x10], pblk[0x40], psup[0x10];
	TPtr8 bp((TUint8*)pblk, 0, KWords*4), sp((TUint8*)psup, 0, KSWords*4);
	dev.ReadVirt(KBlock, bp); dev.ReadVirt(KSuper, sp);
	TUint32 t0=User::TickCount();

	for(TInt poll=0; poll<KPolls; ++poll){
		TPtr8 b2((TUint8*)blk, 0, KWords*4), s2((TUint8*)sup, 0, KSWords*4);
		dev.ReadVirt(KBlock, b2); dev.ReadVirt(KSuper, s2);
		TUint32 ms=(User::TickCount()-t0)*1000/64; // ~15.6ms ticks
		for(TInt i=0; i<KWords; ++i){
			if(blk[i]!=pblk[i]){
				b.Format(_L8("%u.%03us blk+%03x: %08x -> %08x\n"),
					ms/1000, ms%1000, i*4, pblk[i], blk[i]); L(b);
				pblk[i]=blk[i];
			}
		}
		for(TInt i=0; i<KSWords; ++i){
			if(sup[i]!=psup[i]){
				b.Format(_L8("%u.%03us sup+%03x: %08x -> %08x\n"),
					ms/1000, ms%1000, i*4, psup[i], sup[i]); L(b);
				psup[i]=sup[i];
			}
		}
		User::After(200000); // 200 ms
	}
	L(_L8("mtrace done\n"));
	dev.Close();
	gLog.Close(); fs.Close();
	return KErrNone;
}