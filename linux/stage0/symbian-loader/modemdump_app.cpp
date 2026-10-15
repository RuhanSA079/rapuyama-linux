// READ-ONLY dump of the modem (CMT) control block, step 1 of reversing the
// on-chip modem link that resets us. reversed from sisa_wrapper FUN_80104700:
// the block is at *(Kern::SuperPage() + 0x2c); within it +0x20 = the modem image
// base (first word magic 0x809795a3), +0x6c = the reset-request field written by
// SisaResetGenerate, +0x78..0x84 = the modem entry args. we dump the superpage
// head, the whole block, and the modem image header so we can map the shared
// memory transport (doorbell / queues / supervision fields). all via ReadVirt
// (Kern::SafeRead per page) + VirtInfo = read-only, safe.
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\modem.log");
_LIT(KBin, "E:\\modem.bin");

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
	L(_L8("modemdump: read-only CMT control block dump\n"));

	User::FreeLogicalDevice(KRunBlobName);
	TInt rLoad=User::LoadLogicalDevice(KRunBlobLdd);
	TBuf8<96> b; b.Format(_L8("Load=%d\n"), rLoad); L(b);
	if(rLoad!=KErrNone && rLoad!=KErrAlreadyExists){gBin.Close(); gLog.Close(); fs.Close(); return rLoad;}

	RRunBlob dev;
	if(dev.Open()!=KErrNone){gBin.Close(); gLog.Close(); fs.Close(); return KErrGeneral;}

	TUint32 info[2];
	dev.VirtInfo(0x80000000u, info); // info[0] = Kern::SuperPage()
	TUint32 sp=info[0];
	b.Format(_L8("SuperPage=%08x\n"), sp); L(b);
	if(sp<0x80000000u){ L(_L8("bad superpage\n")); dev.Close(); gBin.Close(); gLog.Close(); fs.Close(); return KErrNone; }

	Hex(dev, _L8("superpage head"), sp, 0x40);
	TUint32 block=Word(dev, sp+0x2c);
	b.Format(_L8("\nmodem block (*(SuperPage+0x2c)) = %08x\n"), block); L(b);
	if(block>=0x80000000u || (block>=0xc0000000u)){
		Hex(dev, _L8("modem block"), block, 0x100);
		TUint32 img=Word(dev, block+0x20);
		TUint32 reset=Word(dev, block+0x6c);
		b.Format(_L8("\n  image(+0x20)=%08x  resetfield(+0x6c)=%08x\n"), img, reset); L(b);
		b.Format(_L8("  entryargs +78=%08x +7c=%08x +80=%08x +84=%08x\n"),
			Word(dev, block+0x78), Word(dev, block+0x7c),
			Word(dev, block+0x80), Word(dev, block+0x84)); L(b);
		if(img>=0x80000000u || img>=0xc0000000u){
			Hex(dev, _L8("modem image head"), img, 0x40);
			TUint32 magic=Word(dev, img);
			b.Format(_L8("  image magic=%08x (expect 809795a3)\n"), magic); L(b);
		}
	}
	else{
		b.Format(_L8("block ptr not a kernel address\n")); L(b);
	}

	L(_L8("done\n"));
	dev.Close();
	gBin.Close(); gLog.Close(); fs.Close();
	return KErrNone;
}