// ONE-WAY watchdog test launcher
// loads the runblob ldd and calls KickLoop(), which kills the mmu/symbian and
// kicks both watchdogs (soc counter + pmic over cbus) forever from bare metal.
// phone alive (frozen) for minutes = our kicks work, off after ~2 s = there is
// another watchdog. battery pull to recover. step log -> E:\kickloop.log
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\kickloop.log");

GLDEF_C TInt E32Main(){
	RFs fs;
	if(fs.Connect()!=KErrNone){return KErrGeneral;}
	RFile log;
	if(log.Replace(fs, KLog, EFileWrite)!=KErrNone){fs.Close(); return KErrGeneral;}

	TBuf8<64> b;
	b.Copy(_L8("kickloop: started\n"));
	log.Write(b); log.Flush();

	_LIT(KLdd, "runblob.ldd");
	User::FreeLogicalDevice(KRunBlobName);
	TInt rLoad=User::LoadLogicalDevice(KLdd);
	b.Format(_L8("LoadLogicalDevice=%d\n"), rLoad);
	log.Write(b); log.Flush();

	if(rLoad==KErrNone || rLoad==KErrAlreadyExists){
		RRunBlob dev;
		TInt rOpen=dev.Open();
		b.Format(_L8("Open=%d\n"), rOpen);
		log.Write(b); log.Flush();
		if(rOpen==KErrNone){
			b.Copy(_L8("about-to-KickLoop (one-way; time how long it stays alive)\n"));
			log.Write(b); log.Flush();
			// close everything BEFORE the point of no return
			log.Close();
			fs.Close();
			dev.KickLoop(); // never returns
			return KErrNone;
		}
	}

	log.Close();
	fs.Close();
	return KErrGeneral;
}