// ghidra headless PRE-script for the whole kernel-side rom (kern.bin @0x80000000)
// adds the c6 peripheral window + kernel data as uninitialized blocks so refs
// resolve, forces arm and disassembles every module text range from modules.txt
// (lines: text_start text_end name entry, hex) and makes a function at each entry
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.ArmDisassembleCommand;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.math.BigInteger;

public class ghidra_kern_setup extends GhidraScript{
	public void run() throws Exception{
		Memory mem=currentProgram.getMemory();
		AddressSpace sp=currentProgram.getAddressFactory().getDefaultAddressSpace();
		if(mem.getBlock(sp.getAddress(0xc6000000L))==null){mem.createUninitializedBlock("PERIPH", sp.getAddress(0xc6000000L), 0x01000000L, false);}
		if(mem.getBlock(sp.getAddress(0xc8000000L))==null){mem.createUninitializedBlock("KDATA", sp.getAddress(0xc8000000L), 0x02200000L, false);}
		Register tmode=currentProgram.getProgramContext().getRegister("TMode");
		// arm everywhere before anything gets disassembled
		currentProgram.getProgramContext().setValue(tmode, sp.getAddress(0x80000000L), mem.getBlock(sp.getAddress(0x80000000L)).getEnd(), BigInteger.ZERO);
		String mods=getScriptArgs()[0];
		BufferedReader br=new BufferedReader(new FileReader(mods));
		String l;
		while((l=br.readLine())!=null){
			String[] s=l.trim().split(" ");
			Address a=sp.getAddress(Long.parseLong(s[0], 16));
			Address b=sp.getAddress(Long.parseLong(s[1], 16)-1);
			Address ent=sp.getAddress(Long.parseLong(s[3], 16));
			ArmDisassembleCommand cmd=new ArmDisassembleCommand(new AddressSet(a, b), null, false);
			cmd.applyTo(currentProgram, monitor);
			createLabel(ent, s[2].replace('.', '_')+"_entry", true, SourceType.USER_DEFINED);
			createFunction(ent, null);
			println(s[2]+" "+cmd.getDisassembledAddressSet().getNumAddresses());
		}
		br.close();
	}
}