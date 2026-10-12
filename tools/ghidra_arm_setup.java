// ghidra headless PRE-script, preps a relocated ARM (a32) symbian module:
// 1. adds an uninitialized MMIO block so refs into the peripheral window
// turn into real references
// 2. forces ARM (TMode=0) over the whole code area
// 3. disassembles the whole code range so literal pools get resolved
// then auto-analysis (ARM Constant Reference Analyzer) runs and makes the
// references that ghidra_mmio.java collects
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.ArmDisassembleCommand;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.math.BigInteger;

public class ghidra_arm_setup extends GhidraScript{
	public void run() throws Exception{
		Memory mem=currentProgram.getMemory();
		AddressSpace sp=currentProgram.getAddressFactory().getDefaultAddressSpace();

		// 1. mmio window
		Address mmio=sp.getAddress(0xE0000000L);
		if(mem.getBlock(mmio)==null){mem.createUninitializedBlock("MMIO", mmio, 0x01000000L, false);}

		// find the loaded module block (the one that isnt MMIO)
		MemoryBlock code=null;
		for(MemoryBlock b : mem.getBlocks()){
			if(!b.getName().equals("MMIO")){code=b; break;}
		}
		// code starts after the 0x78 byte TRomImageHeader
		Address start=code.getStart().add(0x78);
		Address end=code.getEnd();
		AddressSet set=new AddressSet(start, end);

		// 2. force arm over the code range
		Register tmode=currentProgram.getProgramContext().getRegister("TMode");
		currentProgram.getProgramContext().setValue(tmode, start, end, BigInteger.ZERO);
		println("TMode=0 over "+start+".."+end);

		// 3. disassemble the whole range as arm
		ArmDisassembleCommand cmd=new ArmDisassembleCommand(set, null, false); // false = arm
		cmd.applyTo(currentProgram, monitor);
		println("disassembled "+cmd.getDisassembledAddressSet().getNumAddresses()+" addrs");
	}
}