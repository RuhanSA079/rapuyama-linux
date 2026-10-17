// ro dump of the live karap interrupt dispatch table, to find the modem
// (CMT/ISI) link IRQ line - the keystone of the on-chip modem transport (route A).
// reversed from karap FUN_800b89b0 (Interrupt::Bind) / FUN_800b8a18 (Unbind):
//   table base = 0xc8003340 (DAT_800b8bd4), 0x1a5 (421) entries of 8 bytes:
//     entry[line].word[0] = line (or 0), word[1] = ISR fn ptr (handler) OR arg
//   an UNBOUND line has word[1] == 0x800b89a4 (&LAB_800b89a4, the default stub);
//   a BOUND line has word[1] = a real handler address (sisa/isi = the modem link).
//   the INTC control regs are phys 0x20060020/0x20060024 (c6060020/24).
// we dump every bound line with its handler so we can identify the modem ISR by
// address (sisa_wrapper 0x80103xxx / isimessagekernel 0x8022xxxx) and then build
// the Linux-side handler + keepalive. all via ReadVirt = ro, safe.
#include <e32std.h>
#include <f32file.h>
#include "runblob.h"

_LIT(KLog, "E:\\intdump.log");

const TUint32 KTbl = 0xc8003340u; // DAT_800b8bd4 handler table base
const TInt KLines = 0x1a5;       // 421 lines
const TUint32 KUnbound= 0x800b89a4u; // default stub = line not bound

static RFile gLog;
static void L(const TDesC8& m){ gLog.Write(m); gLog.Flush(); }

static TUint32 Word(RRunBlob& dev, TUint32 va){
	TUint32 w=0; TPtr8 p((TUint8*)&w, 0, 4); dev.ReadVirt(va, p); return w;
}

GLDEF_C TInt E32Main(){
	RFs fs;
	if(fs.Connect()!=KErrNone){return KErrGeneral;}
	if(gLog.Replace(fs, KLog, EFileWrite)!=KErrNone){fs.Close(); return KErrGeneral;}
	L(_L8("intdump: read-only modem link IRQ hunt (karap dispatch table)\n"));

	User::FreeLogicalDevice(KRunBlobName);
	TInt rLoad=User::LoadLogicalDevice(KRunBlobLdd);
	TBuf8<96> b; b.Format(_L8("Load=%d\n"), rLoad); L(b);
	if(rLoad!=KErrNone && rLoad!=KErrAlreadyExists){gLog.Close(); fs.Close(); return rLoad;}

	RRunBlob dev;
	if(dev.Open()!=KErrNone){gLog.Close(); fs.Close(); return KErrGeneral;}

	b.Format(_L8("table @%08x, %d lines, unbound stub=%08x\n\n"), KTbl, KLines, KUnbound); L(b);

	TInt bound=0;
	for(TInt ln=0; ln<KLines; ++ln){
		TUint32 e0=Word(dev, KTbl + ln*8); // line id / 0
		TUint32 e1=Word(dev, KTbl + ln*8 + 4); // handler fn ptr
		if(e1==KUnbound || e1==0){ continue; } // not bound
		++bound;
		// tag by owning module (rom address range) to spot the modem link
		TPtrC8 mod(_L8("?"));
		if(e1>=0x80103000u && e1<0x80105000u) mod.Set(_L8("SISA"));
		else if(e1>=0x8022b000u && e1<0x8022d000u) mod.Set(_L8("ISIMSG"));
		else if(e1>=0x800b8000u && e1<0x800bd000u) mod.Set(_L8("karap"));
		else if(e1>=0x800bd000u && e1<0x800bf000u) mod.Set(_L8("ecust"));
		else if(e1>=0x80140000u && e1<0x8014a000u) mod.Set(_L8("power/sec"));
		b.Format(_L8("line %3d (0x%03x): w0=%08x handler=%08x  ["), ln, ln, e0, e1);
		b.Append(mod); b.Append(_L8("]\n")); L(b);
	}
	b.Format(_L8("\n%d lines bound.\n"), bound); L(b);
	L(_L8("look for SISA/ISIMSG handlers = the modem link IRQ(s). send E:\\intdump.log\n"));

	dev.Close(); gLog.Close(); fs.Close();
	return KErrNone;
}