// ro dump of the live variant ASIC object + its vtable to resolve the
// modem power/clock/reset method statically-unreachable through the factory
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\asic.log");
_LIT(KBin, "E:\\asic.bin");

const TUint32 KAsicPtr = 0xc8000018u; // holds the TheAsic object pointer
const TUint32 KCfg1 = 0xc800a6f4u; // arg2 to fn50 (modem power config)
const TUint32 KCfg2 = 0xc800a6e8u; // arg3 to fn50

static RFile gLog, gBin;
static void L(const TDesC8& m){ gLog.Write(m); gLog.Flush(); }

static void Hex(RRunBlob& dev, const TDesC8& tag, TUint32 va, TInt n){
	TBuf8<96> t; t.Copy(tag); t.AppendFormat(_L8(" @%08x\n"), va); L(t);
	TUint8 buf[256]; if(n>256){n=256;}
	TPtr8 p(buf, 0, n);
	TInt r=dev.ReadVirt(va, p);
	if(r<0){ TBuf8<48> e; e.Format(_L8("  ReadVirt=%d\n"), r); L(e); return; }
	gBin.Write(TPtrC8(buf, n)); gBin.Flush();
	TBuf8<96> b;
	for(TInt i=0; i<n; i+=16){
		b.Zero(); b.AppendFormat(_L8("  +%03x:"), i);
		for(TInt j=0; j<16 && i+j<n; ++j){ b.AppendFormat(_L8(" %02x"), buf[i+j]); }
		b.Append(_L8("\n")); L(b);
	}
}

static TUint32 Word(RRunBlob& dev, TUint32 va){
	TUint32 w=0; TPtr8 p((TUint8*)&w, 0, 4); dev.ReadVirt(va, p); return w;
}

GLDEF_C TInt E32Main(){
	RFs fs;
	if(fs.Connect()!=KErrNone){return KErrGeneral;}
	if(gLog.Replace(fs, KLog, EFileWrite)!=KErrNone){fs.Close(); return KErrGeneral;}
	if(gBin.Replace(fs, KBin, EFileWrite)!=KErrNone){gLog.Close(); fs.Close(); return KErrGeneral;}
	L(_L8("asicdump: read-only variant ASIC vtable + cfg dump\n"));

	User::FreeLogicalDevice(KRunBlobName);
	TInt rLoad=User::LoadLogicalDevice(KRunBlobLdd);
	TBuf8<96> b; b.Format(_L8("Load=%d\n"), rLoad); L(b);
	if(rLoad!=KErrNone && rLoad!=KErrAlreadyExists){gBin.Close(); gLog.Close(); fs.Close(); return rLoad;}

	RRunBlob dev;
	if(dev.Open()!=KErrNone){gBin.Close(); gLog.Close(); fs.Close(); return KErrGeneral;}

	TUint32 asic=Word(dev, KAsicPtr);
	b.Format(_L8("\n*(%08x) asic obj = %08x\n"), KAsicPtr, asic); L(b);
	if(asic<0x80000000u){ L(_L8("asic ptr not a kernel address, abort\n")); dev.Close(); gBin.Close(); gLog.Close(); fs.Close(); return KErrNone; }

	Hex(dev, _L8("asic object head"), asic, 0x60);

	TUint32 vt=Word(dev, asic);
	b.Format(_L8("\nvtable = *asic = %08x\n"), vt); L(b);
	if(vt<0x80000000u){ L(_L8("vtable ptr not a rom address, abort\n")); dev.Close(); gBin.Close(); gLog.Close(); fs.Close(); return KErrNone; }

	// full vtable: 0x24 entries (0x90 bytes) covers +0x50 (index 0x14) and more
	Hex(dev, _L8("vtable"), vt, 0x90);

	TUint32 fn50=Word(dev, vt+0x50);
	TUint32 fn18=Word(dev, vt+0x18);
	TUint32 fn1c=Word(dev, vt+0x1c);
	TUint32 fn28=Word(dev, vt+0x28);
	b.Format(_L8("\n*** vtable[+0x50] = %08x  <= MODEM power/clock/reset fn\n"), fn50); L(b);
	b.Format(_L8("    vtable[+0x18] = %08x\n    vtable[+0x1c] = %08x\n    vtable[+0x28] = %08x\n"),
		fn18, fn1c, fn28); L(b);

	L(_L8("\n")); Hex(dev, _L8("cfg1 (fn50 arg2)"), KCfg1, 0x40);
	L(_L8("\n")); Hex(dev, _L8("cfg2 (fn50 arg3)"), KCfg2, 0x40);

	L(_L8("\ndone - send E:\\asic.log + asic.bin\n"));
	dev.Close(); gBin.Close(); gLog.Close(); fs.Close();
	return KErrNone;
}