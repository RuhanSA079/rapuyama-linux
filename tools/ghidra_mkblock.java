// ghidra headless PRE-script: adds an uninitialized memory block for the
// peripheral window so the ARM Constant Reference Analyzer turns LDR literal
// constants into real references we can collect afterwards
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

public class ghidra_mkblock extends GhidraScript{
	public void run() throws Exception{
		Memory mem=currentProgram.getMemory();
		Address base=currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(0xE0000000L);
		if(mem.getBlock(base)==null){
			mem.createUninitializedBlock("MMIO", base, 0x01000000L, false);
			println("created MMIO block @ 0xE0000000 size 0x01000000");
		}
		else{println("MMIO block already present");}
	}
}