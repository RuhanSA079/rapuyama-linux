// n8wlnv: n8boot's helper (same sis), started by n8boot before the jump.
// Asks wlanhwinit.dll for the WL1271 NVS + MAC exactly like wldump
// (wldump_app.cpp: CWlanHwInit::NewL, GetMacAddress, GetHwInitData, read
// only, the same calls the WLAN driver makes at every WLAN start) and
// converts it to the Linux wl127x-nvs.bin (n8nvs.h). Results go to the RAM
// drive only, never to E:, n8boot reads and deletes them:
//   D:\n8wlnv.bin  the 912 byte Linux NVS (holds the phone's MAC: private)
//   D:\n8wlnv.txt  status lines for E:\boot.log, MAC masked
// exit reason: 0 ok, <0 error.
// A separate exe because wlanhwinit.dll is all-tcb: a process with TCB
// (n8boot has all capabilities) can't load it. built with C++ exceptions,
// wlanhwinit leaves through TRAP: see build_app.sh EXTRA_* and wldump
#include <e32std.h>
#include <e32base.h>
#include <f32file.h>
#include "n8nvs.h"

struct TMacAddr {
	TUint8 iMacAddress[6];
};

class CWlanHwInit {
public:
	IMPORT_C static CWlanHwInit* NewL();
	IMPORT_C void GetHwInitData(const TUint8** aInitData, TUint& aInitLength,
				    const TUint8** aFwData, TUint& aFwLength);
	IMPORT_C TInt GetMacAddress(TMacAddr& aMacAddress);
};

_LIT(KBin, "D:\\n8wlnv.bin");
_LIT(KTxt, "D:\\n8wlnv.txt");

static RFs gFs;
static RFile gTxt;
static TUint8 gOut[N8NVS_OUT_LEN];

static void L(const TDesC8& m){gTxt.Write(m); gTxt.Flush();}

// the first 3 bytes (the vendor) shown, the rest masked
static void MacLine(const TDesC8& aWhat, const TUint8* aMac){
	TBuf8<64> b;
	b.Format(_L8("n8wlnv: %S %02x:%02x:%02x:xx:xx:xx\n"), &aWhat, aMac[0], aMac[1], aMac[2]);
	L(b);
}

static TInt DumpL(){
	CWlanHwInit* hw=CWlanHwInit::NewL();

	TMacAddr mac;
	Mem::FillZ(&mac, sizeof(mac));
	TInt rm=hw->GetMacAddress(mac);

	const TUint8* init=NULL; const TUint8* fw=NULL;
	TUint initLen=0, fwLen=0;
	hw->GetHwInitData(&init, initLen, &fw, fwLen);
	TBuf8<96> b;
	b.Format(_L8("n8wlnv: GetMacAddress=%d, hw init data %u + %u bytes\n"), rm, initLen, fwLen);
	L(b);
	if(!fw){return KErrNotFound;}

	TInt r=n8nvs_convert(fw, fwLen, gOut);
	b.Format(_L8("n8wlnv: convert=%d (0 ok)\n"), r);
	L(b);
	if(r!=N8NVS_OK){return KErrCorrupt;}

	TUint8 nvsMac[6];
	n8nvs_mac(gOut, nvsMac);
	MacLine(_L8("NVS MAC"), nvsMac);
	if(rm==KErrNone){
		MacLine(_L8("wlanhwinit MAC"), mac.iMacAddress);
		TBool same=ETrue;
		for(TInt i=0; i<6; i++){same=same && nvsMac[i]==mac.iMacAddress[i];}
		L(same ?
			_L8("n8wlnv: MACs match\n") : _L8("n8wlnv: MACs DIFFER (the NVS one is used)\n"));
	}

	RFile f;
	r=f.Replace(gFs, KBin, EFileWrite);
	if(r==KErrNone){r=f.Write(TPtrC8(gOut, sizeof(gOut))); f.Flush(); f.Close();}
	b.Format(_L8("n8wlnv: wrote %d bytes, %d\n"), sizeof(gOut), r);
	L(b);
	// the object stays alive until the process ends (as in wldump)
	return r;
}

// a leave is a C++ throw of XLeaveException here, see wldump_app.cpp
static TInt Worker(TAny*){
	CTrapCleanup* tc=CTrapCleanup::New();
	CActiveScheduler* as=new CActiveScheduler;
	if(!tc || !as){return KErrNoMemory;}
	CActiveScheduler::Install(as);
	TInt err=KErrNone;
	TTrapHandler* th=User::MarkCleanupStack();
	try {
		err=DumpL();
		User::UnMarkCleanupStack(th);
	} catch(XLeaveException& l) {
		err=l.GetReason();
		TBuf8<48> b;
		b.Format(_L8("n8wlnv: leave %d\n"), err);
		L(b);
	} catch(...) {
		err=KErrUnknown;
		L(_L8("n8wlnv: non-leave C++ exception\n"));
	}
	return err;
}

GLDEF_C TInt E32Main(){
	if(gFs.Connect()!=KErrNone){return KErrGeneral;}
	// the worker writes through the same session (wldump v2: KERN-EXEC 0)
	gFs.ShareAuto();
	gFs.Delete(KBin);
	if(gTxt.Replace(gFs, KTxt, EFileWrite)!=KErrNone){gFs.Close(); return KErrGeneral;}
	RThread t;
	TInt r=t.Create(_L("n8wlnv-worker"), Worker, 0x8000, 0x1000, 0x100000, NULL);
	if(r==KErrNone){
		TRequestStatus st;
		t.Logon(st);
		t.Resume();
		User::WaitForRequest(st);
		r=t.ExitType()==EExitKill ? t.ExitReason() : KErrDied;
		if(t.ExitType()!=EExitKill){
			TExitCategoryName cat=t.ExitCategory();
			TBuf8<KMaxExitCategoryName> cat8;
			cat8.Copy(cat);
			TBuf8<96> b;
			b.Format(_L8("n8wlnv: worker panicked '%S' %d\n"), &cat8, t.ExitReason());
			L(b);
		}
		t.Close();
	}
	if(r!=KErrNone){gFs.Delete(KBin);}
	gTxt.Close();
	gFs.Close();
	return r;
}
