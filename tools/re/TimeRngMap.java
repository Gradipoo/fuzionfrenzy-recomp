// Addresses and instruction text for time sources and RNG sites.
// No decompiler output. The report is game-derived and belongs under re/.
//@category Xbox

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;

public class TimeRngMap extends GhidraScript {

	private static final String[] KERNEL = {
		"KeQueryPerformanceCounter", "KeQueryPerformanceFrequency", "KeQueryInterruptTime",
		"KeQuerySystemTime", "KeInterruptTime", "KeSystemTime", "KeTickCount", "KeTimeIncrement",
		"KeDelayExecutionThread", "KeStallExecutionProcessor", "KeSetTimer", "KeSetTimerEx",
		"KeCancelTimer", "KeInitializeTimerEx", "NtCreateTimer", "NtSetTimerEx", "NtCancelTimer",
		"NtQueryTimer", "NtSetSystemTime", "RtlTimeFieldsToTime", "RtlTimeToTimeFields",
	};

	private static final int MAX_REFS = 80;

	@Override
	public void run() throws Exception {
		String path = "/tmp/time-rng-map.txt";
		String[] args = getScriptArgs();
		if (args != null && args.length > 0 && args[0] != null && !args[0].isEmpty()) {
			path = args[0];
		}
		PrintWriter out = new PrintWriter(new FileWriter(path));
		try {
			out.println("# time and RNG map");
			out.println("# program " + currentProgram.getName());
			out.println("# format: addresses, symbol names, one instruction. no decompiled code.");
			mapSymbols(out);
			mapRdtsc(out);
			mapImmediate(out, "lcg_343FD", new byte[] {(byte) 0xFD, 0x43, 0x03, 0x00});
			mapImmediate(out, "lcg_269EC3", new byte[] {(byte) 0xC3, (byte) 0x9E, 0x26, 0x00});
			mapImmediate(out, "rng_global_285630", new byte[] {0x30, 0x56, 0x28, 0x00});
			mapAnchors(out);
		} finally {
			out.close();
		}
		println("time-rng map written to " + path);
	}

	private boolean interesting(String name) {
		if (name == null) return false;
		for (String k : KERNEL) {
			if (name.equals(k)) return true;
		}
		String n = name.toLowerCase();
		return n.contains("rand") || n.contains("vblank") || n.contains("verticalblank")
			|| n.contains("blockontime") || n.contains("gettickcount") || n.contains("timegettime")
			|| n.contains("queryperformance");
	}

	private void mapSymbols(PrintWriter out) throws Exception {
		SymbolTable table = currentProgram.getSymbolTable();
		Set<String> seen = new HashSet<>();
		List<Symbol> hits = new ArrayList<>();
		SymbolIterator it = table.getAllSymbols(true);
		while (it.hasNext() && !monitor.isCancelled()) {
			Symbol sym = it.next();
			if (!interesting(sym.getName())) continue;
			String key = sym.getName() + "@" + sym.getAddress();
			if (!seen.add(key)) continue;
			hits.add(sym);
		}
		out.println();
		out.println("## symbols " + hits.size());
		for (Symbol sym : hits) {
			Address addr = sym.getAddress();
			MemoryBlock block = currentProgram.getMemory().getBlock(addr);
			String blockName = block == null ? "" : block.getName();
			int n = 0;
			ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(addr);
			List<Reference> sample = new ArrayList<>();
			while (refs.hasNext()) {
				Reference ref = refs.next();
				n++;
				if (sample.size() < MAX_REFS) sample.add(ref);
			}
			out.println("SYM " + sym.getName() + " " + addr + " block=" + blockName + " xrefs=" + n);
			for (Reference ref : sample) {
				Address from = ref.getFromAddress();
				Function fn = getFunctionContaining(from);
				Instruction ins = getInstructionContaining(from);
				if (ins == null) ins = getInstructionAt(from);
				String fnText = fn == null ? "-" : fn.getName() + "@" + fn.getEntryPoint();
				String insText = ins == null ? "-" : ins.getAddress() + " " + ins;
				out.println("  XREF " + from + " " + ref.getReferenceType().getName() + " fn=" + fnText + " " + insText);
			}
			if (n > sample.size()) out.println("  XREF ... " + (n - sample.size()) + " more");
		}
		Set<String> found = new HashSet<>();
		for (Symbol sym : hits) found.add(sym.getName());
		out.println("## kernel symbols absent");
		for (String k : KERNEL) {
			if (!found.contains(k)) out.println("ABSENT " + k);
		}
	}

	private void mapRdtsc(PrintWriter out) {
		out.println();
		out.println("## rdtsc");
		int n = 0;
		InstructionIterator it = currentProgram.getListing().getInstructions(true);
		while (it.hasNext() && !monitor.isCancelled()) {
			Instruction ins = it.next();
			if (!"RDTSC".equals(ins.getMnemonicString())) continue;
			n++;
			if (n <= MAX_REFS) {
				Function fn = getFunctionContaining(ins.getAddress());
				MemoryBlock block = currentProgram.getMemory().getBlock(ins.getAddress());
				String fnText = fn == null ? "-" : fn.getName() + "@" + fn.getEntryPoint();
				String blockName = block == null ? "" : block.getName();
				out.println("RDTSC " + ins.getAddress() + " block=" + blockName + " fn=" + fnText);
			}
		}
		out.println("RDTSC count=" + n);
		if (n > MAX_REFS) out.println("RDTSC ... " + (n - MAX_REFS) + " more");
	}

	private void mapImmediate(PrintWriter out, String tag, byte[] pattern) throws Exception {
		out.println();
		out.println("## " + tag);
		Memory mem = currentProgram.getMemory();
		Address addr = mem.getMinAddress();
		int n = 0;
		Map<String, Integer> mnems = new HashMap<>();
		while (addr != null && !monitor.isCancelled()) {
			addr = mem.findBytes(addr, pattern, null, true, monitor);
			if (addr == null) break;
			n++;
			Instruction ins = getInstructionContaining(addr);
			String mnem = ins == null ? "none" : ins.getMnemonicString();
			mnems.merge(mnem, 1, Integer::sum);
			if (n <= 200) {
				MemoryBlock block = mem.getBlock(addr);
				Function fn = getFunctionContaining(addr);
				String blockName = block == null ? "" : block.getName();
				String fnText = fn == null ? "-" : fn.getName() + "@" + fn.getEntryPoint();
				String insText = ins == null ? "-" : ins.getAddress() + " " + ins;
				out.println(tag + " " + addr + " block=" + blockName + " fn=" + fnText + " " + insText);
			}
			addr = addr.add(1);
		}
		out.println(tag + " count=" + n);
		if (n > 200) out.println(tag + " ... " + (n - 200) + " more");
		out.println(tag + " mnemonics " + mnems);
	}

	// Known entry points from the XDK layout and from the symbol pass above.
	// Printed as addresses and one instruction, same as the rest of this file.
	private static final long[] ANCHORS = {
		0x4C510, 0x4C5F0, 0x5B410, 0x5B4A0, 0x5C0F0, 0x238A6A,
		0x1E8E5F, 0x1E8E74, 0x30982, 0x1E893C, 0x4F210,
		0x1E133E, 0x1C8B70, 0x1C4420, 0x1C9C90,
		0x1FB020, 0x1F5520, 0x1FD715, 0x1FD7FC, 0x1FD802, 0x202150,
	};

	private void mapAnchors(PrintWriter out) throws Exception {
		out.println();
		out.println("## anchors");
		for (long value : ANCHORS) {
			Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
			Function at = getFunctionAt(addr);
			Function containing = getFunctionContaining(addr);
			Function fn = at != null ? at : containing;
			String fnText = fn == null ? "-" : fn.getName() + " " + fn.getEntryPoint() + ".." + fn.getBody().getMaxAddress();
			int n = 0;
			ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(addr);
			List<Reference> sample = new ArrayList<>();
			while (refs.hasNext()) {
				Reference ref = refs.next();
				n++;
				if (sample.size() < 40) sample.add(ref);
			}
			out.println("ANCHOR " + addr + " fn=" + fnText + " xrefs=" + n);
			for (Reference ref : sample) {
				Address from = ref.getFromAddress();
				Function caller = getFunctionContaining(from);
				Instruction ins = getInstructionContaining(from);
				if (ins == null) ins = getInstructionAt(from);
				String callerText = caller == null ? "-" : caller.getName() + "@" + caller.getEntryPoint();
				String insText = ins == null ? "-" : ins.getAddress() + " " + ins;
				out.println("  XREF " + from + " " + ref.getReferenceType().getName() + " fn=" + callerText + " " + insText);
			}
			if (n > sample.size()) out.println("  XREF ... " + (n - sample.size()) + " more");
			if (fn != null && fn.getEntryPoint().equals(addr)) {
				InstructionIterator it = currentProgram.getListing().getInstructions(fn.getBody(), true);
				int shown = 0;
				while (it.hasNext() && shown < 24) {
					Instruction ins = it.next();
					String mnem = ins.getMnemonicString();
					String text = ins.toString().toUpperCase();
					boolean keep = mnem.startsWith("CALL") || mnem.equals("RDTSC")
						|| text.contains("343FD") || text.contains("269EC3") || text.contains("285630");
					if (!keep) continue;
					out.println("  IN " + ins.getAddress() + " " + ins);
					shown++;
				}
			}
		}
	}
}
