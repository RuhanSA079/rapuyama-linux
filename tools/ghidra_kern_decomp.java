// ghidra headless POST-script, decompiles every function and writes one .c per
// rom module (by text range from modules.txt) into the same dir
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;
import java.util.*;

public class ghidra_kern_decomp extends GhidraScript{
	public void run() throws Exception{
		String mods=getScriptArgs()[0];
		String dir=new File(mods).getParent();
		List<long[]> rng=new ArrayList<>();
		List<String> names=new ArrayList<>();
		BufferedReader br=new BufferedReader(new FileReader(mods));
		String l;
		while((l=br.readLine())!=null){
			String[] s=l.trim().split(" ");
			rng.add(new long[]{Long.parseLong(s[0], 16), Long.parseLong(s[1], 16)});
			names.add(s[2]);
		}
		br.close();
		Map<String, PrintWriter> out=new HashMap<>();
		DecompInterface d=new DecompInterface();
		d.openProgram(currentProgram);
		int n=0;
		for(Function f : currentProgram.getFunctionManager().getFunctions(true)){
			long a=f.getEntryPoint().getOffset();
			String mod="unknown";
			for(int i=0; i<rng.size(); i++){
				if(a>=rng.get(i)[0] && a<rng.get(i)[1]){mod=names.get(i); break;}
			}
			DecompileResults r=d.decompileFunction(f, 60, monitor);
			if(r==null || !r.decompileCompleted()){continue;}
			PrintWriter w=out.get(mod);
			if(w==null){w=new PrintWriter(new FileWriter(dir+"/"+mod+".c")); out.put(mod, w);}
			w.println("// ==== "+f.getName()+" @ "+f.getEntryPoint());
			w.println(r.getDecompiledFunction().getC());
			n++;
		}
		for(PrintWriter w : out.values()){w.close();}
		println("decompiled "+n+" functions");
	}
}