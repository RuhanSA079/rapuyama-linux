// calls Symbian's own SisaResetGenerate (0x801039e0) in LIVE Symbian to reset /
// quiesce the modem via the firmware's real sequence, then sleeps so the queued
// reset DFC runs. idea: get the modem into a non-supervising state using Symbian
// (which knows how), THEN boot Linux separately (e6boot) into a machine the modem
// won't reset. run this FIRST, watch the phone survive (not reboot/power-off),
// then run e6boot. the reset TYPE (0x80|t) is a guess - if the phone powers off
// or reboots, that type is wrong; tell me and I'll try another.
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\modemoff.log");
const TUint32 KType=0x8c; // 0x80 (required bit) | reset type 0xc (special path)

static RFile gLog;
static void L(const TDesC8& m){ gLog.Write(m); gLog.Flush(); }

GLDEF_C TInt E32Main(){
	RFs fs;
	if(fs.Connect()!=KErrNone){return KErrGeneral;}
	if(gLog.Replace(fs, KLog, EFileWrite)!=KErrNone){fs.Close(); return KErrGeneral;}
	L(_L8("modemoff: SisaResetGenerate via firmware\n"));

	User::FreeLogicalDevice(KRunBlobName);
	TInt rLoad=User::LoadLogicalDevice(KRunBlobLdd);
	TBuf8<96> b; b.Format(_L8("Load=%d\n"), rLoad); L(b);
	if(rLoad!=KErrNone && rLoad!=KErrAlreadyExists){gLog.Close(); fs.Close(); return rLoad;}
	RRunBlob dev;
	if(dev.Open()!=KErrNone){gLog.Close(); fs.Close(); return KErrGeneral;}

	b.Format(_L8("calling SisaResetGenerate(0x%02x)...\n"), KType); L(b);
	TInt r=dev.ModemReset(KType);
	b.Format(_L8("ModemReset ret=%d (if you read this, no immediate panic)\n"), r); L(b);

	// let the reset DFC + modem act
	User::After(2000000); // 2 s
	L(_L8("still alive 2s after modem reset. now run e6boot to try Linux.\n"));

	dev.Close();
	gLog.Close(); fs.Close();
	return KErrNone;
}