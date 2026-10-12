// ghidra headless post-script (java GhidraScript)
// goes over ALL instructions and collects literal pool constants that land in
// the peripheral window [0xE0000000, 0xE1000000). for every LDR whose ref
// points at a data word inside the module it reads that word, and if its in the
// window, records it with the loading instruction and its function (if any)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import java.io.*;
import java.util.*;

public class ghidra_mmio extends GhidraScript{
	public void run() throws Exception{
		final long LO=0xE0000000L, HI=0xE1000000L;
		Memory mem=currentProgram.getMemory();
		Listing listing=currentProgram.getListing();
		FunctionManager fm=currentProgram.getFunctionManager();

		class Row{long tgt, from; String fn;}
		List<Row> rows=new ArrayList<Row>();
		TreeMap<Long, Integer> regs=new TreeMap<Long, Integer>();

		InstructionIterator it=listing.getInstructions(true);
		while(it.hasNext()){
			Instruction insn=it.next();
			for(Reference ref : insn.getReferencesFrom()){
				Address t=ref.getToAddress();
				if(t==null || !t.isMemoryAddress()){continue;}
				long off=t.getOffset()&0xFFFFFFFFL;
				long val;
				if(LO<=off && off<HI){val=off;} // direct ref into the window
				else if(mem.contains(t)){
					try{val=mem.getInt(t)&0xFFFFFFFFL;} // literal pool word
					catch(Exception e){continue;}
					if(val<LO || val>=HI){continue;}
				}
				else{continue;}

				Row r=new Row();
				r.tgt=val;
				r.from=insn.getAddress().getOffset()&0xFFFFFFFFL;
				Function f=fm.getFunctionContaining(insn.getAddress());
				r.fn=(f!=null) ? f.getName() : "-";
				rows.add(r);
				Integer c=regs.get(val);
				regs.put(val, (c==null ? 0 : c)+1);
			}
		}
		Collections.sort(rows, new Comparator<Row>(){
			public int compare(Row a, Row b){
				int c=Long.compare(a.tgt, b.tgt);
				return c!=0 ? c : Long.compare(a.from, b.from);
			}
		});

		String name=currentProgram.getName();
		String out="/home/fluffles/Documents/Coding/other-projects/e6-linux/work/ghidra/"+name+".mmio.txt";
		PrintWriter f=new PrintWriter(new FileWriter(out));
		f.printf("# MMIO literal loads in %s  (window %#x..%#x)%n", name, LO, HI);
		f.println("# register     function                        loaded_at");
		for(Row r : rows){f.printf("0x%08x   %-30s 0x%08x%n", r.tgt, r.fn, r.from);}
		f.printf("%n# distinct registers: %d%n", regs.size());
		for(Map.Entry<Long, Integer> e : regs.entrySet()){f.printf("0x%08x  x%d%n", e.getKey(), e.getValue());}
		f.close();
		println("MMIO dump -> "+out+"  ("+rows.size()+" refs, "+regs.size()+" regs)");
	}
}