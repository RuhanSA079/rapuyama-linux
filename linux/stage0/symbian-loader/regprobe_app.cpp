// reads a list of regs to check the reversed drivers before linux, ONE reg per
// driver call and crash resilient like scan.exe: before each read a record
// {addr, 0, pending} is appended to E:\regprobe3.bin and flushed, after the read
// it gets the value + ok. a reg that kills the phone (external abort, clock
// gated block...) stays "pending" and the next run carries on after it.
// rerun until E:\regprobe3.log says DONE. list goes from regs the firmware reads
// itself (safe) to regs it never reads (risky). tools/parse_regprobe.py
// v2: also writes (clock gates on before reading a gated block)
// v3: real gate ids, skips the rest of a block after a crash, files regprobe3.*
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\regprobe3.log");
_LIT(KBin, "E:\\regprobe3.bin");
const TUint32 KOk=0x600d600du;
const TUint32 KPending=0xbad0bad0u;
const TUint32 KWrote=0x3017e000u;
const TUint32 KSkipped=0x5c1bbed0u;

// {c6 virt, value to write, 1 = write / 0 = read, block}. after a crash the rest
// of that block is skipped (one reboot per dead block, not one per reg)
struct TOp{TUint32 iAddr; TUint32 iVal; TUint32 iWrite; TUint32 iBlock;};
static const TOp KRegs[]={
	// run 2 (13.10) proved my gate ids were wrong: the PowerResourceManager ids
	// map to hw gates through a table (power_resources 0x800cbee8). i2c0.dll asks
	// for 0x11 = EHwClkI2C1 = gates 0x7 (0x200330d0 bit 6) + 0x12 (0x200330b0 bit 4),
	// the bt pdd for 0x1f = EHwClkBB38M4AnaClk1 = gate 0x22 (0x200330d0 bit 12).
	// all polarity 1 -> on = write the bits to R+4
	// block 1: bt uart with its clock on
	{0xc60330d4u, 0x1000, 1, 1},
	{0xc6025a04u, 0, 0, 1}, {0xc6025a84u, 0, 0, 1}, {0xc6025a9cu, 0, 0, 1}, {0xc6025aa0u, 0, 0, 1},
	{0xc6025a2cu, 0, 0, 1}, {0xc6025a34u, 0, 0, 1}, {0xc6025a38u, 0, 0, 1},
	{0xc6025a8cu, 0, 0, 1}, {0xc6025a88u, 0, 0, 1}, {0xc6025a08u, 0, 0, 1}, {0xc6025a0cu, 0, 0, 1},
	{0xc6025a10u, 0, 0, 1}, {0xc6025a5cu, 0, 0, 1}, {0xc6025a60u, 0, 0, 1}, {0xc6025ab8u, 0, 0, 1},
	{0xc6025a14u, 0, 0, 1}, {0xc6025a18u, 0, 0, 1}, {0xc6025a1cu, 0, 0, 1}, {0xc6025a3cu, 0, 0, 1},
	// block 2: i2c with both gates on
	{0xc60330d4u, 0x40, 1, 2}, {0xc60330b4u, 0x10, 1, 2},
	{0xc6032200u, 0, 0, 2}, {0xc6032224u, 0, 0, 2}, {0xc6032208u, 0, 0, 2}, {0xc6032234u, 0, 0, 2},
	{0xc6032238u, 0, 0, 2}, {0xc6032228u, 0, 0, 2}, {0xc6032230u, 0, 0, 2}, {0xc6032204u, 0, 0, 2},
	{0xc603222cu, 0, 0, 2},
	// block 3: i2c regs the firmware never touches
	{0xc6032210u, 0, 0, 3}, {0xc6032220u, 0, 0, 3}, {0xc603223cu, 0, 0, 3},
};
const TInt KN=sizeof(KRegs)/sizeof(KRegs[0]);

static void Log(RFs& aFs, const TDesC8& aMsg){
	RFile f;
	TInt r=f.Open(aFs, KLog, EFileWrite);
	if(r==KErrNotFound){r=f.Create(aFs, KLog, EFileWrite);}
	if(r!=KErrNone){return;}
	TInt pos=0;
	f.Seek(ESeekEnd, pos);
	f.Write(aMsg);
	f.Flush();
	f.Close();
}

GLDEF_C TInt E32Main(){
	RFs fs;
	if(fs.Connect()!=KErrNone){return KErrGeneral;}
	TBuf8<128> b;

	// how far did earlier runs get
	RFile data;
	TInt r=data.Open(fs, KBin, EFileWrite);
	if(r==KErrNotFound){r=data.Create(fs, KBin, EFileWrite);}
	if(r!=KErrNone){fs.Close(); return r;}
	TInt size=0;
	data.Size(size);
	TInt start=size/12;
	// the last record still pending = it killed the phone, skip the rest of its block
	if(start>0 && start<=KN){
		TUint32 last[3];
		TPtr8 lp((TUint8*)last, 0, sizeof(last));
		TInt lpos=(start-1)*12;
		data.Seek(ESeekStart, lpos);
		if(data.Read(lp)==KErrNone && lp.Length()==12 && last[2]==KPending){
			TUint32 blk=KRegs[start-1].iBlock;
			while(start<KN && KRegs[start].iBlock==blk){
				TUint32 sk[3]={KRegs[start].iAddr, 0, KSkipped};
				TPtrC8 skp((const TUint8*)sk, sizeof(sk));
				TInt spos=start*12;
				data.Seek(ESeekStart, spos);
				data.Write(skp);
				start++;
			}
			data.Flush();
		}
	}
	b.Format(_L8("run: start at %d of %d\n"), start, KN);
	Log(fs, b);

	if(start>=KN){
		Log(fs, _L8("DONE (all done, delete E:\\regprobe3.bin to start over)\n"));
		data.Close(); fs.Close();
		return KErrNone;
	}

	User::FreeLogicalDevice(KRunBlobName);
	_LIT(KLdd, "runblob.ldd");
	TInt rLoad=User::LoadLogicalDevice(KLdd);
	b.Format(_L8("Load=%d\n"), rLoad);
	Log(fs, b);

	if(rLoad==KErrNone || rLoad==KErrAlreadyExists){
		RRunBlob dev;
		if(dev.Open()==KErrNone){
			// a reboot undid the earlier writes (clock gates), redo the ones of the
			// block we resume in
			for(TInt j=0; j<start && j<KN; ++j){
				if(KRegs[j].iBlock!=KRegs[start<KN ? start : KN-1].iBlock){continue;}
				if(KRegs[j].iWrite){dev.RegWrite(KRegs[j].iAddr, KRegs[j].iVal);}
			}
			for(TInt i=start; i<KN; ++i){
				// 1. pending record, on disk BEFORE the access
				TUint32 rec[3]={KRegs[i].iAddr, KRegs[i].iVal, KPending};
				TPtrC8 rp((const TUint8*)rec, sizeof(rec));
				TInt pos=i*12;
				data.Seek(ESeekStart, pos);
				data.Write(rp);
				data.Flush();
				// 2. the access (this is where a bad reg kills the phone)
				TInt rr;
				if(KRegs[i].iWrite){
					rr=dev.RegWrite(KRegs[i].iAddr, KRegs[i].iVal);
					rec[2]=(rr==KErrNone) ? KWrote : (TUint32)rr;
				}
				else{
					TPtrC8 addr((const TUint8*)&KRegs[i].iAddr, 4);
					TBuf8<4> val;
					rr=dev.RegProbe(addr, val);
					// 3. fill it in
					rec[1]=(val.Length()==4) ? *(const TUint32*)val.Ptr() : 0;
					rec[2]=(rr==KErrNone) ? KOk : (TUint32)rr;
				}
				pos=i*12;
				data.Seek(ESeekStart, pos);
				data.Write(rp);
				data.Flush();
			}
			dev.Close();
			Log(fs, _L8("DONE\n"));
		}
	}
	data.Close();
	fs.Close();
	return KErrNone;
}