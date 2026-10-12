// decompiles every function to c and dumps it all to a file to grep offline
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;
public class ghidra_decomp extends GhidraScript{
	public void run() throws Exception{
		DecompInterface d=new DecompInterface();
		d.openProgram(currentProgram);
		PrintWriter w=new PrintWriter(new FileWriter("/home/fluffles/Documents/Coding/other-projects/e6-linux/work/ghidra/"+currentProgram.getName()+".c"));
		FunctionManager fm=currentProgram.getFunctionManager();
		int n=0;
		for(Function f : fm.getFunctions(true)){
			DecompileResults r=d.decompileFunction(f, 30, monitor);
			if(r!=null && r.decompileCompleted()){
				w.println("// ==== "+f.getName()+" @ "+f.getEntryPoint());
				w.println(r.getDecompiledFunction().getC());
				n++;
			}
		}
		w.close();
		println("decompiled "+n+" functions");
	}
}