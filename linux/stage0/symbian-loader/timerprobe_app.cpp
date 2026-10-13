// checks the timers i reversed from the firmware (docs/driver-re.md) on the
// live phone before trusting them in the linux driver: fast counter should do
// ~32768/s, the ms timer should count down inside 0..0x9600 with reload 0x9600.
// dumps the raw words to E:\timerprobe.bin (tools/parse_timerprobe.py). read only, safe
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\timerprobe.log");
_LIT(KBin, "E:\\timerprobe.bin");

GLDEF_C TInt E32Main(){
	RFs fs;
	if(fs.Connect()!=KErrNone){return KErrGeneral;}
	RFile log;
	log.Replace(fs, KLog, EFileWrite);
	TBuf8<128> b;
	b.Copy(_L8("timerprobe\n"));
	log.Write(b); log.Flush();

	User::FreeLogicalDevice(KRunBlobName);
	_LIT(KLdd, "runblob.ldd");
	TInt rLoad=User::LoadLogicalDevice(KLdd);
	b.Format(_L8("Load=%d\n"), rLoad);
	log.Write(b); log.Flush();

	if(rLoad==KErrNone || rLoad==KErrAlreadyExists){
		RRunBlob dev;
		if(dev.Open()==KErrNone){
			TBuf8<KTimerProbeWords*4> buf;
			TInt r=dev.TimerProbe(buf);
			b.Format(_L8("TimerProbe=%d len=%d\n"), r, buf.Length());
			log.Write(b); log.Flush();
			if(buf.Length()==KTimerProbeWords*4){
				RFile f;
				if(f.Replace(fs, KBin, EFileWrite)==KErrNone){f.Write(buf); f.Flush(); f.Close();}
				const TUint32* w=(const TUint32*)buf.Ptr();
				b.Format(_L8("ticks %u fc %u (want ~1000 / ~32768) reload %x\n"), w[37]-w[1], w[38]-w[2], w[40]);
				log.Write(b); log.Flush();
			}
			dev.Close();
		}
	}
	log.Close();
	fs.Close();
	return KErrNone;
}