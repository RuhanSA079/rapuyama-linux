// ONE-WAY launcher for securetrace.S: kill symbian, feed the known soc + pmic
// watchdogs from bare metal, and log fast-counter heartbeat intervals to
// phys 0x1f080000 (inside the reserved ramcon area). the loop runs until the
// phone resets by itself; afterwards run e6logs (it already dumps the whole
// 1 mb at 0x1f000000) and parse with tools/parse_securetrace.py.
// signal: records * ~31 ms = how long the non-secure loop lived; a big final
// interval = the cpu stalled before the reset. step log -> E:\securetrace.log
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\securetrace.log");

GLDEF_C TInt E32Main(){
	RFs fs;
	if(fs.Connect()!=KErrNone){return KErrGeneral;}
	RFile log;
	if(log.Replace(fs, KLog, EFileWrite)!=KErrNone){fs.Close(); return KErrGeneral;}

	TBuf8<64> b;
	b.Copy(_L8("securetrace: started\n"));
	log.Write(b); log.Flush();

	User::FreeLogicalDevice(KRunBlobName);
	TInt rLoad=User::LoadLogicalDevice(KRunBlobLdd);
	b.Format(_L8("LoadLogicalDevice=%d\n"), rLoad);
	log.Write(b); log.Flush();

	if(rLoad==KErrNone || rLoad==KErrAlreadyExists){
		RRunBlob dev;
		TInt rOpen=dev.Open();
		b.Format(_L8("Open=%d\n"), rOpen);
		log.Write(b); log.Flush();
		if(rOpen==KErrNone){
			b.Copy(_L8("about-to-SecureTrace (one-way; time the reboot)\n"));
			log.Write(b); log.Flush();
			// close everything BEFORE the point of no return
			log.Close();
			fs.Close();
			dev.SecureTrace(); // never returns
			return KErrNone;
		}
	}

	log.Close();
	fs.Close();
	return KErrGeneral;
}