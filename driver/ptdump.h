// shared stuff between the ptdump ldd (kernel) and the user-side client
// kernel side does the bare minimum (read cp15, copy raw tables to user mem)
// all the parsing happens offline on my pc
#ifndef PTDUMP_H
#define PTDUMP_H

#include <e32cmn.h>
#ifndef __KERNEL_MODE__
#include <e32std.h>
#endif

_LIT(KPtDumpName, "ptdump");

// armv6 L1 table is 16 KiB (4096 x 32bit descriptors)
const TInt KL1TableSize=16*1024;

// header that goes before the raw L1 table in the returned buffer
struct TPtDumpHdr{
	TUint32 iTtbr0; // cp15 c2,c0,0
	TUint32 iTtbr1; // cp15 c2,c0,1
	TUint32 iTtbcr; // cp15 c2,c0,2
	TUint32 iSctlr; // cp15 c1,c0,0 (control reg)
	TUint32 iL1Phys; // phys base that actually got mapped & copied
	TUint32 iL1Size; // == KL1TableSize
};

const TInt KPtBufSize=sizeof(TPtDumpHdr)+KL1TableSize;

// params for EControlReadWin: read iCount 32bit words starting at virt iVirt
struct TReadWin{
	TUint32 iVirt;
	TUint32 iCount; // number of words (kernel caps it to KWinMax)
};
const TInt KWinMax=256; // max words per ReadWin call (1 kb)

class RPtDump : public RBusLogicalChannel{
	public:
		enum TControl{EControlReadL1, EControlReadWin, EControlPrintfDiff};

	#ifndef __KERNEL_MODE__
		TInt Open(){return DoCreate(KPtDumpName, TVersion(1, 0, 0), KNullUnit, NULL, NULL);}	
		TInt ReadL1(TDes8& aBuf){return DoControl(EControlReadL1, (TAny*)&aBuf);}

		// reads aCount words from virt aVirt into aBuf. one page per call so the
		// user side can save progress between calls (so a crash doesnt lose the scan)
		TInt ReadWin(TUint32 aVirt, TInt aCount, TDes8& aBuf){
			TReadWin w;
			w.iVirt=aVirt; w.iCount=(TUint32)aCount;
			return DoControl(EControlReadWin, (TAny*)&w, (TAny*)&aBuf);
		}

		// snapshot A of aCount words at aVirt, spam the kernel debug uart with
		// Kern::Printf, snapshot B. aBuf gets A then B (2*aCount words). whatever
		// regs change A->B got touched by the uart driver => thats the uart
		TInt PrintfDiff(TUint32 aVirt, TInt aCount, TDes8& aBuf){
			TReadWin w;
			w.iVirt=aVirt; w.iCount=(TUint32)aCount;
			return DoControl(EControlPrintfDiff, (TAny*)&w, (TAny*)&aBuf);
		}
	#endif
};

#endif // PTDUMP_H