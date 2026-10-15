// drives EControlSnoopPoll to catch securitykext's transient secure request.
// the descriptor at 0xc670c450 is only non-zero for a micro-window around each
// smc, so we tight-poll it from the kernel back-to-back for a few minutes until
// we catch one (the 300 s keepalive and/or the more frequent PA_SWD calls).
// read-only, symbian stays alive the whole time. each catch (40 words: 6 desc +
// 16 shared-header + 16 reqbuf) is logged decoded to E:\snoop2.log and raw to
// E:\snoop2.bin. let it run; use the phone a bit (lock/unlock, open apps) to
// provoke secure calls. stops after KMaxCatch catches or KMaxBurst bursts.
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\snoop2.log");
_LIT(KBin, "E:\\snoop2.bin");
const TInt KMaxCatch=16;
const TInt KMaxBurst=400000; // ~ a few minutes of back-to-back bursts

static RFile gLog, gBin;
static void L(const TDesC8& m){ gLog.Write(m); gLog.Flush(); }

GLDEF_C TInt E32Main(){
	RFs fs;
	if(fs.Connect()!=KErrNone){return KErrGeneral;}
	if(gLog.Replace(fs, KLog, EFileWrite)!=KErrNone){fs.Close(); return KErrGeneral;}
	if(gBin.Replace(fs, KBin, EFileWrite)!=KErrNone){gLog.Close(); fs.Close(); return KErrGeneral;}
	L(_L8("snooppoll: tight-polling securitykext descriptor (read-only)\n"));

	User::FreeLogicalDevice(KRunBlobName);
	TInt rLoad=User::LoadLogicalDevice(KRunBlobLdd);
	TBuf8<128> b;
	b.Format(_L8("Load=%d\n"), rLoad); L(b);
	if(rLoad!=KErrNone && rLoad!=KErrAlreadyExists){gBin.Close(); gLog.Close(); fs.Close(); return rLoad;}

	RRunBlob dev;
	if(dev.Open()!=KErrNone){gBin.Close(); gLog.Close(); fs.Close(); return KErrGeneral;}

	TUint32 snap[40];
	TInt caught=0, burst=0;
	TUint32 lastCmd=0, lastPA=0;
	while(caught<KMaxCatch && burst<KMaxBurst){
		TPtr8 p((TUint8*)snap, 0, sizeof(snap));
		TInt r=dev.SnoopPoll(p, 0);
		burst++;
		if(r!=1){continue;}
		// skip an identical back-to-back snapshot (same cmd+PAid+reqphys)
		if(snap[0]==lastCmd && snap[1]==lastPA){continue;}
		lastCmd=snap[0]; lastPA=snap[1];
		caught++;
		TUint8 subtype=(TUint8)((snap[22]>>16)&0xff); // reqbuf+2
		b.Format(_L8("\ncatch %d (burst %d): cmd=%08x PAid=%08x reqphys=%08x respphys=%08x flag=%08x subtype=0x%02x\n"),
			caught, burst, snap[0], snap[1], snap[3], snap[4], snap[5], subtype);
		L(b);
		b.Format(_L8("  reqbuf: %08x %08x %08x %08x %08x %08x %08x %08x\n"),
			snap[22], snap[23], snap[24], snap[25], snap[26], snap[27], snap[28], snap[29]); L(b);
		b.Format(_L8("  shared+400: %08x %08x %08x %08x\n"), snap[6], snap[7], snap[8], snap[9]); L(b);
		gBin.Write(TPtrC8((const TUint8*)snap, sizeof(snap))); gBin.Flush();
	}
	b.Format(_L8("\ndone: %d catches in %d bursts\n"), caught, burst); L(b);
	dev.Close();
	gBin.Close(); gLog.Close(); fs.Close();
	return KErrNone;
}