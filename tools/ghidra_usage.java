// for every instruction that loads a constant in the peripheral window, prints
// it plus the next few, so i can see if the value gets dereferenced (a real
// address) or masked/shifted (a hal-encoded register id)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import java.io.*;

public class ghidra_usage extends GhidraScript{
	public void run() throws Exception{
		final long LO=0xE0000000L, HI=0xE1000000L;
		Memory mem=currentProgram.getMemory();
		Listing l=currentProgram.getListing();
		String out="/home/fluffles/Documents/Coding/other-projects/e6-linux/work/ghidra/"+currentProgram.getName()+".usage.txt";
		PrintWriter w=new PrintWriter(new FileWriter(out));

		InstructionIterator it=l.getInstructions(true);
		int shown=0;
		while(it.hasNext() && shown<25){
			Instruction insn=it.next();
			boolean hit=false;
			for(Reference ref : insn.getReferencesFrom()){
				Address t=ref.getToAddress();
				if(t==null || !t.isMemoryAddress()){continue;}
				long off=t.getOffset()&0xFFFFFFFFL;
				long val=-1;
				if(LO<=off && off<HI){val=off;}
				else if(mem.contains(t)){
					try{
						long v=mem.getInt(t)&0xFFFFFFFFL;
						if(LO<=v && v<HI){val=v;}
					}
					catch(Exception e){}
				}
				if(val>=0){hit=true; break;}
			}
			if(!hit){continue;}
			shown++;
			w.printf("---- load site @ %s ----%n", insn.getAddress());
			Instruction cur=insn;
			for(int i=0; i<7 && cur!=null; i++){
				w.printf("  %s  %s%n", cur.getAddress(), cur.toString());
				cur=cur.getNext();
			}
			w.println();
		}
		w.close();
		println("usage dump -> "+out+" ("+shown+" sites)");
	}
}