// ONE-WAY takeover launcher
// loads the runblob ldd and calls Takeover(), which kills the mmu/symbian and
// drives the display from bare metal in an infinite loop. never returns, the
// screen should start cycling colours (R/G/B/W). battery pull to recover.
// step log (flushed) goes to E:\runblob.log up to the handoff
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\runblob.log");

GLDEF_C TInt E32Main(){
	RFs fs;
	if(fs.Connect()!=KErrNone){return KErrGeneral;}
	RFile log;
	if(log.Replace(fs, KLog, EFileWrite)!=KErrNone){fs.Close(); return KErrGeneral;}

	TBuf8<64> b;
	b.Copy(_L8("takeover: started\n"));
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
			b.Copy(_L8("about-to-Takeover (one-way; watch the screen)\n"));
			log.Write(b); log.Flush();
			// close everything BEFORE the point of no return
			log.Close();
			fs.Close();
			dev.Takeover(); // never returns
			return KErrNone;
		}
	}

	log.Close();
	fs.Close();
	return KErrGeneral;
}