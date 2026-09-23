// Headless post-script: pins DS to DGROUP, creates functions at the indexed image offsets,
// and writes every decompiled function to one C file.
// args: <starts.txt (hex image offsets) or -> <out.c> <dgroup segment hex, e.g. 178F> [timeout_s] [only_starts]
// With "-" as starts, only the functions Ghidra found itself are decompiled.
// With only_starts = "only", just the functions whose entry is listed in starts.txt are decompiled
// (other functions, e.g. names applied inside hand-written asm, stay named but are skipped).
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.PrintWriter;
import java.math.BigInteger;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

public class DecompileAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        List<String> starts = args[0].equals("-") ? java.util.Collections.emptyList() : Files.readAllLines(Paths.get(args[0]));
        int dgroup = Integer.parseInt(args[2], 16);
        int timeout = args.length > 3 ? Integer.parseInt(args[3]) : 300;
        boolean onlyStarts = args.length > 4 && args[4].equals("only");
        java.util.Set<Long> startSet = new java.util.HashSet<>();
        for (String s : starts) if (!s.trim().isEmpty()) startSet.add(Long.parseLong(s.trim(), 16));

        Address base = currentProgram.getMinAddress();
        println("image base address: " + base);
        for (ghidra.program.model.mem.MemoryBlock b : currentProgram.getMemory().getBlocks())
            println(String.format("block %-12s %s - %s", b.getName(), b.getStart(), b.getEnd()));
        int loadSeg = (int) (base.getOffset() >> 4);

        // Code lives below DGROUP; tell the decompiler DS always equals DGROUP.
        Register ds = currentProgram.getRegister("ds");
        Address codeEnd = base.add(((long) dgroup << 4) - 1);
        currentProgram.getProgramContext().setValue(ds, base, codeEnd, BigInteger.valueOf(loadSeg + dgroup));

        int created = 0;
        for (String s : starts) {
            s = s.trim();
            if (s.isEmpty()) continue;
            Address a = base.add(Long.parseLong(s, 16));
            if (getInstructionAt(a) == null) {
                new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
            }
            if (getFunctionContaining(a) == null) {  // don't split functions Ghidra already found
                Function f = createFunction(a, String.format("fn_%s", s));
                if (f != null) created++;
            }
        }
        println("functions created: " + created);

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(args[1], "UTF-8")) {
            out.println("// " + currentProgram.getName() + " decompilation (Ghidra, headless). Addresses: segment:offset; image offset = linear - base.");
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            int n = 0, failed = 0;
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                long img = f.getEntryPoint().getOffset() - base.getOffset();
                if (onlyStarts && !startSet.contains(img)) continue;
                DecompileResults r = decomp.decompileFunction(f, timeout, monitor);
                out.printf("%n// ==== %s  image 0x%05x  entry %s ====%n", f.getName(), img, f.getEntryPoint());
                if (r != null && r.decompileCompleted()) {
                    out.println(r.getDecompiledFunction().getC());
                    n++;
                } else {
                    out.println("// decompile failed: " + (r == null ? "null" : r.getErrorMessage()));
                    failed++;
                }
            }
            println("decompiled " + n + " functions, " + failed + " failed");
        }
        decomp.dispose();
    }
}
