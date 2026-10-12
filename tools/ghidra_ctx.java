// for the first few instructions that load a constant in the peripheral window,
// dumps them + the next 5 instructions (short version of ghidra_usage.java)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import java.io.*;
public class ghidra_ctx extends GhidraScript{
	public void run() throws Exception{
		final long LO=0xE0000000L, HI=0xE1000000L;
		Memory mem=currentProgram.getMemory();
		Listing l=currentProgram.getListing();
		PrintWriter w=new PrintWriter(new FileWriter("/home/fluffles/Documents/Coding/other-projects/e6-linux/work/ghidra/"+currentProgram.getName()+".ctx.txt"));
		InstructionIterator it=l.getInstructions(true);
		int shown=0;
		while(it.hasNext() && shown<8){
			Instruction insn=it.next();
			long val=-1;
			for(Reference ref : insn.getReferencesFrom()){
				Address t=ref.getToAddress();
				if(t==null || !t.isMemoryAddress()){continue;}
				long off=t.getOffset()&0xFFFFFFFFL;
				if(LO<=off && off<HI){val=off;}
				else if(mem.contains(t)){
					try{
						long v=mem.getInt(t)&0xFFFFFFFFL;
						if(LO<=v && v<HI){val=v;}
					}
					catch(Exception e){}
				}
				if(val>=0){break;}
			}
			if(val<0){continue;}
			shown++;
			w.printf("==== loads %#x @ %s ====%n", val, insn.getAddress());
			Instruction c=insn;
			for(int i=0; i<6 && c!=null; i++){
				w.printf("  %s  %s%n", c.getAddress(), c.toString());
				c=c.getNext();
			}
			w.println();
		}
		w.close();
		println("ctx -> sites "+shown);
	}
}