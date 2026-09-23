// Headless pre-script: before auto-analysis, tell Ghidra that DS = DGROUP in all code below DGROUP,
// so data references are resolved against the right segment.
// args: <dgroup segment hex, e.g. 178F>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;

import java.math.BigInteger;

public class SetDS extends GhidraScript {
    @Override
    public void run() throws Exception {
        int dgroup = Integer.parseInt(getScriptArgs()[0], 16);
        Address base = currentProgram.getMinAddress();
        int loadSeg = (int) (base.getOffset() >> 4);
        Register ds = currentProgram.getRegister("ds");
        Address codeEnd = base.add(((long) dgroup << 4) - 1);
        currentProgram.getProgramContext().setValue(ds, base, codeEnd, BigInteger.valueOf(loadSeg + dgroup));
        println(String.format("DS = %04X for %s - %s", loadSeg + dgroup, base, codeEnd));
    }
}
