// runs a RAW arm blob from actually EXECUTABLE kernel memory
//
// first try jumped into Kern::Alloc memory which is execute-never (XN) ->
// prefetch abort. fix: alloc phys ram and map it with EMapAttrSupRwx
// (supervisor read/write/EXECUTE, XN clear) via DPlatChunkHw, copy the blob
// there and jump. mapping is uncached (EMapAttrFullyBlocking) so the I side sees
// our writes without cache games. this is the real primitive for loading code
#include <kernel/kernel.h>
#include <kernel/kern_priv.h>
#include <kernel/cache.h>
#include <platform.h> // DPlatChunkHw, Epoc::AllocPhysicalRam, EMapAttr*
#include "runblob.h"
#include "blob.hdr" // KStage0Blob[], KStage0BlobLen
#include "stub2.hdr" // KStub2[], KStub2Len (mmu-off trampoline stub)
#include "tramp3.hdr" // KTramp3[], KTramp3Len (full handoff trampoline)
#include "fakekern.hdr" // KFakeKern[], KFakeKernLen (fake kernel)
#include "takeover.hdr" // KTakeover[], KTakeoverLen (one-way takeover)
#include "tramp_boot.hdr" // KTrampBoot[], KTrampBootLen (zImage handoff)

typedef TInt (*TStubFn)(TUint32 aMarkerPhys, TUint32 aVal);
typedef void (*TVoidFn)(void);
typedef void (*TBootFn)(TUint32 aEntry, TUint32 aMach, TUint32 aDtb, TUint32 aWdog);
typedef TInt (*TTrampFn)(TUint32 aEntryPhys, TUint32 aMarkerPhys, TUint32 aDtb, TUint32 aMach);

typedef TInt (*TBlobFn)(TUint32* aOut, TInt aWords, TUint32 aCookie);

// cp15 helpers (arm, supervisor)
static TUint32 Ttbr0(){TUint32 v; asm volatile("mrc p15,0,%0,c2,c0,0":"=r"(v)); return v;}
static TUint32 Ttbcr(){TUint32 v; asm volatile("mrc p15,0,%0,c2,c0,2":"=r"(v)); return v;}
static void CleanDCacheMVA(TLinAddr a){asm volatile("mcr p15,0,%0,c7,c10,1"::"r"(a));}
static void Dsb(){asm volatile("mcr p15,0,%0,c7,c10,4"::"r"(0));}
static void InvTlbAll(){asm volatile("mcr p15,0,%0,c8,c7,0"::"r"(0));}
static void Isb(){asm volatile("mcr p15,0,%0,c7,c5,4"::"r"(0));}

// identity-maps a 1 mb section covering phys aP as normal executable in TTBR0.
// returns the L1 chunk (close it later) + index + old descriptor, or NULL
static DPlatChunkHw* IdMapExec(TPhysAddr aP, TUint32& aIdx, TUint32& aOld){
	TUint32 n=Ttbcr()&0x7;
	TUint32 l1size=0x4000u>>n;
	TPhysAddr l1phys=Ttbr0()&~(l1size-1);
	aIdx=(aP>>20);
	DPlatChunkHw* cl=NULL;
	if(DPlatChunkHw::New(cl, l1phys, (TInt)l1size, EMapAttrSupRw|EMapAttrCachedMax)!=KErrNone){return NULL;}
	volatile TUint32* l1=(volatile TUint32*)cl->LinearAddress();
	aOld=l1[aIdx];
	l1[aIdx]=(aP&0xFFF00000u)|(1u<<10)|0x8u|0x4u|0x2u; // normal exec
	CleanDCacheMVA((TLinAddr)&l1[aIdx]);
	Dsb(); InvTlbAll(); Isb();
	return cl;
}

class DRunBlobChan : public DLogicalChannelBase{
	public:
		DRunBlobChan() : iBootChunk(NULL), iBootPhys(0), iBootVirt(0), iBootSize(0){}
		virtual ~DRunBlobChan(){}
		// boot chunk state, kept between AllocBoot/LoadChunk/Boot calls
		DPlatChunkHw* iBootChunk;
		TPhysAddr iBootPhys;
		TLinAddr iBootVirt;
		TUint32 iBootSize;
	protected:
		virtual TInt DoCreate(TInt, const TDesC8*, const TVersion&){return KErrNone;}
		virtual TInt Request(TInt aFunction, TAny* a1, TAny* a2);
};

TInt DRunBlobChan::Request(TInt aFunction, TAny* a1, TAny* a2){
	// trampoline stage 1: build + check an identity section map
	if(aFunction==RRunBlob::EControlIdentityTest){
		const TInt KSec=0x100000; // 1 mb section
		TUint32 res[8];
		for(TInt z=0; z<8; ++z){res[z]=0;}
		DPlatChunkHw* cw=NULL;
		DPlatChunkHw* cl=NULL;
		TPhysAddr p=0;
		TBool haveP=EFalse;

		do{
			TInt r=Epoc::AllocPhysicalRam(KSec, p, 20); // 1 mb aligned
			if(r!=KErrNone){res[0]=(TUint32)r; break;}
			haveP=ETrue;
			res[1]=p;

			// put a marker in phys[0] through an UNCACHED writable alias
			r=DPlatChunkHw::New(cw, p, KSec, EMapAttrSupRw|EMapAttrFullyBlocking);
			if(r!=KErrNone){res[0]=(TUint32)r; break;}
			volatile TUint32* w=(volatile TUint32*)cw->LinearAddress();
			w[0]=0xDEADBEEFu;

			// L1 entry for va == p (identity). N (TTBCR) sets the TTBR0 table size
			TUint32 n=Ttbcr()&0x7;
			TUint32 l1size=0x4000u>>n; // 16kb >> N
			TPhysAddr l1phys=Ttbr0()&~(l1size-1);
			TUint32 idx=(p>>20); // section index
			res[2]=idx;

			r=DPlatChunkHw::New(cl, l1phys, (TInt)l1size, EMapAttrSupRw|EMapAttrCachedMax);
			if(r!=KErrNone){res[0]=(TUint32)r; break;}
			volatile TUint32* l1=(volatile TUint32*)cl->LinearAddress();

			TUint32 oldDesc=l1[idx];
			res[4]=oldDesc;
			if(oldDesc!=0){res[0]=1; break;} // va already mapped -> bail out safely

			// armv6 section descriptor, AFE format (SCTLR.AFE=1 on this chip):
			// bits[1:0]=10 section, AF=bit10=1, AP2=bit15=0 AP1=bit11=0 -> PL1 RW,
			// C=B=0 uncached (same as the uncached marker write), XN=0, domain 0
			TUint32 desc=(p&0xFFF00000u)|(1u<<10)|0x2u;
			res[3]=desc;
			l1[idx]=desc;
			CleanDCacheMVA((TLinAddr)&l1[idx]);
			Dsb(); InvTlbAll(); Isb();

			volatile TUint32* idva=(volatile TUint32*)p; // the identity va
			res[5]=idva[0]; // expect 0xDEADBEEF
			idva[0]=0x600DF00Du; // write via identity va...
			Dsb();
			res[6]=w[0]; // ...read back via the alias (expect 600DF00D)

			// restore
			l1[idx]=oldDesc;
			CleanDCacheMVA((TLinAddr)&l1[idx]);
			Dsb(); InvTlbAll(); Isb();
		}while(0);

		if(cl){cl->Close(NULL);}
		if(cw){cw->Close(NULL);}
		if(haveP){Epoc::FreePhysicalRam(p, KSec);}

		TPtrC8 src((const TUint8*)res, sizeof(res));
		return Kern::ThreadDesWrite(&Kern::CurrentThread(), a1, src, 0);
	}

	// trampoline stage 2: reversible mmu-off test
	if(aFunction==RRunBlob::EControlTrampTest){
		const TInt KSec=0x100000;
		TUint32 res[8];
		for(TInt z=0; z<8; ++z){res[z]=0;}
		DPlatChunkHw* cw=NULL;
		DPlatChunkHw* cl=NULL;
		TPhysAddr p=0;
		TBool haveP=EFalse;

		do{
			TInt r=Epoc::AllocPhysicalRam(KSec, p, 20);
			if(r!=KErrNone){res[0]=(TUint32)r; break;}
			haveP=ETrue; res[1]=p;

			// writable alias: copy the stub to phys[0], clear the marker at phys+0x1000
			r=DPlatChunkHw::New(cw, p, KSec, EMapAttrSupRw|EMapAttrFullyBlocking);
			if(r!=KErrNone){res[0]=(TUint32)r; break;}
			volatile TUint8* wb=(volatile TUint8*)cw->LinearAddress();
			for(TUint i=0; i<KStub2Len; ++i){wb[i]=KStub2[i];}
			*(volatile TUint32*)(cw->LinearAddress()+0x1000)=0;

			// identity-map section P as NORMAL (executable, SO memory cant run code)
			TUint32 n=Ttbcr()&0x7;
			TUint32 l1size=0x4000u>>n;
			TPhysAddr l1phys=Ttbr0()&~(l1size-1);
			TUint32 idx=(p>>20);
			r=DPlatChunkHw::New(cl, l1phys, (TInt)l1size, EMapAttrSupRw|EMapAttrCachedMax);
			if(r!=KErrNone){res[0]=(TUint32)r; break;}
			volatile TUint32* l1=(volatile TUint32*)cl->LinearAddress();
			TUint32 oldDesc=l1[idx];
			res[4]=oldDesc;
			if(oldDesc!=0){res[0]=1; break;}

			// section, AF=1, C=1,B=1 (normal cacheable with TRE on), XN=0, PL1 RW
			TUint32 desc=(p&0xFFF00000u)|(1u<<10)|0x8u|0x4u|0x2u; // 0x40E
			l1[idx]=desc;
			CleanDCacheMVA((TLinAddr)&l1[idx]);
			Dsb(); InvTlbAll(); Isb();
			Cache::IMB_Range((TLinAddr)p, KStub2Len); // so the I side sees the stub

			// grab PRRR/NMRR for debugging (memory type remap regs)
			asm volatile("mrc p15,0,%0,c10,c2,0":"=r"(res[5])); // PRRR
			asm volatile("mrc p15,0,%0,c10,c2,1":"=r"(res[6])); // NMRR

			// JUMP: run the stub from the identity va, it drops the mmu and brings it back
			TStubFn fn=(TStubFn)p;
			fn(p+0x1000, 0x600DCAFEu);
			res[2]=0x600DCAFEu;
			res[3]=*(volatile TUint32*)(cw->LinearAddress()+0x1000); // read the marker back

			l1[idx]=oldDesc;
			CleanDCacheMVA((TLinAddr)&l1[idx]);
			Dsb(); InvTlbAll(); Isb();
			res[0]=0;
		}while(0);

		if(cl){cl->Close(NULL);}
		if(cw){cw->Close(NULL);}
		if(haveP){Epoc::FreePhysicalRam(p, KSec);}

		TPtrC8 src((const TUint8*)res, sizeof(res));
		return Kern::ThreadDesWrite(&Kern::CurrentThread(), a1, src, 0);
	}

	// FULL trampoline test: load payload @+0x8000, hand off, echo regs back
	if(aFunction==RRunBlob::EControlFullTramp){
		const TInt KSec=0x100000;
		const TUint KOff=0x8000; // payload (kernel) offset, like TEXT_OFFSET
		const TUint KMk=0x4000; // marker offset
		const TUint32 KMach=0xE6E6E6E6u, KDtb=0xD7B00000u;
		TUint32 res[8];
		for(TInt z=0; z<8; ++z){res[z]=0;}
		DPlatChunkHw* cw=NULL;
		DPlatChunkHw* cl=NULL;
		TPhysAddr p=0;
		TBool haveP=EFalse;

		do{
			TInt r=Epoc::AllocPhysicalRam(KSec, p, 20);
			if(r!=KErrNone){res[0]=(TUint32)r; break;}
			haveP=ETrue; res[1]=p;

			r=DPlatChunkHw::New(cw, p, KSec, EMapAttrSupRw|EMapAttrFullyBlocking);
			if(r!=KErrNone){res[0]=(TUint32)r; break;}
			TLinAddr wl=cw->LinearAddress();
			volatile TUint8* wb=(volatile TUint8*)wl;
			for(TUint i=0; i<KTramp3Len; ++i){wb[i]=KTramp3[i];} // tramp @ 0
			for(TUint i=0; i<KFakeKernLen; ++i){wb[KOff+i]=KFakeKern[i];} // payload @ +0x8000
			for(TInt i=0; i<16; ++i){*(volatile TUint8*)(wl+KMk+i)=0;} // clear the marker

			TUint32 n=Ttbcr()&0x7;
			TUint32 l1size=0x4000u>>n;
			TPhysAddr l1phys=Ttbr0()&~(l1size-1);
			TUint32 idx=(p>>20);
			r=DPlatChunkHw::New(cl, l1phys, (TInt)l1size, EMapAttrSupRw|EMapAttrCachedMax);
			if(r!=KErrNone){res[0]=(TUint32)r; break;}
			volatile TUint32* l1=(volatile TUint32*)cl->LinearAddress();
			TUint32 oldDesc=l1[idx];
			if(oldDesc!=0){res[0]=1; break;}
			l1[idx]=(p&0xFFF00000u)|(1u<<10)|0x8u|0x4u|0x2u; // normal exec (0x40E)
			CleanDCacheMVA((TLinAddr)&l1[idx]);
			Dsb(); InvTlbAll(); Isb();
			Cache::IMB_Range((TLinAddr)p, KOff+KFakeKernLen);

			// hand off: tramp_entry(payloadPhys, markerPhys, dtb, machtype)
			TTrampFn fn=(TTrampFn)p;
			fn(p+KOff, p+KMk, KDtb, KMach);

			// read the regs the payload echoed back (mmu is back on by now)
			volatile TUint32* mk=(volatile TUint32*)(wl+KMk);
			res[2]=mk[0]; res[3]=mk[1]; res[4]=mk[2]; res[5]=mk[3];

			l1[idx]=oldDesc;
			CleanDCacheMVA((TLinAddr)&l1[idx]);
			Dsb(); InvTlbAll(); Isb();
			res[0]=0;
		}while(0);

		if(cl){cl->Close(NULL);}
		if(cw){cw->Close(NULL);}
		if(haveP){Epoc::FreePhysicalRam(p, KSec);}

		TPtrC8 src((const TUint8*)res, sizeof(res));
		return Kern::ThreadDesWrite(&Kern::CurrentThread(), a1, src, 0);
	}

	// EControlFbFill: write a colour through an EXISTING kernel va (fb hunt).
	// iPhys here is actually the kernel VIRT address that already maps the
	// target ram (from the unfiltered 0xc6 sweep). we do NOT make a new
	// DPlatChunkHw mapping (that crashed when pointed at in-use system ram).
	// write through the live mapping, then clean the D cache so the display
	// controller dma (reads phys ram) sees our pixels
	if(aFunction==RRunBlob::EControlFbFill){
		TFbFill f;
		TInt rr=Kern::ThreadRawRead(&Kern::CurrentThread(), a1, &f, sizeof(f));
		if(rr!=KErrNone){return rr;}
		if(f.iBytes==0 || f.iBytes>0x100000){f.iBytes=0x80000;} // default 512 kb
		volatile TUint32* v=(volatile TUint32*)f.iPhys; // live kernel va
		TUint n=f.iBytes/4;
		for(TUint i=0; i<n; ++i){v[i]=f.iColor;}
		for(TUint a=0; a<f.iBytes; a+=32){CleanDCacheMVA((TLinAddr)f.iPhys+a);} // clean D cache lines
		Dsb();
		return KErrNone;
	}

	// EControlTakeover: ONE-WAY. kill symbian, drive the display
	if(aFunction==RRunBlob::EControlTakeover){
		const TInt KSec=0x100000;
		TPhysAddr p=0;
		TInt r=Epoc::AllocPhysicalRam(KSec, p, 20);
		if(r!=KErrNone){return r;}
		DPlatChunkHw* cw=NULL;
		r=DPlatChunkHw::New(cw, p, KSec, EMapAttrSupRw|EMapAttrFullyBlocking);
		if(r!=KErrNone){Epoc::FreePhysicalRam(p, KSec); return r;}
		volatile TUint8* wb=(volatile TUint8*)cw->LinearAddress();
		for(TUint i=0; i<KTakeoverLen; ++i){wb[i]=KTakeover[i];}

		TUint32 n=Ttbcr()&0x7;
		TUint32 l1size=0x4000u>>n;
		TPhysAddr l1phys=Ttbr0()&~(l1size-1);
		TUint32 idx=(p>>20);
		DPlatChunkHw* cl=NULL;
		r=DPlatChunkHw::New(cl, l1phys, (TInt)l1size, EMapAttrSupRw|EMapAttrCachedMax);
		if(r!=KErrNone){cw->Close(NULL); Epoc::FreePhysicalRam(p, KSec); return r;}
		volatile TUint32* l1=(volatile TUint32*)cl->LinearAddress();
		if(l1[idx]!=0){cl->Close(NULL); cw->Close(NULL); Epoc::FreePhysicalRam(p, KSec); return KErrInUse;}
		l1[idx]=(p&0xFFF00000u)|(1u<<10)|0x8u|0x4u|0x2u; // normal exec
		CleanDCacheMVA((TLinAddr)&l1[idx]);
		Dsb(); InvTlbAll(); Isb();
		Cache::IMB_Range((TLinAddr)p, KTakeoverLen);

		TVoidFn fn=(TVoidFn)p;
		fn(); // never returns
		return KErrNone; // unreachable
	}

	// booter: AllocBoot / LoadChunk / Boot
	if(aFunction==RRunBlob::EControlAllocBoot){
		TUint32 size=0;
		TInt rr=Kern::ThreadRawRead(&Kern::CurrentThread(), a1, &size, 4);
		if(rr!=KErrNone){return rr;}
		if(iBootChunk){
			iBootChunk->Close(NULL);
			iBootChunk=NULL;
			Epoc::FreePhysicalRam(iBootPhys, iBootSize);
			iBootSize=0;
		}
		size=(size+0xFFFFFu)&~0xFFFFFu; // round up to 1 mb
		TInt r=Epoc::AllocPhysicalRam((TInt)size, iBootPhys, 20);
		if(r!=KErrNone){return r;}
		r=DPlatChunkHw::New(iBootChunk, iBootPhys, (TInt)size, EMapAttrSupRw|EMapAttrCachedMax);
		if(r!=KErrNone){Epoc::FreePhysicalRam(iBootPhys, (TInt)size); iBootPhys=0; return r;}
		iBootVirt=iBootChunk->LinearAddress();
		iBootSize=size;
		TUint32 out=(TUint32)iBootPhys;
		return Kern::ThreadRawWrite(&Kern::CurrentThread(), a2, &out, 4);
	}

	if(aFunction==RRunBlob::EControlLoadChunk){
		if(!iBootChunk){return KErrNotReady;}
		TLoadChunk c;
		TInt rr=Kern::ThreadRawRead(&Kern::CurrentThread(), a1, &c, sizeof(c));
		if(rr!=KErrNone){return rr;}
		if(c.iOffset>iBootSize || c.iLen>iBootSize-c.iOffset){return KErrArgument;}
		return Kern::ThreadRawRead(&Kern::CurrentThread(), (const TAny*)c.iSrc, (TUint8*)(iBootVirt+c.iOffset), (TInt)c.iLen);
	}

	if(aFunction==RRunBlob::EControlBoot){
		if(!iBootChunk){return KErrNotReady;}
		TBootParams b;
		TInt rr=Kern::ThreadRawRead(&Kern::CurrentThread(), a1, &b, sizeof(b));
		if(rr!=KErrNone){return rr;}

		// patch the dtb /memory base sentinel (BE 1b ad b0 02) -> boot chunk phys
		TUint8* dtb=(TUint8*)(iBootVirt+b.iDtbOff);
		TUint32 scan=iBootSize-b.iDtbOff;
		if(scan>0x10000u){scan=0x10000u;}
		for(TUint32 i=0; i+4<=scan; ++i){
			if(dtb[i]==0x1b && dtb[i+1]==0xad && dtb[i+2]==0xb0 && dtb[i+3]==0x02){
				TUint32 be=(TUint32)iBootPhys;
				dtb[i]=(TUint8)(be>>24); dtb[i+1]=(TUint8)(be>>16);
				dtb[i+2]=(TUint8)(be>>8); dtb[i+3]=(TUint8)be;
				break;
			}
		}

		// make the whole boot chunk coherent for the mmu-off kernel
		for(TUint32 off=0; off<iBootSize; off+=32){CleanDCacheMVA(iBootVirt+off);}
		Dsb();

		// put tramp_boot in its own identity-mapped executable page and jump
		const TInt KSec=0x100000;
		TPhysAddr sp=0;
		if(Epoc::AllocPhysicalRam(KSec, sp, 20)!=KErrNone){return KErrNoMemory;}
		DPlatChunkHw* sc=NULL;
		if(DPlatChunkHw::New(sc, sp, KSec, EMapAttrSupRw|EMapAttrFullyBlocking)!=KErrNone){
			Epoc::FreePhysicalRam(sp, KSec);
			return KErrNoMemory;
		}
		volatile TUint8* sb=(volatile TUint8*)sc->LinearAddress();
		for(TUint i=0; i<KTrampBootLen; ++i){sb[i]=KTrampBoot[i];}
		TUint32 idx=0, old=0;
		DPlatChunkHw* cl=IdMapExec(sp, idx, old);
		if(!cl){sc->Close(NULL); Epoc::FreePhysicalRam(sp, KSec); return KErrGeneral;}
		Cache::IMB_Range((TLinAddr)sp, KTrampBootLen);

		TBootFn fn=(TBootFn)sp;
		fn((TUint32)iBootPhys+b.iZImageOff, b.iMach, (TUint32)iBootPhys+b.iDtbOff, b.iWdog); // never returns
		return KErrNone; // unreachable
	}

	// EControlTimedDiff: find free-running counters (timer/watchdog)
	if(aFunction==RRunBlob::EControlTimedDiff){
		TFbFill p; // reused: iPhys=virt iBytes=count iColor=delay
		TInt rr=Kern::ThreadRawRead(&Kern::CurrentThread(), a1, &p, sizeof(p));
		if(rr!=KErrNone){return rr;}
		TUint count=p.iBytes;
		if(count==0 || count>1024){count=256;}
		TUint32* ab=(TUint32*)Kern::Alloc(count*8);
		if(!ab){return KErrNoMemory;}
		volatile TUint32* mm=(volatile TUint32*)p.iPhys;
		for(TUint i=0; i<count; ++i){ab[i]=mm[i];} // snapshot A
		volatile TUint32 spin=0;
		for(TUint32 k=0; k<p.iColor; ++k){spin+=k;} // delay
		for(TUint i=0; i<count; ++i){ab[count+i]=mm[i];} // snapshot B
		TPtrC8 src((const TUint8*)ab, (TInt)(count*8));
		TInt r=Kern::ThreadDesWrite(&Kern::CurrentThread(), a2, src, 0);
		Kern::Free(ab);
		return r;
	}

	if(aFunction!=RRunBlob::EControlRunBlob){return KErrNotSupported;}

	const TInt KPageSz=0x1000;
	const TInt KOutWords=32;

	// 1. grab one physically contiguous ram page
	TPhysAddr phys=0;
	TInt r=Epoc::AllocPhysicalRam(KPageSz, phys);
	if(r!=KErrNone){return r;}

	// 2. map it executable (supervisor rwx, uncached)
	DPlatChunkHw* chunk=NULL;
	r=DPlatChunkHw::New(chunk, phys, KPageSz, EMapAttrSupRwx|EMapAttrFullyBlocking);
	if(r!=KErrNone){Epoc::FreePhysicalRam(phys, KPageSz); return r;}
	TLinAddr lin=chunk->LinearAddress();

	// 3. copy the raw blob in
	TUint8* code=(TUint8*)lin;
	for(TUint i=0; i<KStage0BlobLen; ++i){code[i]=KStage0Blob[i];}
	Cache::IMB_Range(lin, KStage0BlobLen);

	// 4. THE JUMP: run the loaded raw machine code
	TUint32 out[KOutWords];
	for(TInt j=0; j<KOutWords; ++j){out[j]=0;}
	TBlobFn fn=(TBlobFn)lin;
	TInt wrote=fn(out, KOutWords, 0x5A5AA5A5u);
	(void)wrote;

	// 5. give the result words back to user space
	TPtrC8 src((const TUint8*)out, KOutWords*4);
	r=Kern::ThreadDesWrite(&Kern::CurrentThread(), a1, src, 0);

	// 6. cleanup (DPlatChunkHw is a DObject -> Close(NULL), not Kern::ChunkClose)
	chunk->Close(NULL);
	Epoc::FreePhysicalRam(phys, KPageSz);
	return r;
}

class DRunBlobFactory : public DLogicalDevice{
	public:
		DRunBlobFactory(){iVersion=TVersion(1,0,0); iParseMask=0;}
		virtual TInt Install(){return SetName(&KRunBlobName);}
		virtual void GetCaps(TDes8& aDes) const{TVersion v=iVersion; Kern::InfoCopy(aDes, (const TUint8*)&v, sizeof(v));}
		virtual TInt Create(DLogicalChannelBase*& aChannel){aChannel=new DRunBlobChan; return aChannel ? KErrNone : KErrNoMemory;}
};

DECLARE_STANDARD_LDD(){
	return new DRunBlobFactory;
}