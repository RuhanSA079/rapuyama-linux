// dumps what the N8 WLAN driver gets at every WLAN start: the TI WL1271 NVS
// (radio calibration + MAC, built by wlanhwinit.dll from the modem's
// permanent memory and its own defaults), the WL1271 firmware and the MAC
// address. read only: it asks wlanhwinit.dll through its exported API, the
// same calls wlanpdd/wlanldd make when WLAN is switched on.
//   E:\wl_nvs.bin   init data (GetHwInitData)
//   E:\wl_fw.bin    firmware  (GetHwInitData)
//   E:\wldump.log   lengths, first bytes, MAC
// build: APP_LIBS=driver/import/wlanhwinit.dso APP_CAPS=all-tcb
//        ./build_app.sh wldump wldump_app.cpp 0xe1000054
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

GLDEF_C TInt E32Main(){
	CTrapCleanup* tc=CTrapCleanup::New();
	if(gFs.Connect()!=KErrNone){return KErrGeneral;}
	L(_L8("wldump v1\n"));
	// no TRAP: the EUSER.dso here has no TTrap exports. a leave (only on
	// out of memory) panics the app, the log shows how far it got
	DumpL();
	L(_L8("done\n"));
	gFs.Close();
	delete tc;
	return KErrNone;
}
