// installs the transport-fn hook (EControlHookInstall), then polls the capture
// ring for ~5 min while you USE the phone (lock/unlock, open apps, settings) to
// provoke secure calls and catch the 300 s keepalive, decoding each new request
// to E:\hook.log, then removes the hook. the ring is ring[0]=count, 7-word
// entries at +0x20: [0]descptr [1]cmd [2]PAid [3]w2 [4]reqphys [5]respphys [6]flag.
// for each new entry we also ReadPhys the reqbuf to recover the subtype.
// IMPORTANT: this redirects live securitykext code, so we always HookRemove
// before exit and never FreeLogicalDevice while armed.
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\hook.log");
_LIT(KBin, "E:\\hook.bin");
const TInt KPolls=600;        // ~5 min at 0.5 s
const TInt KEntrySz=8;        // words per ring slot (7 used)

static RFile gLog;
static void L(const TDesC8& m){ gLog.Write(m); gLog.Flush(); }

GLDEF_C TInt E32Main(){
	RFs fs;
	if(fs.Connect()!=KErrNone){return KErrGeneral;}
	if(gLog.Replace(fs, KLog, EFileWrite)!=KErrNone){fs.Close(); return KErrGeneral;}
	L(_L8("hook: install transport hook, capture secure requests\n"));

	User::FreeLogicalDevice(KRunBlobName);
	TInt rLoad=User::LoadLogicalDevice(KRunBlobLdd);
	TBuf8<160> b;
	b.Format(_L8("Load=%d\n"), rLoad); L(b);
	if(rLoad!=KErrNone && rLoad!=KErrAlreadyExists){gLog.Close(); fs.Close(); return rLoad;}

	RRunBlob dev;
	if(dev.Open()!=KErrNone){gLog.Close(); fs.Close(); return KErrGeneral;}

	TInt rI=dev.HookInstall();
	b.Format(_L8("HookInstall=%d (armed; now use the phone)\n"), rI); L(b);
	if(rI==KErrNone){
		TUint32 ring[0x400];
		TUint32 seen=0;
		for(TInt poll=0; poll<KPolls; ++poll){
			TPtr8 rp((TUint8*)ring, 0, sizeof(ring));
			if(dev.HookRead(rp)==KErrNone){
				TUint32 count=ring[0];
				while(seen<count){
					TUint32 slot=seen&63;
					TUint32* e=&ring[(0x20/4)+slot*KEntrySz];
					TUint8 sub=0;
					if(e[4]){ // reqphys -> try to read the subtype (reqbuf+2)
						TUint8 rb[16]; TPtr8 bp(rb, 0, sizeof(rb));
						if(dev.ReadPhys(e[4], bp)==KErrNone){sub=rb[2];}
					}
					b.Format(_L8("req #%u: cmd=%08x PAid=%08x w2=%08x reqphys=%08x respphys=%08x flag=%08x sub=0x%02x\n"),
						seen, e[1], e[2], e[3], e[4], e[5], e[6], sub);
					L(b);
					seen++;
				}
			}
			User::After(500000); // 0.5 s
		}
		// final raw ring dump for offline inspection
		TPtr8 rp((TUint8*)ring, 0, sizeof(ring));
		if(dev.HookRead(rp)==KErrNone){
			RFile bin;
			if(bin.Replace(fs, KBin, EFileWrite)==KErrNone){
				bin.Write(TPtrC8((const TUint8*)ring, sizeof(ring))); bin.Flush(); bin.Close();
			}
		}
		b.Format(_L8("captured %u requests total\n"), seen); L(b);
	}

	TInt rR=dev.HookRemove();
	b.Format(_L8("HookRemove=%d\n"), rR); L(b);
	dev.Close();
	gLog.Close(); fs.Close();
	return KErrNone;
}