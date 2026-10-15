// reads the persistent boot/reset-reason register after a reset, so we can tell
// what reset the chip. rebootdriver.ldd reversed: phys 0x20040400 (c6 virt
// 0xc6040400), bits [23:20] = reason nibble, survives the reset. firmware decode
// (FUN_800ccf6c): nibble 0->code1 0xe->2 0xf->3 0x8->4 0xa->5 0xb->6 = the sw
// reboot reasons. a value outside that set after one of our death-resets would
// mean the hw latched a different (secure/wd) source there.
// workflow: run once after a NORMAL symbian boot to get the baseline nibble,
// then after a linux/kickloop death-reset, and diff E:\e6rst.log. all reads go
// through the c6 window (EControlRegProbe), the same regs the firmware reads, so
// this is safe (no new mmio, no writes)
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\e6rst.log");

// c6-window regs to snapshot. phys 0x20000000 -> c6 virt 0xc6000000
struct TReg{TUint32 iAddr; const char* iName;};
static const TReg KRegs[]={
	{0xc6040400u, "bootreason 0x20040400 (nibble [23:20])"},
	{0xc6040404u, "0x20040404"},
	{0xc6040408u, "0x20040408"},
	{0xc6040280u, "sisa status 0x20040280"},
	{0xc6040284u, "sisa set 0x20040284"},
	{0xc6040288u, "sisa clear 0x20040288"},
	{0xc6033114u, "reboot reg 0x20033114"},
	{0xc6033194u, "trace/crashlog 0x20033194"},
	{0xc60336c4u, "soc wdt counter 0x200336c4"},
};
const TInt KN=sizeof(KRegs)/sizeof(KRegs[0]);

// the firmware's nibble->reason decode, for the humans reading the log
static const char* ReasonName(TUint32 nib){
	switch(nib){
		case 0x0: return "code1 (cold/power-on?)";
		case 0xe: return "code2";
		case 0xf: return "code3";
		case 0x8: return "code4";
		case 0xa: return "code5";
		case 0xb: return "code6";
		default:  return "NOT a known sw reason (hw/secure/wd latch?)";
	}
}

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
	Log(fs, _L8("\n=== e6rst run ===\n"));

	User::FreeLogicalDevice(KRunBlobName);
	TInt rLoad=User::LoadLogicalDevice(KRunBlobLdd);
	b.Format(_L8("Load=%d\n"), rLoad);
	Log(fs, b);
	if(rLoad!=KErrNone && rLoad!=KErrAlreadyExists){fs.Close(); return rLoad;}

	RRunBlob dev;
	if(dev.Open()!=KErrNone){fs.Close(); return KErrGeneral;}

	// read all regs in one RegProbe call
	TUint32 addrs[16];
	for(TInt i=0; i<KN; ++i){addrs[i]=KRegs[i].iAddr;}
	TPtrC8 ap((const TUint8*)addrs, KN*4);
	TUint32 vals[16];
	TPtr8 vp((TUint8*)vals, 0, KN*4);
	TInt rr=dev.RegProbe(ap, vp);
	b.Format(_L8("RegProbe=%d got %d words\n"), rr, vp.Length()/4);
	Log(fs, b);

	if(rr==KErrNone){
		for(TInt i=0; i<KN && i<vp.Length()/4; ++i){
			TBuf8<80> nm; nm.Copy(TPtrC8((const TUint8*)KRegs[i].iName));
			b.Format(_L8("  %08x = %08x  "), KRegs[i].iAddr, vals[i]);
			b.Append(nm); b.Append(_L8("\n"));
			Log(fs, b);
		}
		TUint32 nib=(vals[0]>>20)&0xf;
		TBuf8<64> rn; rn.Copy(TPtrC8((const TUint8*)ReasonName(nib)));
		b.Format(_L8("REASON nibble = 0x%x -> "), nib);
		b.Append(rn); b.Append(_L8("\n"));
		Log(fs, b);
	}

	dev.Close();
	fs.Close();
	return KErrNone;
}