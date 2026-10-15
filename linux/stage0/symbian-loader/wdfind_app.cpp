// finds the free-running counters (system timer / watchdog) in the sysctrl
// block at phys 0x30000000 (live kernel va 0xc6018000, safe to read).
// reads the block, spins, reads again, whatever changed is a counter.
// dumps A+B to E:\wdiff.bin for diffing on the pc. reversible and safe
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\wdfind.log");
_LIT(KBin, "E:\\wdiff.bin");
const TUint32 KSysVirt=0xc6018000u; // -> phys 0x30000000 (sysctrl)
const TUint32 KCount=256; // 1 kb window

GLDEF_C TInt E32Main(){
	RFs fs;
	if(fs.Connect()!=KErrNone){return KErrGeneral;}
	RFile log;
	log.Replace(fs, KLog, EFileWrite);
	TBuf8<96> b;
	b.Copy(_L8("wdfind\n"));
	log.Write(b); log.Flush();

	User::FreeLogicalDevice(KRunBlobName);
	TInt rLoad=User::LoadLogicalDevice(KRunBlobLdd);
	b.Format(_L8("Load=%d\n"), rLoad);
	log.Write(b); log.Flush();

	if(rLoad==KErrNone || rLoad==KErrAlreadyExists){
		RRunBlob dev;
		if(dev.Open()==KErrNone){
			TUint8* mem=(TUint8*)User::Alloc(KCount*8);
			if(mem){
				TPtr8 buf(mem, 0, KCount*8);
				TInt r=dev.TimedDiff(KSysVirt, KCount, 60000000u, buf);
				b.Format(_L8("TimedDiff=%d len=%d\n"), r, buf.Length());
				log.Write(b); log.Flush();
				if(buf.Length()==(TInt)(KCount*8)){
					RFile f;
					if(f.Replace(fs, KBin, EFileWrite)==KErrNone){f.Write(buf); f.Flush(); f.Close();}
					// quick summary in the log: how many regs changed
					const TUint32* w=(const TUint32*)buf.Ptr();
					TInt changed=0, firstOff=-1;
					for(TUint i=0; i<KCount; ++i){
						if(w[i]!=w[KCount+i]){
							changed++;
							if(firstOff<0){firstOff=(TInt)(i*4);}
						}
					}
					b.Format(_L8("changed=%d firstOff=0x%x\n"), changed, firstOff);
					log.Write(b); log.Flush();
				}
				User::Free(mem);
			}
			dev.Close();
		}
	}
	log.Close();
	fs.Close();
	TBuf<48> n;
	n.Copy(_L("wdfind done"));
	User::InfoPrint(n);
	return KErrNone;
}