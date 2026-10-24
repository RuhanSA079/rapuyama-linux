// streams a linux zImage + dtb into contiguous phys ram and hands off to the
// kernel (one-way). needs zImage and e6.dtb on E:, or on a memory card: with
// both on the card and on E: it asks which one to boot from
//
// chunk layout: zImage @ +0x00008000 (TEXT_OFFSET), dtb @ +0x02000000.
// The E6 kernel has fixed PHYS_OFFSET=0x10000000; the DTB describes the
// same RAM range. The legacy sentinel patch in the driver does not match it.
// after Boot() the kernel takes over and this never returns (battery pull to
// recover). until there are rapuyama drivers + watchdog handling this wont
// boot visibly, its just the loader ready for when the adapter shows up
#include <e32std.h>
#include <f32file.h>
#include <e32hal.h>
#include <e32svr.h>
#include <u32hal.h>
#include "runblob.h"
#ifdef RB_WLAN_CLOCK
#include "n8nvs.h"
#endif

_LIT(KLog, "E:\\boot.log");
#ifdef RB_IMAGE_BOOT
// e6bfix: the uncompressed kernel (build-vc/arch/arm/boot/Image), it runs in
// place inside the chunk, see AllocBoot in runblob_ldd.cpp
_LIT(KZImage, "Image");
#else
_LIT(KZImage, "zImage");
#endif
// device tree file name (RB_DEFS='-DRB_DTB="n8"' -> n8.dtb), so other phones'
// loaders (n8boot) read their own dtb instead of the e6 one
#ifndef RB_DTB
#define RB_DTB "e6"
#endif
_LIT(KDtb, "" RB_DTB ".dtb"); // _LIT needs a string literal first
// E:\keepscreen.txt: leave symbian's display on at the handoff (see Blank)
_LIT(KKeepScreen, "E:\\keepscreen.txt");

const TUint32 KZOff=0x00008000; // TEXT_OFFSET
#ifdef RB_IMAGE_BOOT
// the Image runs in place up to ~chunk+0x1690000 (_end), the dtb right after
// the kernel keeps it reserved. 32 mb so the chunk can sit high enough
const TUint32 KDtbOff=0x01f00000;
const TUint32 KChunkSz=0x02000000;
#else
const TUint32 KDtbOff=0x02000000; // 32 mb into the chunk
const TUint32 KChunkSz=0x03000000; // 48 mb
#endif
const TInt KBuf=32768;

static RFs gFs;
static RFile gLog;
static void L(const TDesC8& s){TInt p=0; gLog.Seek(ESeekEnd, p); gLog.Write(s); gLog.Flush();}
static void Lf(TRefByValue<const TDesC8> f, ...){
	VA_LIST l;
	VA_START(l, f);
	TBuf8<128> b;
	b.FormatList(f, l);
	VA_END(l);
	L(b);
}

static TInt Ask(const TDesC& aLine1, const TDesC& aLine2, const TDesC& aBut1, const TDesC& aBut2){
	RNotifier n;
	if(n.Connect()!=KErrNone){return KErrGeneral;}
	TInt button=-1;
	TRequestStatus s;
	n.Notify(aLine1, aLine2, aBut1, aBut2, button, s);
	User::WaitForRequest(s);
	n.Close();
	return (s.Int()==KErrNone) ? button : s.Int();
}

static void Fail(const TDesC& aWhat, TInt aErr){
	TBuf<32> e;
	e.Format(_L("error %d"), aErr);
	Ask(aWhat, e, _L("OK"), KNullDesC);
}

// "X:\name"
static TFileName OnDrive(TInt aDrive, const TDesC& aName){
	TChar c;
	RFs::DriveToChar(aDrive, c);
	TFileName f;
	f.Append(c);
	f.Append(_L(":\\"));
	f.Append(aName);
	return f;
}

static TBool Has(const TDesC& aFile){
	TEntry e;
	return gFs.Entry(aFile, e)==KErrNone && !e.IsDir();
}

static TBool HasKernel(TInt aDrive){
	return Has(OnDrive(aDrive, KZImage)) && Has(OnDrive(aDrive, KDtb));
}

// the drive with the kernel files: E: (the N8's internal 16 gb), or the first
// memory card that has them (F: on the N8). asks when both have them
static TInt PickDrive(){
	TDriveList dl;
	if(gFs.DriveList(dl)!=KErrNone){return EDriveE;}
	TInt card=-1;
	for(TInt d=EDriveF; d<EDriveZ; d++){
		TDriveInfo di;
		if(!dl[d] || gFs.Drive(di, d)!=KErrNone){continue;}
		Lf(_L8("drive %c: att=%x media=%d\n"), 'A'+d, di.iDriveAtt, di.iType);
		if(!(di.iDriveAtt&KDriveAttRemovable) || di.iType==EMediaNotPresent){continue;}
		if(HasKernel(d)){card=d; break;}
	}
	if(card<0){return EDriveE;}
	if(!HasKernel(EDriveE)){return card;}
	TBuf<48> m;
	m.Format(_L("memory card %c: or phone E:"), 'A'+card);
	return (Ask(_L("boot linux from"), m, _L("Card"), _L("Phone"))==0) ? card : EDriveE;
}

// switches symbian's display off right before the handoff, so the screen goes
// black instead of freezing on the symbian picture until linux starts the
// panel. needs PowerMgmt (the app has all capabilities)
static void Blank(){
	if(Has(KKeepScreen)){L(_L8("keepscreen: display left on\n")); return;}
	TInt r=UserSvr::HalFunction(EHalGroupDisplay, EDisplayHalSetState, (TAny*)EFalse, NULL);
	Lf(_L8("display off=%d\n"), r);
}

#ifdef RB_WLAN_CLOCK
// N8 WLAN NVS for linux: the helper n8wlnv.exe (same sis) asks wlanhwinit.dll
// for the WL1271 NVS + MAC and converts it (n8nvs.h) into D:\n8wlnv.bin; the
// boot leaves it for linux at phys 0x1f0f6000 (runblob_ldd.cpp, written with
// irqs off right before the jump). block, all u32 little endian:
//   +0 magic 'N8NV' (0x564e384e)  +4 version 1  +8 length (912)
//   +12 crc32 (zlib) of the data  +16 the data (wl127x-nvs.bin)
// linux: linux/initramfs/nvsget.c. no NVS = a zero header there
_LIT(KNvsHelper, "n8wlnv.exe");
_LIT(KNvsBin, "D:\\n8wlnv.bin");
_LIT(KNvsTxt, "D:\\n8wlnv.txt");
const TUint32 KNvsMagic=0x564e384e;
static TUint8 gNvsBlock[16+N8NVS_OUT_LEN];

static void Put32(TUint8* p, TUint32 v){p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24;}

// the helper's status lines into boot.log, then gone from D:
static void CopyHelperLog(){
	RFile f;
	if(f.Open(gFs, KNvsTxt, EFileRead)==KErrNone){
		TBuf8<512> b;
		f.Read(b);
		f.Close();
		L(b);
	}
	gFs.Delete(KNvsTxt);
}

// runs the helper (10 s at most), returns the block length (0 = none)
static TInt WlanNvs(){
	RProcess p;
	TInt r=p.Create(KNvsHelper, KNullDesC);
	if(r==KErrNone){
		TRequestStatus st, ts;
		RTimer t;
		t.CreateLocal();
		p.Logon(st);
		p.Resume();
		t.After(ts, 10000000);
		User::WaitForRequest(st, ts);
		if(st==KRequestPending){
			L(_L8("wlan nvs: helper timed out, killed\n"));
			p.Kill(KErrTimedOut);
			User::WaitForRequest(st);
		}else{
			t.Cancel();
			User::WaitForRequest(ts);
		}
		r=(p.ExitType()==EExitKill) ? p.ExitReason() : KErrDied;
		t.Close();
		p.Close();
	}
	Lf(_L8("wlan nvs: helper n8wlnv=%d\n"), r);
	CopyHelperLog();
	TInt len=0;
	RFile f;
	if(r==KErrNone && f.Open(gFs, KNvsBin, EFileRead)==KErrNone){
		TPtr8 d(gNvsBlock+16, 0, N8NVS_OUT_LEN);
		TInt rr=f.Read(d);
		f.Close();
		if(rr==KErrNone && d.Length()==N8NVS_OUT_LEN){
			Put32(gNvsBlock, KNvsMagic);
			Put32(gNvsBlock+4, 1);
			Put32(gNvsBlock+8, N8NVS_OUT_LEN);
			Put32(gNvsBlock+12, n8nvs_crc32(gNvsBlock+16, N8NVS_OUT_LEN));
			len=16+N8NVS_OUT_LEN;
		}
		Lf(_L8("wlan nvs: read %d bytes (%d)\n"), d.Length(), rr);
	}
	gFs.Delete(KNvsBin);
	return len;
}
#endif

// streams a file into the boot chunk at chunk offset aBase, returns size or <0
static TInt Stream(RRunBlob& aDev, const TDesC& aName, TUint32 aBase, TUint8* aBuf){
	RFile f;
	TInt r=f.Open(gFs, aName, EFileRead);
	if(r!=KErrNone){return r;}
	TInt total=0;
	f.Size(total);
	TUint32 off=0;
	for(;;){
		TPtr8 p(aBuf, 0, KBuf);
		r=f.Read(p);
		if(r!=KErrNone || p.Length()==0){break;}
		r=aDev.LoadChunk(aBase+off, aBuf, (TUint32)p.Length());
		if(r!=KErrNone){f.Close(); return r;}
		TInt was=total ? (TInt)((TInt64)off*10/total) : 0;
		off+=p.Length();
		TInt now=total ? (TInt)((TInt64)off*10/total) : 0;
		if(total>(1<<20) && now!=was){ // big files only, every 10 %
			TBuf<48> t;
			t.Format(_L("loading %S %d %%"), &aName, now*10);
			User::InfoPrint(t);
		}
	}
	f.Close();
	return (r==KErrNone) ? total : r;
}

#ifdef RB_CALIB
// phase calibration, read only (EControlSnoopPoll, no smc, no hooks).
// phase 1: watch securitykext's request descriptor for <window> s and log every
// request with its 32 kHz fast counter -> the period. phase 2: wait for the
// next request, sleep <delay> s, hand off. the ldd puts the counter of that
// request + the counter at the handoff into the isah block, the kernel flight
// recorder keeps logging the counter, tools/parse_calib.py lines them up.
// E:\calib.txt (optional): "<window> <delay>" in seconds, default "330 0"
_LIT(KCalibCfg, "E:\\calib.txt");

static TInt SecsSince(const TTime& aT){
	TTime n;
	n.HomeTime();
	TTimeIntervalSeconds s;
	n.SecondsFrom(aT, s);
	return s.Int();
}

static void ReadCalibCfg(TInt& aWindow, TInt& aDelay){
	aWindow=0; // the ldd reads the secure timer itself, snooping is optional
	aDelay=0;
	RFile f;
	if(f.Open(gFs, KCalibCfg, EFileRead)!=KErrNone){return;}
	TBuf8<64> b;
	f.Read(b);
	f.Close();
	TLex8 lx(b);
	TInt v;
	if(lx.Val(v)==KErrNone && v>0){aWindow=v;}
	lx.SkipSpace();
	if(lx.Val(v)==KErrNone && v>=0){aDelay=v;}
}

// one poll burst. ETrue on a NEW request: the descriptor stays non-zero for a
// moment, so a sighting less than 1 s after the previous one is the same request
static TBool CalibPoll(RRunBlob& aDev, TUint32* aSnap, TUint32& aLastFc, TBool& aHave){
	TPtr8 p((TUint8*)aSnap, 0, 40*4);
	if(aDev.SnoopPoll(p, 0)!=1){return EFalse;}
	TUint32 fc=aSnap[38];
	TBool isNew=!aHave || fc-aLastFc>=32768u;
	aHave=ETrue;
	aLastFc=fc;
	return isNew;
}

static void CalibLog(TInt aN, const TUint32* aSnap, TUint32& aPrevFc, TBool& aHavePrev){
	TUint32 fc=aSnap[38];
	TUint32 dtMs=aHavePrev ? (TUint32)(((TUint64)(fc-aPrevFc)*1000)>>15) : 0;
	Lf(_L8("calib: req %d fc=%08x dt=%u ms cmd=%08x PAid=%08x\n"), aN, fc, dtMs, aSnap[0], aSnap[1]);
	Lf(_L8("  reqphys=%08x reqbuf %08x %08x %08x %08x\n"), aSnap[3], aSnap[22], aSnap[23], aSnap[24], aSnap[25]);
	aPrevFc=fc;
	aHavePrev=ETrue;
}

static void Calibrate(RRunBlob& aDev){
	TInt window=0, delay=0;
	ReadCalibCfg(window, delay);
	Lf(_L8("calib: window %d s, delay after the request %d s\n"), window, delay);
	if(window==0){return;}
	TUint32 snap[40];
	TUint32 lastFc=0, prevFc=0;
	TBool have=EFalse, havePrev=EFalse;
	TInt n=0;
	TTime t;
	t.HomeTime();
	while(SecsSince(t)<window){
		if(CalibPoll(aDev, snap, lastFc, have)){CalibLog(++n, snap, prevFc, havePrev);}
	}
	Lf(_L8("calib: %d requests in the window\n"), n);
	t.HomeTime();
	TBool got=EFalse;
	while(!got && SecsSince(t)<window){
		if(CalibPoll(aDev, snap, lastFc, have)){CalibLog(++n, snap, prevFc, havePrev); got=ETrue;}
	}
	if(!got){
		L(_L8("calib: no request in phase 2, booting anyway (phase unknown)\n"));
		return;
	}
	if(delay>0){
		Lf(_L8("calib: sleeping %d s before the handoff\n"), delay);
		User::After(TTimeIntervalMicroSeconds32(delay*1000000));
	}
}
#endif

GLDEF_C TInt E32Main(){
	if(gFs.Connect()!=KErrNone){return KErrGeneral;}
	if(gLog.Replace(gFs, KLog, EFileWrite)!=KErrNone){gFs.Close(); return KErrGeneral;}
	L(_L8("boot: started\n"));
	L(_L8("boot: alloc v3\n"));
	{
		TMemoryInfoV1Buf mi;
		if(UserHal::MemoryInfo(mi)==KErrNone){
			Lf(_L8("ram total=%dk free=%dk\n"), mi().iTotalRamInBytes>>10, mi().iFreeRamInBytes>>10);
		}
	}

	TInt drive=PickDrive();
	TFileName zimage=OnDrive(drive, KZImage), dtb=OnDrive(drive, KDtb);
	Lf(_L8("boot drive %c:\n"), 'A'+drive);

	User::FreeLogicalDevice(KRunBlobName);
	TInt rLoad=User::LoadLogicalDevice(KRunBlobLdd);
	Lf(_L8("LoadLogicalDevice=%d\n"), rLoad);
	if(rLoad!=KErrNone && rLoad!=KErrAlreadyExists){Fail(_L("can't load the ldd"), rLoad); gLog.Close(); gFs.Close(); return rLoad;}

	RRunBlob dev;
	TInt r=dev.Open();
	if(r!=KErrNone){L(_L8("open failed\n")); Fail(_L("can't open the ldd"), r); gLog.Close(); gFs.Close(); return r;}

#ifdef RB_WLAN_CLOCK
	// n8: the bluetooth uart as SYMBIAN has it right now. We run as a normal
	// symbian app, so with a bluetooth link up this catches the live state of
	// the block; by the time linux sees it the handoff has always left
	// +0x84 = 0 and its transmitter never shifts a bit. Peek() maps uncached,
	// unlike ReadPhys. Read only, a few lines in the log.
	// Opt in with E:\\btdump.txt: a dump that kills the app would otherwise
	// leave no way to boot at all
	if(Has(_L("E:\\btdump.txt"))){
		static const TUint32 KBtRegs[]={
			0x20025a04, 0x20025a08, 0x20025a0c, 0x20025a10, 0x20025a14,
			0x20025a18, 0x20025a1c, 0x20025a20, 0x20025a28, 0x20025a2c,
			0x20025a30, 0x20025a34, 0x20025a38, 0x20025a3c, 0x20025a48,
			0x20025a5c, 0x20025a60, 0x20025a84, 0x20025a88, 0x20025a8c,
			0x20025a9c, 0x20025aa0, 0x20025ab4, 0x20025ab8,
			0x20031864, 0x20031868, 0x2003186c, 0x20031870, 0x20031874,
			0x20031878, 0x2003187c,
			0x20020208, 0x20020308, 0x20020804, 0x200330d0};
		L(_L8("bt regs (symbian live):\n"));
		for(TUint i=0; i<sizeof(KBtRegs)/sizeof(KBtRegs[0]); i+=4){
			TBuf8<96> line;
			for(TUint j=i; j<i+4 && j<sizeof(KBtRegs)/sizeof(KBtRegs[0]); ++j){
				TUint32 v=0;
				TInt pr=dev.Peek(KBtRegs[j], &v);
				TBuf8<24> one;
				if(pr==KErrNone){one.Format(_L8(" %08x=%08x"), KBtRegs[j], v);}
				else{one.Format(_L8(" %08x=err%d"), KBtRegs[j], pr);}
				line.Append(one);
			}
			line.Append(_L8("\n"));
			L(line);
		}
	}
#endif

	TUint8* buf=(TUint8*)User::Alloc(KBuf);
	if(!buf){L(_L8("no mem\n")); Fail(_L("no heap"), KErrNoMemory); dev.Close(); gLog.Close(); gFs.Close(); return KErrNoMemory;}

	TUint32 phys=0;
	for(;;){
		phys=0;
		r=dev.AllocBoot(KChunkSz, phys);
		Lf(_L8("AllocBoot=%d phys=%08x\n"), r, phys);
		if(r==KErrNone){break;}
		TBuf<48> m;
		// on -4 phys = biggest block symbian gave | its size in mb (0 = none)
		if(r==KErrNoMemory && phys){
			m.Format(_L("no ram: best %dmb@%08x"), phys&0xff, phys&~0xfffffu);
		}else{
			m.Format(_L("no free ram block (%d)"), r);
		}
		if(Ask(m, _L("close apps and retry?"), _L("Retry"), _L("Cancel"))!=0){
			User::Free(buf); dev.Close(); gLog.Close(); gFs.Close();
			return r;
		}
	}

	TInt zsz=Stream(dev, zimage, KZOff, buf);
	Lf(_L8("zImage stream=%d\n"), zsz);
	TInt dsz=Stream(dev, dtb, KDtbOff, buf);
	Lf(_L8("dtb stream=%d\n"), dsz);
	User::Free(buf);

	if(zsz<=0 || dsz<=0){
		L(_L8("missing zImage/dtb -- aborting\n"));
		Fail((zsz<=0) ? zimage : dtb, (zsz<=0) ? zsz : dsz);
		dev.Close(); gLog.Close(); gFs.Close();
		return KErrNotFound;
	}

#ifdef RB_CALIB
	Calibrate(dev);
#endif
	TBuf<48> m;
	m.Format(_L("ram %08x, kernel %d kb"), phys, zsz>>10);
	if(Ask(m, _L("boot linux?"), _L("Boot"), _L("Cancel"))!=0){
		L(_L8("cancelled\n"));
		dev.Close(); gLog.Close(); gFs.Close();
		return KErrCancel;
	}
#ifdef RB_CALIB
	// boot right after the inferred 210 s phase. tthere should be a countdown
	// but it doesn't work and i don't wanna fix it
	{
		const TUint32 KPeriod=210000, KPhase=900;
		TUint32 now=User::NTickCount();
		TUint32 target=((now+KPhase)/KPeriod+1)*KPeriod-KPhase;
		for(;;){
			TInt left=(TInt)(target-User::NTickCount());
#ifdef RB_TIMER_CAPTURE
			if(left<=6000){break;}
#else
			if(left<=3000){break;}
#endif
			TBuf<48> t;
			t.Format(_L("linux in %d s"), left/1000);
			User::InfoPrint(t);
			User::After(2000000);
		}
		User::InfoPrint(_L("booting linux"));
#ifdef RB_TIMER_CAPTURE
		// allocate before sampling; write the file after the service window
		Lf(_L8("timer capture start tick=%u\n"), User::NTickCount());
		const TInt bytes=(8+36*(16+512*32))*4;
		RChunk capture;
		TInt rs=capture.CreateLocal(bytes, bytes);
		if(rs!=KErrNone){
			Fail(_L("timer capture allocation"), rs);
			dev.Close(); gLog.Close(); gFs.Close();
			return rs;
		}
		TPtr8 p(capture.Base(), 0, bytes);
		rs=dev.WatchService(p);
		Lf(_L8("WatchService=%d bytes=%d\n"), rs, p.Length());
		if(rs==KErrNone){
			RFile f;
			rs=f.Replace(gFs, _L("E:\\timers.bin"), EFileWrite);
			if(rs==KErrNone){rs=f.Write(p); f.Close();}
		}
		capture.Close();
		if(rs!=KErrNone){
			Fail(_L("timer capture"), rs);
			dev.Close(); gLog.Close(); gFs.Close();
			return rs;
		}
#else
		TBuf8<4> none;
		TInt rs=dev.WatchService(none);
		Lf(_L8("WatchService=%d\n"), rs);
#endif
	}
#endif
#ifdef RB_WLAN_CLOCK
	// n8: WL1271 reference clock (PRM 0x25) on + WLANENABLE high, so linux
	// finds the chip powered and clocked. only logged, the boot goes on
	// on by default, E:\\nowlan.txt skips it (linux then finds the WL1271 off
	// and unclocked). one step per call, logged before and after, so a crash
	// in symbian's power code leaves the step it died in last
	if(Has(_L("E:\\nowlan.txt"))){
		L(_L8("wlan clock: skipped (nowlan.txt)\n"));
		dev.NvsStore(KNullDesC8);
	}else{
		// the phone's own NVS for linux, first: wlanhwinit may switch the
		// WLAN power itself, the clock steps below set the final state
		TInt nvs=WlanNvs();
		TInt rs=dev.NvsStore(TPtrC8(gNvsBlock, nvs));
		Lf(_L8("wlan nvs: %d byte block for linux (%d)\n"), nvs, rs);
		const TInt steps[3]={1, 2, 4};
		const char* names[3]={"register", "clock 0x25 on", "genio100 high"};
		TInt done=0;
		for(TInt i=0; i<3; i++){
			TUint32 w[5]={0, 0, 0, 0, 0};
			TPtr8 p((TUint8*)w, 0, sizeof(w));
			Lf(_L8("wlan clock: %s ...\n"), names[i]);
			TInt rw=dev.WlanClock(p, steps[i]);
			Lf(_L8("wlan clock: %s = %d (client %x gpio out %08x dir %08x)\n"),
				names[i], rw, w[1], w[3], w[4]);
			if(rw!=KErrNone){break;}
			done++;
		}
		// the clock request goes to the modem as a message: give it time to
		// act before symbian is gone
		if(done>=2){
			User::After(500000);
			L(_L8("wlan clock: waited 500 ms\n"));
		}
	}
#endif
	Blank();
	L(_L8("about-to-Boot (one-way)\n"));
	gLog.Close(); gFs.Close();
	dev.Boot(KZOff, KDtbOff, 0xFFFFFFFFu, 0); // never returns
	return KErrNone;
}