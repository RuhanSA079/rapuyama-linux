// tiny eka2 kernel-side driver for the rapuyama
// on EControlReadL1 it reads the cp15 mmu regs, walks the page map and copies
// it all to a user descriptor. no parsing in kernel mode, keeping the crash
// surface as small as i can
#include <kernel/kernel.h>
#include <kernel/kern_priv.h>
#include "ptdump.h"

// cp15 accessors (compiled as arm)
static TUint32 Ttbr0(){TUint32 v; asm volatile("mrc p15,0,%0,c2,c0,0" : "=r"(v)); return v;}
static TUint32 Ttbr1(){TUint32 v; asm volatile("mrc p15,0,%0,c2,c0,1" : "=r"(v)); return v;}
static TUint32 Ttbcr(){TUint32 v; asm volatile("mrc p15,0,%0,c2,c0,2" : "=r"(v)); return v;}
static TUint32 Sctlr(){TUint32 v; asm volatile("mrc p15,0,%0,c1,c0,0" : "=r"(v)); return v;}

class DPtDumpChan : public DLogicalChannelBase{
	public:
		virtual ~DPtDumpChan(){}
	protected:
		virtual TInt DoCreate(TInt aUnit, const TDesC8* aInfo, const TVersion& aVer);
		virtual TInt Request(TInt aFunction, TAny* a1, TAny* a2);
};

TInt DPtDumpChan::DoCreate(TInt /*aUnit*/, const TDesC8* /*aInfo*/, const TVersion& /*aVer*/){return KErrNone;}

TInt DPtDumpChan::Request(TInt aFunction, TAny* a1, TAny* a2){
	// EControlReadWin: reads one small window of (maybe mmio) memory
	// NOT wrapped in XTRAP on purpose: external mmio aborts cant be trapped here
	// (tried it). instead the user side saves progress every call, so a crash
	// only loses this one window and the next run skips past it
	if(aFunction==RPtDump::EControlReadWin){
		TReadWin w;
		TInt rp=Kern::ThreadRawRead(&Kern::CurrentThread(), a1, &w, sizeof(w));
		if(rp!=KErrNone){return rp;}
		TUint count=w.iCount;
		if(count==0 || count>(TUint)KWinMax){count=KWinMax;}
		TUint32* wb=(TUint32*)Kern::Alloc(count*4);
		if(!wb){return KErrNoMemory;}
		volatile TUint32* mm=(volatile TUint32*)w.iVirt;
		for(TUint i=0; i<count; ++i){wb[i]=mm[i];} // <- can abort+reboot, thats expected
		TPtrC8 src((const TUint8*)wb, (TInt)(count*4));
		TInt r=Kern::ThreadDesWrite(&Kern::CurrentThread(), a2, src, 0);
		Kern::Free(wb);
		return r;
	}

	// EControlPrintfDiff: snapshot A, spam the debug uart, snapshot B.
	// only reads addrs the caller already knows decode (live pages) so its safe.
	// if the uart is one of them its tx/status/fifo regs will change A->B
	if(aFunction==RPtDump::EControlPrintfDiff){
		TReadWin w;
		TInt rp=Kern::ThreadRawRead(&Kern::CurrentThread(), a1, &w, sizeof(w));
		if(rp!=KErrNone){return rp;}
		TUint count=w.iCount;
		if(count==0 || count>(TUint)KWinMax){count=KWinMax;}
		TUint32* ab=(TUint32*)Kern::Alloc(count*8); // A then B
		if(!ab){return KErrNoMemory;}
		volatile TUint32* mm=(volatile TUint32*)w.iVirt;
		for(TUint i=0; i<count; ++i){ab[i]=mm[i];} // snapshot A
		for(TInt k=0; k<24; ++k){
			Kern::Printf("ptdump-uartfind %d ------------------------", k);
		}
		for(TUint i=0; i<count; ++i){ab[count+i]=mm[i];} // snapshot B
		TPtrC8 src((const TUint8*)ab, (TInt)(count*8));
		TInt r=Kern::ThreadDesWrite(&Kern::CurrentThread(), a2, src, 0);
		Kern::Free(ab);
		return r;
	}

	if(aFunction!=RPtDump::EControlReadL1){return KErrNotSupported;}

	// v9 output layout (all TUint32):
	// [0]=TTBR0 [1]=TTBR1 [2]=TTBCR [3]=SCTLR
	// [4]=nRuns [5]=nProbes [6]=readVirt [7]=readPhys [8]=readWords
	// [9..10]=reserved(0) [11]=nCWin [12..15]=reserved(0)
	// then nRuns triples {virt, phys, pages} (non-ram runs)
	// then nProbes pairs {halVirt, LinearToPhysical} (hal-id candidates)
	// then nCWin triples {virt, phys, pages} (unfiltered 0xc0..0xc8 window)
	TUint8* buf=(TUint8*)Kern::Alloc(KPtBufSize);
	if(!buf){return KErrNoMemory;}
	TUint32* out=(TUint32*)buf;
	for(TInt z=0; z<16; ++z){out[z]=0;}
	out[0]=Ttbr0();
	out[1]=Ttbr1();
	out[2]=Ttbcr();
	out[3]=Sctlr();

	const TUint32 KInvalid=0xFFFFFFFFu;
	const TUint32 KRamLo=0x10000000u, KRamHi=0x21000000u;
	const TUint32 KStep=0x1000u; // 4 kb
	const TUint32 KPeriphCore=0x40000000u; // phys base of the peripheral core

	// part 1: fine 4 GiB sweep, non-ram runs go out as {virt, phys, pages}
	const TInt KHdrWords=16;
	const TInt KMaxRuns=128;
	TUint32* runs=out+KHdrWords;
	TInt nRuns=0;
	TLinAddr curVirt=0; TPhysAddr curPhys=KInvalid; TUint32 curPages=0;
	TUint32 expectPhys=KInvalid;
	TLinAddr periphVirt40=0; TUint32 periph40Pages=0;
	for(TLinAddr va=0x00001000u; va!=0x00000000u; va+=KStep){
		TPhysAddr pa=Epoc::LinearToPhysical(va);
		TBool keep=(pa!=KInvalid) && !(pa>=KRamLo && pa<KRamHi);
		if(curPages>0 && keep && pa==expectPhys){curPages++;}
		else{
			if(curPages>0 && nRuns<KMaxRuns){ // flush current run
				runs[nRuns*3+0]=(TUint32)curVirt;
				runs[nRuns*3+1]=(TUint32)curPhys;
				runs[nRuns*3+2]=curPages;
				if(curPhys==KPeriphCore){periphVirt40=curVirt; periph40Pages=curPages;}
				nRuns++;
			}
			if(keep){curVirt=va; curPhys=pa; curPages=1;}
			else{curPages=0;}
		}
		expectPhys=keep ? (pa+KStep) : KInvalid;
	}
	if(curPages>0 && nRuns<KMaxRuns){
		runs[nRuns*3+0]=(TUint32)curVirt;
		runs[nRuns*3+1]=(TUint32)curPhys;
		runs[nRuns*3+2]=curPages;
		if(curPhys==KPeriphCore){periphVirt40=curVirt; periph40Pages=curPages;}
		nRuns++;
	}
	out[4]=(TUint32)nRuns;

	// part 2: probe the hal-id candidates i pulled from the driver literal pools
	// (if any of them resolve theyre real virt addrs and not hal ids)
	static const TUint32 KHalCand[]={
		0xe0410000u, 0xe0411000u, 0xe0424000u, 0xe0451000u, // uart block
		0xe0800000u, 0xe0600000u, 0xe0100000u, 0xe0400000u, // intc/pwm/gpio/mem
		0xe0200000u};
	const TInt nCand=(TInt)(sizeof(KHalCand)/sizeof(KHalCand[0]));
	TUint32* probes=runs+nRuns*3;
	for(TInt i=0; i<nCand; ++i){
		probes[i*2+0]=KHalCand[i];
		probes[i*2+1]=Epoc::LinearToPhysical(KHalCand[i]);
	}
	out[5]=(TUint32)nCand;

	// ReadL1 doesnt touch mmio anymore (map + hal probes only) so the scanner
	// can grab the page map with zero reboot risk. all the dangerous mmio
	// reading happens separately, one window at a time, via EControlReadWin
	(void)periphVirt40; (void)periph40Pages;
	out[6]=0; out[7]=0; out[8]=0; out[9]=0; out[10]=0;

	// part 3: UNFILTERED sweep of the kernel peripheral window.
	// part 1 drops every phys in the ram range [0x10000000,0x21000000) to skip
	// dram fragmentation noise. but an on-chip peripheral (timer/intc/uart)
	// could be mapped in the 0xc0000000..0xc8000000 kernel window with a phys
	// that lands in that ram range -> it would get filtered out. so re-sweep
	// that window and record EVERY mapped page, no phys filter, as
	// {virt, phys, pages} runs. still only LinearToPhysical, no mmio reads
	TUint32* cwin=out+16+nRuns*3+nCand*2;
	const TUint KMaxCWin=200;
	TUint nCWin=0;
	TLinAddr cvCur=0; TPhysAddr cpCur=KInvalid; TUint32 cPages=0;
	TUint32 cExpect=KInvalid;
	for(TLinAddr va=0xc0000000u; va<0xc8000000u; va+=KStep){
		TPhysAddr pa=Epoc::LinearToPhysical(va);
		TBool mapped=(pa!=KInvalid);
		if(cPages>0 && mapped && pa==cExpect){cPages++;}
		else{
			if(cPages>0 && nCWin<KMaxCWin){
				cwin[nCWin*3+0]=(TUint32)cvCur;
				cwin[nCWin*3+1]=(TUint32)cpCur;
				cwin[nCWin*3+2]=cPages;
				nCWin++;
			}
			if(mapped){cvCur=va; cpCur=pa; cPages=1;}
			else{cPages=0;}
		}
		cExpect=mapped ? (pa+KStep) : KInvalid;
	}
	if(cPages>0 && nCWin<KMaxCWin){
		cwin[nCWin*3+0]=(TUint32)cvCur;
		cwin[nCWin*3+1]=(TUint32)cpCur;
		cwin[nCWin*3+2]=cPages;
		nCWin++;
	}
	out[11]=nCWin;

	TInt total=(TInt)((16+nRuns*3+nCand*2+nCWin*3)*4);
	TPtrC8 src(buf, total);
	TInt r=Kern::ThreadDesWrite(&Kern::CurrentThread(), a1, src, 0);
	Kern::Free(buf);
	return r;
}

class DPtDumpFactory : public DLogicalDevice{
	public:
		DPtDumpFactory();
		virtual TInt Install();
		virtual void GetCaps(TDes8& aDes) const;
		virtual TInt Create(DLogicalChannelBase*& aChannel);
};

DPtDumpFactory::DPtDumpFactory(){
	iVersion=TVersion(1, 0, 0);
	iParseMask=0; // no unit, no pdd, no info
}

TInt DPtDumpFactory::Install(){
	return SetName(&KPtDumpName);
}

void DPtDumpFactory::GetCaps(TDes8& aDes) const{
	TVersion v=iVersion;
	Kern::InfoCopy(aDes, (const TUint8*)&v, sizeof(v));
}

TInt DPtDumpFactory::Create(DLogicalChannelBase*& aChannel){
	aChannel=new DPtDumpChan;
	return aChannel ? KErrNone : KErrNoMemory;
}

DECLARE_STANDARD_LDD(){
	return new DPtDumpFactory;
}