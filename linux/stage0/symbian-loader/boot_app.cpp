// streams a linux zImage + dtb from E: into contiguous phys ram and hands off
// to the kernel (one-way). needs E:\zImage and E:\e6.dtb on the phone
//
// chunk layout: zImage @ +0x00008000 (TEXT_OFFSET), dtb @ +0x02000000.
// The E6 kernel has fixed PHYS_OFFSET=0x10000000; the DTB describes the
// same RAM range. The legacy sentinel patch in the driver does not match it.
// after Boot() the kernel takes over and this never returns (battery pull to
// recover). until there are rapuyama drivers + watchdog handling this wont
// boot visibly, its just the loader ready for when the adapter shows up
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\boot.log");
#ifdef RB_IMAGE_BOOT
// e6bfix: the uncompressed kernel (build-vc/arch/arm/boot/Image), it runs in
// place inside the chunk, see AllocBoot in runblob_ldd.cpp
_LIT(KZImage, "E:\\Image");
#else
_LIT(KZImage, "E:\\zImage");
#endif
_LIT(KDtb, "E:\\e6.dtb");

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
		off+=p.Length();
	}
	f.Close();
	return (r==KErrNone) ? total : r;
}

GLDEF_C TInt E32Main(){
	if(gFs.Connect()!=KErrNone){return KErrGeneral;}
	if(gLog.Replace(gFs, KLog, EFileWrite)!=KErrNone){gFs.Close(); return KErrGeneral;}
	L(_L8("boot: started\n"));

	User::FreeLogicalDevice(KRunBlobName);
	TInt rLoad=User::LoadLogicalDevice(KRunBlobLdd);
	Lf(_L8("LoadLogicalDevice=%d\n"), rLoad);
	if(rLoad!=KErrNone && rLoad!=KErrAlreadyExists){gLog.Close(); gFs.Close(); return rLoad;}

	RRunBlob dev;
	if(dev.Open()!=KErrNone){L(_L8("open failed\n")); gLog.Close(); gFs.Close(); return KErrGeneral;}

	TUint8* buf=(TUint8*)User::Alloc(KBuf);
	if(!buf){L(_L8("no mem\n")); return KErrNoMemory;}

	TUint32 phys=0;
	TInt r=dev.AllocBoot(KChunkSz, phys);
	Lf(_L8("AllocBoot=%d phys=%08x\n"), r, phys);
	if(r!=KErrNone){User::Free(buf); gLog.Close(); gFs.Close(); return r;}

	TInt zsz=Stream(dev, KZImage, KZOff, buf);
	Lf(_L8("zImage stream=%d\n"), zsz);
	TInt dsz=Stream(dev, KDtb, KDtbOff, buf);
	Lf(_L8("dtb stream=%d\n"), dsz);
	User::Free(buf);

	if(zsz<=0 || dsz<=0){
		L(_L8("missing zImage/dtb on E: -- aborting\n"));
		gLog.Close(); gFs.Close();
		return KErrNotFound;
	}

	L(_L8("about-to-Boot (one-way)\n"));
	gLog.Close(); gFs.Close();
	dev.Boot(KZOff, KDtbOff, 0xFFFFFFFFu, 0); // never returns
	return KErrNone;
}