# Early NPX overflow characterization

This is a **hardware observation tool**, not a pass/fail regression. The unresolved
cases cannot be settled by comparing the emulator with itself or with modern x87.
No hardware results have been collected yet.

Build with `make npx-overflow-probe` (NASM required). Copy `tests/npxovf.com` to
real-mode DOS on an 8087 or 80287 machine and run `NPXOVF`. A coprocessor must be
present. It writes/overwrites **NPXOVF.BIN in the current DOS directory**, resets
the NPX state, and leaves it initialized. It does not access raw disks. Do not run
this on a machine without an NPX or inside an environment that traps CLI.
The experimental PCE NPX now implements the required memory controls in its
optional test interface, but has no normal guest configuration option and still
rejects the unresolved overflow cases. Running this probe there is not hardware
characterization or an acceptance test.

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

## Transcendental capture

Build with `make npx-trans-probe`. Run `NPXTRANS.COM` under DOS on a known
original 8087 or 80287, then decode its `NPXTRANS.BIN` using:

```sh
python3 tests/decode_npx_trans.py NPXTRANS.BIN
```

Record chip model/revision and machine alongside the file. A 387, emulator, or
zero-filled COM template is not evidence for the original chips. This utility
requires an installed coprocessor; it does not detect one or install a handler.
Like the overflow probe, it masks 8087 interrupt delivery with IEM, disables
maskable CPU interrupts during capture, and uses no-wait FNSAVE immediately
after the tested instruction. It leaves interrupts restored before DOS I/O.

The 80 cases cover four RC settings and all-masked versus I/O/U/P-unmasked:
FPTAN zero, tiny-path boundary, ordinary pair, empty/full stack; F2XM1 minimum
normal and negative zero; FYL2X overflow; FYL2XP1 underflow. Normal setup loads
Y then X while exceptions are masked, then installs the test control word.
Full setup fills all eight registers; empty setup loads none.

Each 122-byte record holds id/CW/opcode/setup, two real80 inputs, and a 94-byte
FNSAVE image. Header: `NPXTRN1` plus NUL, 16-bit count 80 and record size 122.
The decoder prints all eight logical register payloads, status and physical tag
word. It validates structural metadata but deliberately reports no PASS/FAIL.
The supplied program has been assembled and its template format checked, but
**has not been executed on physical hardware**.
