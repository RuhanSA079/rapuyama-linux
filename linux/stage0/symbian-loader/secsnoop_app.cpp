// READ-ONLY snoop of securitykext's secure-monitor channel while symbian is
// alive, to capture the real request it sends over smc #0 (the type-9 keepalive
// payload is registered at runtime, so it can't be recovered statically).
// chain reversed from securitykext: channel object at kernel virt 0xc800d1c4;
//   obj[0]  = shared page va (0xc670c000 = phys 0x1ffec000)
//   obj[5]  (+0x14) = transport fn ptr (the smc stub)
//   obj[6]  (+0x18) = current request descriptor ptr (kernel virt)
//   obj[7]  (+0x1c) = where the descriptor addr is published
// descriptor (6 words): [0]=0x3f cmd [1]=PA id [3]=phys(reqbuf) [4]=phys(respbuf)
//   [5]=flag; reqbuf+2 byte = request subtype.
// all reads go through ReadVirt/ReadPhys (no writes, no smc) = safe. the shared
// page retains the last request, so a dump shows the format even between calls.
// dumps raw to E:\secsnoop.bin and decoded fields to E:\secsnoop.log.
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\secsnoop.log");
_LIT(KBin, "E:\\secsnoop.bin");
const TUint32 KObj=0xc800d1c4u; // secure channel object (kernel virt)
const TUint32 KShared=0x1ffec000u; // shared page (phys)

static RFile gLog, gBin;

static void L(const TDesC8& m){ gLog.Write(m); gLog.Flush(); }

static void Hex(const TDesC8& tag, const TUint8* p, TInt n){
	TBuf8<96> b;
	b.Copy(tag); b.Append(_L8("\n")); L(b);
	for(TInt i=0; i<n; i+=16){
		b.Zero();
		b.AppendFormat(_L8("  +%02x:"), i);
		for(TInt j=0; j<16 && i+j<n; ++j){ b.AppendFormat(_L8(" %02x"), p[i+j]); }
		b.Append(_L8("\n")); L(b);
	}
	gBin.Write(TPtrC8(p, n)); gBin.Flush();
}

GLDEF_C TInt E32Main(){
	RFs fs;
	if(fs.Connect()!=KErrNone){return KErrGeneral;}
	if(gLog.Replace(fs, KLog, EFileWrite)!=KErrNone){fs.Close(); return KErrGeneral;}
	if(gBin.Replace(fs, KBin, EFileWrite)!=KErrNone){gLog.Close(); fs.Close(); return KErrGeneral;}
	L(_L8("secsnoop: read-only secure-channel dump\n"));

	User::FreeLogicalDevice(KRunBlobName);
	TInt rLoad=User::LoadLogicalDevice(KRunBlobLdd);
	TBuf8<96> b;
	b.Format(_L8("Load=%d\n"), rLoad); L(b);
	if(rLoad!=KErrNone && rLoad!=KErrAlreadyExists){gBin.Close(); gLog.Close(); fs.Close(); return rLoad;}

	RRunBlob dev;
	if(dev.Open()!=KErrNone){gBin.Close(); gLog.Close(); fs.Close(); return KErrGeneral;}

	TUint32 obj[16];
	TPtr8 op((TUint8*)obj, 0, sizeof(obj));
	TInt r=dev.ReadVirt(KObj, op);
	b.Format(_L8("ReadVirt(obj)=%d\n"), r); L(b);
	Hex(_L8("channel object @c800d1c4"), (TUint8*)obj, 0x40);

	TUint32 shared=obj[0], tfn=obj[5], descp=obj[6], pub=obj[7];
	b.Format(_L8("sharedpage=%08x  transportfn=%08x  descptr=%08x  pub=%08x\n"),
		shared, tfn, descp, pub); L(b);

	// the request descriptor (6 words) at obj[6]
	if(descp>=0x80000000u){
		TUint32 desc[8];
		TPtr8 dp((TUint8*)desc, 0, 0x18);
		r=dev.ReadVirt(descp, dp);
		b.Format(_L8("ReadVirt(desc)=%d\n"), r); L(b);
		Hex(_L8("descriptor"), (TUint8*)desc, 0x18);
		b.Format(_L8("  cmd=%08x PAid=%08x w2=%08x reqphys=%08x respphys=%08x flag=%08x\n"),
			desc[0], desc[1], desc[2], desc[3], desc[4], desc[5]); L(b);
		// the actual request + response buffers (phys)
		if(desc[3]){
			TUint8 req[256]; TPtr8 rp(req, 0, sizeof(req));
			r=dev.ReadPhys(desc[3], rp);
			b.Format(_L8("ReadPhys(reqbuf %08x)=%d  subtype=0x%02x\n"),
				desc[3], r, (r==KErrNone? req[2] : 0)); L(b);
			Hex(_L8("reqbuf"), req, 256);
		}
		if(desc[4]){
			TUint8 resp[256]; TPtr8 rp(resp, 0, sizeof(resp));
			r=dev.ReadPhys(desc[4], rp);
			b.Format(_L8("ReadPhys(respbuf %08x)=%d\n"), desc[4], r); L(b);
			Hex(_L8("respbuf"), resp, 256);
		}
	}
	else{
		L(_L8("descptr not a kernel address (channel idle); dumping shared page only\n"));
	}

	// the shared page itself (phys 0x1ffec000); securitykext memcpys a 0x10-byte
	// header to +0x400, so dump around there too
	TUint8 sp0[256]; TPtr8 s0(sp0, 0, sizeof(sp0));
	r=dev.ReadPhys(KShared, s0);
	b.Format(_L8("ReadPhys(shared %08x)=%d\n"), KShared, r); L(b);
	Hex(_L8("shared +0x000"), sp0, 256);
	TUint8 sp4[256]; TPtr8 s4(sp4, 0, sizeof(sp4));
	r=dev.ReadPhys(KShared+0x400, s4);
	b.Format(_L8("ReadPhys(shared+0x400 %08x)=%d\n"), KShared+0x400, r); L(b);
	Hex(_L8("shared +0x400"), sp4, 256);

	L(_L8("done\n"));
	dev.Close();
	gBin.Close(); gLog.Close(); fs.Close();
	return KErrNone;
}