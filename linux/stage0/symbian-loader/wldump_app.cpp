// dumps what the N8 WLAN driver gets at every WLAN start: the TI WL1271 NVS
// (radio calibration + MAC, built by wlanhwinit.dll from the modem's
// permanent memory and its own defaults), the WL1271 firmware and the MAC
// address. read only: it asks wlanhwinit.dll through its exported API, the
// same calls wlanpdd/wlanldd make when WLAN is switched on.
//   E:\wl_nvs.bin   init data (GetHwInitData)
//   E:\wl_fw.bin    firmware  (GetHwInitData)
//   E:\wldump.log   lengths, first bytes, MAC
// the calls run in a worker thread with its own cleanup stack and active
// scheduler (the ISI client code under wlanhwinit wants one); the main
// thread logs how that thread ended, so a leave or crash in NewL shows up
// as exit type / category / reason instead of a silent panic.
// build: APP_LIBS="driver/import/wlanhwinit.dso driver/import/euser_full.dso"
//        APP_CAPS=all-tcb ./build_app.sh wldump wldump_app.cpp 0xe1000054
#include <e32std.h>
#include <f32file.h>

// the wlanhwinit.dll API (oss.FCL.sf.adapt.TIWLANWL1271 wlanhwinit_stub,
// eabi/wlanhwinitu.def ordinals 1 GetHwInitData, 3 GetMacAddress, 6 NewL).
// only the exported calls are used, so the class layout doesn't matter
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

_LIT(KLog, "E:\\wldump.log");
_LIT(KNvs, "E:\\wl_nvs.bin");
_LIT(KFw, "E:\\wl_fw.bin");

static RFs gFs;

static void L(const TDesC8& m){
	RFile f; TInt r=f.Open(gFs, KLog, EFileWrite);
	if(r==KErrNotFound){r=f.Create(gFs, KLog, EFileWrite);}
	if(r!=KErrNone){return;}
	TInt p=0; f.Seek(ESeekEnd, p); f.Write(m); f.Flush(); f.Close();
}

static void Head(const TDesC8& aName, const TUint8* aPtr, TUint aLen){
	TBuf8<160> b;
	b.Format(_L8("%S: %u bytes at %08x:"), &aName, aLen, (TUint32)aPtr);
	for(TUint i=0; aPtr && i<aLen && i<24; i++){b.AppendFormat(_L8(" %02x"), aPtr[i]);}
	b.Append('\n');
	L(b);
}

static TInt Save(const TDesC& aFile, const TUint8* aPtr, TUint aLen){
	if(!aPtr || !aLen){return KErrNotFound;}
	RFile f;
	TInt r=f.Replace(gFs, aFile, EFileWrite);
	if(r==KErrNone){r=f.Write(TPtrC8(aPtr, aLen)); f.Flush(); f.Close();}
	return r;
}

static void DumpL(){
	L(_L8("calling CWlanHwInit::NewL\n"));
	CWlanHwInit* hw=CWlanHwInit::NewL();
	L(_L8("CWlanHwInit::NewL ok\n"));

	TMacAddr mac;
	Mem::FillZ(&mac, sizeof(mac));
	TInt r=hw->GetMacAddress(mac);
	TBuf8<80> b;
	b.Format(_L8("GetMacAddress=%d %02x:%02x:%02x:%02x:%02x:%02x\n"), r,
		 mac.iMacAddress[0], mac.iMacAddress[1], mac.iMacAddress[2],
		 mac.iMacAddress[3], mac.iMacAddress[4], mac.iMacAddress[5]);
	L(b);

	const TUint8* nvs=NULL; const TUint8* fw=NULL;
	TUint nvsLen=0, fwLen=0;
	hw->GetHwInitData(&nvs, nvsLen, &fw, fwLen);
	Head(_L8("nvs"), nvs, nvsLen);
	Head(_L8("fw"), fw, fwLen);
	b.Format(_L8("save nvs=%d fw=%d\n"), Save(KNvs, nvs, nvsLen), Save(KFw, fw, fwLen));
	L(b);
	// the object stays alive until the process ends: its destructor isn't
	// needed and its class layout isn't known here
}

// a leave is a C++ throw of XLeaveException here (leave == throw); with
// -fexceptions e32cmn.h declares it, and libsupc++ matches the phone's
// typeinfo by name. GetReason() also ends the leave (Exec::LeaveEnd).
// v1..v5 had no catch: E32USER-CBase 66, the code was lost

static TInt Worker(TAny*){
	CTrapCleanup* tc=CTrapCleanup::New();
	CActiveScheduler* as=new CActiveScheduler;
	if(!tc || !as){return KErrNoMemory;}
	CActiveScheduler::Install(as);
	L(_L8("worker: cleanup stack + active scheduler ready\n"));
	// TRAP by hand: built with -fexceptions against the phone's runtime
	TInt err=KErrNone;
	TTrapHandler* th=User::MarkCleanupStack();
	try {
		DumpL();
		User::UnMarkCleanupStack(th);
	} catch(XLeaveException& l) {
		err=l.GetReason();
	} catch(...) {
		err=KErrUnknown;
		L(_L8("worker: non-leave C++ exception\n"));
	}
	TBuf8<48> b;
	b.Format(_L8("worker: done, leave code %d\n"), err);
	L(b);
	return err;
}

GLDEF_C TInt E32Main(){
	if(gFs.Connect()!=KErrNone){return KErrGeneral;}
	// the worker logs and saves through the same session: without this its
	// first file call is a bad handle (v2: KERN-EXEC 0)
	gFs.ShareAuto();
	L(_L8("wldump v6\n"));
	RThread t;
	TInt r=t.Create(_L("wldump-worker"), Worker, 0x8000, 0x1000, 0x100000, NULL);
	TBuf8<120> b;
	if(r!=KErrNone){
		b.Format(_L8("RThread::Create=%d\n"), r);
		L(b);
		gFs.Close();
		return r;
	}
	TRequestStatus st;
	t.Logon(st);
	t.Resume();
	User::WaitForRequest(st);
	TExitCategoryName cat=t.ExitCategory();
	TBuf8<KMaxExitCategoryName> cat8;
	cat8.Copy(cat);
	b.Format(_L8("worker ended: type %d (0 kill, 1 terminate, 2 panic) category '%S' reason %d\n"),
		 (TInt)t.ExitType(), &cat8, t.ExitReason());
	L(b);
	t.Close();
	gFs.Close();
	return KErrNone;
}
