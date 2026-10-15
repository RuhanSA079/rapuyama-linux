// ONE-WAY launcher for smcprobe.S: kill symbian, feed the known watchdogs, then
// issue one smc #0 the way securitykext does and log whether the secure monitor
// returned, into phys 0x1f080000 (inside the reserved ramcon area). the loop
// then feeds the watchdogs until the phone resets by itself; afterwards run
// e6logs (it dumps the whole 1 mb at 0x1f000000) and parse with
// tools/parse_smcprobe.py.
// signal: 'after' marker set = we entered+left the monitor safely from bare
// metal (foundation for a keepalive). only 'before' set = the smc did not return
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\smcprobe.log");

GLDEF_C TInt E32Main(){
	RFs fs;
	if(fs.Connect()!=KErrNone){return KErrGeneral;}
	RFile log;
	if(log.Replace(fs, KLog, EFileWrite)!=KErrNone){fs.Close(); return KErrGeneral;}

	TBuf8<64> b;
	b.Copy(_L8("smcprobe: started\n"));
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
			b.Copy(_L8("about-to-SmcProbe (one-way; time the reboot)\n"));
			log.Write(b); log.Flush();
			// close everything BEFORE the point of no return
			log.Close();
			fs.Close();
			dev.SmcProbe(); // never returns
			return KErrNone;
		}
	}

	log.Close();
	fs.Close();
	return KErrGeneral;
}