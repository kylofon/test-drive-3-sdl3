// Headless post-script (run after auto-analysis, before ApplySymbols/DecompileAll).
// Ghidra shows code past a 64 KB linear boundary with a normalised segment (1e12:2000 is treated as
// 2000:0120) and wraps near branch targets in *that* segment. In TDIII segment 0e12 (Ghidra 1e12)
// crosses linear 0x20000 at offset 1EE0, so backward near calls from its upper part land in DGROUP.
// For every relative call/jump this recomputes the target inside the block's own segment; where
// Ghidra's flow differs it adds a call/jump override reference (which the decompiler honours),
// disassembles the real target and makes a function for calls. Functions Ghidra created in DGROUP
// are removed (there is no code there).
// args: <dgroup segment hex, e.g. 1BE4>
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.*;

import java.util.ArrayList;
import java.util.List;

public class FixNearFlows extends GhidraScript {
    @Override
    public void run() throws Exception {
        int dgroup = Integer.parseInt(getScriptArgs()[0], 16);
        Address base = currentProgram.getMinAddress();
        long dgLinear = base.getOffset() + ((long) dgroup << 4);
        SegmentedAddressSpace space = (SegmentedAddressSpace) base.getAddressSpace();
        Listing listing = currentProgram.getListing();
        ReferenceManager refs = currentProgram.getReferenceManager();
        int fixed = 0, unfixable = 0;
        List<Address> newCalls = new ArrayList<>();

        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (!block.isExecute() && !block.getName().startsWith("CODE")) continue;
            if (!(block.getStart() instanceof SegmentedAddress)) continue;
            if (block.getStart().getOffset() >= dgLinear) continue;
            int seg = ((SegmentedAddress) block.getStart()).getSegment();
            long segLinear = (long) seg << 4;
            for (Instruction ins : listing.getInstructions(new AddressSet(block.getStart(), block.getEnd()), true)) {
                byte[] b = ins.getBytes();
                int op = b[0] & 0xFF;
                int rel;
                if ((op == 0xE8 || op == 0xE9) && b.length >= 3) rel = (short) ((b[1] & 0xFF) | (b[2] << 8));
                else if ((op == 0xEB || (op >= 0x70 && op <= 0x7F) || (op >= 0xE0 && op <= 0xE3)) && b.length >= 2) rel = b[1];
                else continue;
                long off = ins.getAddress().getOffset() - segLinear;
                int target = (int) ((off + b.length + rel) & 0xFFFF);
                Address want = space.getAddress(seg, target);
                Address[] flows = ins.getFlows();
                if (flows.length == 1 && flows[0].getOffset() == want.getOffset()) continue;
                if (op == 0xE8 || op == 0xE9) {
                    RefType type = op == 0xE8 ? RefType.CALL_OVERRIDE_UNCONDITIONAL : RefType.JUMP_OVERRIDE_UNCONDITIONAL;
                    for (Reference r : refs.getFlowReferencesFrom(ins.getAddress())) refs.delete(r);
                    Reference r = refs.addMemoryReference(ins.getAddress(), want, type, SourceType.USER_DEFINED, 0);
                    refs.setPrimary(r, true);
                    if (op == 0xE8) newCalls.add(want);
                    else new DisassembleCommand(want, null, true).applyTo(currentProgram, monitor);
                    fixed++;
                } else {
                    println("cannot override conditional/short branch at " + ins.getAddress() + " -> " + want);
                    unfixable++;
                }
            }
        }
        for (Address a : newCalls) {
            if (listing.getInstructionAt(a) == null) new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
            if (listing.getFunctionAt(a) == null) createFunction(a, null);
        }
        int removed = 0;
        FunctionManager fm = currentProgram.getFunctionManager();
        for (Function f : fm.getFunctions(true)) {
            if (f.getEntryPoint().getOffset() >= dgLinear) {
                fm.removeFunction(f.getEntryPoint());
                removed++;
            }
        }
        println(String.format("near flows fixed: %d, not fixable: %d, DGROUP functions removed: %d", fixed, unfixable, removed));
    }
}
