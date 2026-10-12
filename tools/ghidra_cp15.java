// finds cp15 mmu setup (mcr/mrc p15) and page table / section descriptor building.
// dumps the instructions so i can find where the hw mapping happens
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;
public class ghidra_cp15 extends GhidraScript{
	public void run() throws Exception{
		Listing l=currentProgram.getListing();
		PrintWriter w=new PrintWriter(new FileWriter("/home/fluffles/Documents/Coding/other-projects/e6-linux/work/ghidra/cp15.txt"));
		InstructionIterator it=l.getInstructions(true);
		int n=0;
		while(it.hasNext()){
			Instruction insn=it.next();
			String s=insn.toString().toLowerCase();
			if(s.contains("mcr") || s.contains("mrc") || s.contains("p15")){
				w.printf("%s  %s%n", insn.getAddress(), insn.toString());
				n++;
			}
		}
		w.close();
		println("cp15 insns: "+n);
	}
}