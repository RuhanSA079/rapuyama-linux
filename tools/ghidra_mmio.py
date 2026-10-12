# ghidra headless post-script (jython)
# dumps every reference into the peripheral window [0xE0000000, 0xE1000000)
# per function with its access type, to build a code->register map
# output: <project_parent>/<program_name>.mmio.txt
import os
from ghidra.program.model.symbol import RefType

LO = 0xE0000000
HI = 0xE1000000

prog = currentProgram
fm = prog.getFunctionManager()
listing = prog.getListing()

rows = [] # (target, func, from_addr, access)
for fn in fm.getFunctions(True):
    body = fn.getBody()
    it = listing.getInstructions(body, True)
    while it.hasNext():
        insn = it.next()
        for ref in insn.getReferencesFrom():
            t = ref.getToAddress()
            if t is None:
                continue
            off = t.getOffset() & 0xFFFFFFFF
            if LO <= off < HI:
                rt = ref.getReferenceType()
                acc = "W" if rt.isWrite() else "R" if rt.isRead() else "?"
                rows.append((off, fn.getName(), insn.getAddress().getOffset() & 0xFFFFFFFF, acc))

rows.sort()
name = prog.getName()
out = os.path.join(os.path.dirname(prog.getExecutablePath()), name + ".mmio.txt")
# executablePath can point into the imported file dir, fall back to work/ghidra
try:
    f = open(out, "w")
except Exception:
    out = "/home/fluffles/Documents/Coding/other-projects/e6-linux/work/ghidra/" + name + ".mmio.txt"
    f = open(out, "w")

f.write("# MMIO references in %s  (window %#x..%#x)\n" % (name, LO, HI))
f.write("# target     access  function                      from\n")
seen_targets = {}
for off, fname, frm, acc in rows:
    f.write("%#010x  %s  %-30s %#010x\n" % (off, acc, fname, frm))
    seen_targets.setdefault(off, set()).add(acc)
f.write("\n# distinct target registers: %d\n" % len(seen_targets))
for off in sorted(seen_targets):
    f.write("%#010x  %s\n" % (off, "".join(sorted(seen_targets[off]))))
f.close()
print("MMIO dump written to %s (%d refs, %d distinct regs)" % (out, len(rows), len(seen_targets)))