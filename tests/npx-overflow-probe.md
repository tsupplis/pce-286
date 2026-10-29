# Early NPX overflow characterization

This is a **hardware observation tool**, not a pass/fail regression. The unresolved
cases cannot be settled by comparing the emulator with itself or with modern x87.
No hardware results have been collected yet.

Build with `make npx-overflow-probe` (NASM required). Copy `tests/npxovf.com` to
real-mode DOS on an 8087 or 80287 machine and run `NPXOVF`. A coprocessor must be
present. It writes/overwrites **NPXOVF.BIN in the current DOS directory**, resets
the NPX state, and leaves it initialized. It does not access raw disks. Do not run
this on a machine without an NPX or inside an environment that traps CLI.
The current experimental PCE NPX lacks the control/memory forms used by this
probe; running it there is not yet an acceptance test.

Decode a captured file with:

```
python3 tests/decode_npx_overflow.py NPXOVF.BIN > observation.csv
```

Keep the unmodified BIN, record CPU/NPX manufacturer and exact part marking,
DOS version, and physical hardware versus emulator. Do not present emulator
observations as physical-hardware evidence. A comparison may expose model/revision
differences rather than establish one universal answer.

The 192 cases cover both signs, PC=24/53/64, all four rounding controls, and both
OM/PM masks for:

1. True exponent overflow: largest finite temporary-real value plus itself.
2. Precision-rounding boundary: largest significand at PC plus half its ULP,
   at the highest finite exponent. Directed inward/chop controls provide the
   adjacent non-overflow observations as well.

Each case initializes the NPX, loads normal operands, executes FADDP, captures
94 bytes with **FNSAVE without an intervening WAIT**, then waits for the save to
complete. IF is disabled only during that sequence and restored before DOS I/O.
This follows Intel 80286/80287 PRM figure 4-3; it avoids a waiting store before
an unmasked exception has been captured. The reset control word selects the 8087 IEM mask, preventing its INT output
(which can be wired to NMI) during capture; arithmetic OM/PM remain independently
selected. The 287 keeps its reserved bit clear. This probe does not characterize
8087 IEM-clear interrupt delivery. Hardware line/interrupt-controller behavior
still depends on the target board.

File format: eight-byte `NPXOVF1\0` signature, little-endian u16 count (192),
u16 record size (118). Each record has u16 id, u16 input CW, two raw 10-byte
operands, and the 94-byte real-mode FNSAVE image. The decoder reports saved CW,
SW, tags and logical ST(0) without applying an expected-result policy. It checks
metadata and file integrity, not arithmetic correctness. A freshly built COM's
zero-filled capture area is **not** a hardware observation.

Questions to settle:

- Directed finite saturation at reduced PC: which low significand bits survive?
- Unmasked rounding-carry overflow: delivered significand/exponent and OE/PE/C1?
- Does either answer differ between 8087 and 80287 revisions?

The conflicting unnormal rows of Intel table 1-11 and its unusual full-precision
finite result remain documented; this probe deliberately uses normal operands.
Unsynchronized ESC while BUSY is not probed: Intel requires WAIT synchronization
on 8087, and a synchronous emulator cannot establish concurrent hardware behavior.

References: Intel 80286/80287 PRM, table 1-11 and figure 4-3;
Intel AP-113, instruction/error synchronization.
