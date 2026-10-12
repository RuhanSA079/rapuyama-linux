// peripheral register scanner for the e6 that survives crashes.

// reading unknown mmio can hit an external abort that cant be trapped -> instant
// reboot. so it reads ONE page per driver call and saves progress around every
// call: "next index" gets saved (and flushed) BEFORE each read, the data gets
// appended (and flushed) AFTER
#include <e32std.h>
#include <f32file.h>
#include "ptdump.h"

_LIT(KLddFile, "ptdump.ldd");
_LIT(KIdx, "E:\\scanidx.bin");
_LIT(KData, "E:\\scandata.bin");
_LIT(KLog, "E:\\scan.log");

const TInt KWordsPerPage=16; // first 16 regs (0x00..0x3C) per page
const TUint32 KDone=0xFFFFFFFFu;
const TInt KMaxPages=512;

static TUint32 ReadIdx(RFs& aFs){
	RFile f;
	if(f.Open(aFs, KIdx, EFileRead)!=KErrNone){return 0;}
	TBuf8<4> b;
	f.Read(b, 4);
	f.Close();
	if(b.Length()<4){return 0;}
	return *(const TUint32*)b.Ptr();
}

static void WriteIdx(RFs& aFs, TUint32 aIdx){
	RFile f;
	if(f.Replace(aFs, KIdx, EFileWrite)!=KErrNone){return;}
	TPtrC8 p((const TUint8*)&aIdx, 4);
	f.Write(p);
	f.Flush();
	f.Close();
}

GLDEF_C TInt E32Main(){
	RFs fs;
	if(fs.Connect()!=KErrNone){return KErrGeneral;}

	User::FreeLogicalDevice(KPtDumpName);
	TInt rLoad=User::LoadLogicalDevice(KLddFile);
	if(rLoad!=KErrNone && rLoad!=KErrAlreadyExists){fs.Close(); return rLoad;}

	RPtDump dev;
	if(dev.Open()!=KErrNone){fs.Close(); return KErrGeneral;}

	// 1. grab the page map (no mmio, safe)
	TUint8* mapmem=(TUint8*)User::Alloc(KPtBufSize);
	if(!mapmem){dev.Close(); fs.Close(); return KErrNoMemory;}
	TPtr8 mapbuf(mapmem, 0, KPtBufSize);
	TInt rMap=dev.ReadL1(mapbuf);
	const TUint32* h=(const TUint32*)mapbuf.Ptr();
	TInt nRuns=(rMap==KErrNone && mapbuf.Length()>=20) ? (TInt)h[4] : 0;

	// always dump the raw map to disk (incl the unfiltered 0xc6 window sweep)
	{
		_LIT(KMap, "E:\\map.bin");
		RFile mf;
		if(mf.Replace(fs, KMap, EFileWrite)==KErrNone){
			mf.Write(mapbuf);
			mf.Flush();
			mf.Close();
		}
	}

	// 2. build the page list: 0x40100000 bank first, then the other
	// peripheral banks. boot rom (phys 0) gets skipped
	TUint32* pv=(TUint32*)User::Alloc(KMaxPages*4); // page virt
	TUint32* pp=(TUint32*)User::Alloc(KMaxPages*4); // page phys
	TInt nPages=0;
	for(TInt pass=0; pass<2; ++pass){
		for(TInt ri=0; ri<nRuns; ++ri){
			TUint32 rv=h[16+ri*3+0];
			TUint32 rp=h[16+ri*3+1];
			TUint32 rg=h[16+ri*3+2];
			if(rp==0){continue;} // skip boot rom
			TBool isMain=(rp==0x40100000u);
			if(pass==0 && !isMain){continue;} // pass 0: main bank only
			if(pass==1 && isMain){continue;} // pass 1: everything else
			for(TUint32 p=0; p<rg && nPages<KMaxPages; ++p){
				pv[nPages]=rv+p*0x1000u;
				pp[nPages]=rp+p*0x1000u;
				nPages++;
			}
		}
	}

	// 3. resume index
	TUint32 idx=ReadIdx(fs);

	// if the page scan is already DONE, this run does the Kern::Printf-diff
	// uart hunt over the LIVE peripheral runs instead (safe, those pages decode).
	// per page: snapshot A, spam the debug uart, snapshot B -> E:\pdiff.bin
	// (record = [phys][A0..15][B0..15], 33 words). A vs B gets diffed offline
	if(idx==KDone){
		_LIT(KPdiff, "E:\\pdiff.bin");
		RFile pf;
		if(pf.Replace(fs, KPdiff, EFileWrite)==KErrNone){
			TInt npd=0;
			for(TInt ri=0; ri<nRuns; ++ri){
				TUint32 rv=h[16+ri*3+0];
				TUint32 rp=h[16+ri*3+1];
				TUint32 rg=h[16+ri*3+2];
				if(rp==0){continue;} // skip boot rom
				if(rp==0x40100000u){continue;} // skip the dead bank
				for(TUint32 p=0; p<rg; ++p){
					TBuf8<KWordsPerPage*8> ab;
					dev.PrintfDiff(rv+p*0x1000u, KWordsPerPage, ab);
					TUint32 rec[1+KWordsPerPage*2];
					rec[0]=rp+p*0x1000u;
					const TUint32* wd=(const TUint32*)ab.Ptr();
					for(TInt k=0; k<KWordsPerPage*2; ++k){
						rec[1+k]=(ab.Length()>=(k+1)*4) ? wd[k] : 0;
					}
					TPtrC8 rpd((const TUint8*)rec, sizeof(rec));
					pf.Write(rpd);
					pf.Flush();
					npd++;
				}
			}
			pf.Close();
			TBuf<64> n2;
			n2.Format(_L("pdiff DONE pages=%d"), npd);
			User::InfoPrint(n2);
		}
		dev.Close();
		User::Free(pv); User::Free(pp); User::Free(mapmem);
		fs.Close();
		return KErrNone;
	}

	// open the data file for append
	RFile data;
	if(data.Open(fs, KData, EFileWrite)!=KErrNone){data.Replace(fs, KData, EFileWrite);}
	TInt endpos=0;
	data.Seek(ESeekEnd, endpos);

	// 4. the actual scan loop
	TBuf8<KWordsPerPage*4> win;
	TInt done=0;
	for(; idx<(TUint32)nPages; ++idx){
		WriteIdx(fs, idx+1); // save BEFORE read
		win.Zero();
		TInt rc=dev.ReadWin(pv[idx], KWordsPerPage, win);

		TUint32 rec[4+KWordsPerPage];
		rec[0]=idx; rec[1]=pp[idx]; rec[2]=pv[idx]; rec[3]=(TUint32)rc;
		const TUint32* wd=(const TUint32*)win.Ptr();
		for(TInt k=0; k<KWordsPerPage; ++k){
			rec[4+k]=(win.Length()>=(k+1)*4) ? wd[k] : 0;
		}
		TPtrC8 rp((const TUint8*)rec, sizeof(rec));
		data.Write(rp);
		data.Flush(); // save AFTER read
		done++;
	}

	WriteIdx(fs, KDone);
	data.Close();

	TBuf8<128> log;
	log.AppendFormat(_L8("scan nRuns=%d nPages=%d scanned_this_run=%d DONE\n"), nRuns, nPages, done);
	RFile lf;
	if(lf.Replace(fs, KLog, EFileWrite)==KErrNone){lf.Write(log); lf.Close();}

	TBuf<64> note;
	note.Format(_L("scan DONE pages=%d run=%d"), nPages, done);
	User::InfoPrint(note);

	dev.Close();
	User::Free(pv); User::Free(pp); User::Free(mapmem);
	fs.Close();
	return KErrNone;
}