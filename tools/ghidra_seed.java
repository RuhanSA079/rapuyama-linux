// ghidra headless PRE-script for mixed arm/thumb blobs (the isasw dram dump):
// reads a list of hex fn addresses (script arg), bit0=1 -> thumb, disassembles
// each one in the right mode and makes it a function. auto-analysis follows
// the calls from there. forcing one mode over everything (ghidra_thumb_setup)
// breaks on blobs that mix both
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.ArmDisassembleCommand;
import ghidra.program.model.address.*;
import java.io.*;

public class ghidra_seed extends GhidraScript{
	public void run() throws Exception{
		AddressSpace sp=currentProgram.getAddressFactory().getDefaultAddressSpace();
		BufferedReader r=new BufferedReader(new FileReader(getScriptArgs()[0]));
		String l;
		int n=0;
		while((l=r.readLine())!=null){
			l=l.trim();
			if(l.isEmpty()){continue;}
			long v=Long.parseLong(l, 16);
			boolean thumb=(v&1)!=0;
			Address a=sp.getAddress(v&~1L);
			if(getInstructionAt(a)==null){
				new ArmDisassembleCommand(a, null, thumb).applyTo(currentProgram, monitor);
			}
			if(getFunctionAt(a)==null && createFunction(a, null)!=null){n++;}
		}
		r.close();
		println("seeded "+n+" functions");
	}
}