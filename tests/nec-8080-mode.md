# NEC V20/V30 8080 mode — 2026-10-27

## Implemented

`src/cpu/e8086/nec8080.c` adapts the existing 8080 engine; it does not duplicate
its instruction handlers. The native CPU registers remain authoritative and
are synchronized before/after each 8080 instruction:

| 8080 | NEC/native register or bus |
| --- | --- |
| A | AL (AH preserved) |
| BC | CX |
| DE | DX |
| HL | BX |
| SP | BP |
| instruction fetch | CS:IP, 16-bit offset wrap |
| data and 8080 stack | DS:offset, 16-bit offset wrap |
| native transition stack | SS:SP |
| ports | existing machine port callbacks |

BRKEM (`0F FF imm8`) saves a native frame, enables MD writes and enters the
vector's emulation segment. CALLN (`ED ED imm8`) saves the emulation return
frame on the native stack and enters native code. IRET restores MD and resumes
8080 execution. RETEM (`ED FD`) restores the BRKEM frame and disables MD writes.
POPF/IRET cannot clear MD outside a BRKEM/RETEM interval. Native PUSHF exposes
NEC MD rather than forcing all high bits to one.

External interrupts and single-step traps enter native handlers. EI delay and
HLT wakeup are covered, including disabled INT waking an emulation HLT without
vectoring. The existing trap API can deliver an NMI-style vector; this does not
add a new motherboard NMI input. Reset aborts emulation. Disassembly follows the
current mode and recognizes all three transition instructions.

The standalone 8080 core has an optional instruction-fetch callback so the
adapter can distinguish CS instruction fetches from DS data accesses. Existing
users retain unified memory when this callback is absent. The standalone Intel
interrupt engine is not used for NEC native-vector delivery.

## Verification

`test_nec_8080` is in the full C suite and `make check-cpu-regression`.
564 checks cover both V20 and V30: register preservation, segmented fetch/data,
independent stacks, native/emulation round trips, native handler register changes,
interrupt/IRET and single-step returns, reset, HLT, ports, fetch wrap and debugger
decoding. A bounded 8080 guest fills and verifies 256 bytes before RETEM.

- Global CPU gate and full C suite pass, including the existing 3,156,288-check
  standalone 8080 suite and native NEC tests.
- ASan/UBSan passes the adapter suite (leak detection disabled).
- XT input/disk/reboot passes on a disposable disk; source disk unchanged.
- OS/2 diagnostic disk boots and windowed ECHO NEC8080OK / VER 1.30 pass;
  screenshot inspected, guest stopped, user installation untouched.
- Native-mode test fixtures now explicitly set MD=1 when setting test flags;
  their existing arithmetic/flag expectations were retained.

Evidence: `tests/memory-validation/nec8080-mode/`,
`tests/memory-validation/xt-nec8080-20261027/result.txt`, and
`tests/memory-validation/os2-desktop/nec8080.png` / `.log`.
XT/OS2 smoke used the build immediately before the final NEC-only single-step
addition; that addition passed the final global/C/sanitizer suites.

## Remaining limits

This is functional adapter acceptance, not exhaustive NEC hardware conformance.
8080 instructions currently use the engine's timing estimates, not NEC bus-cycle
accuracy. Undocumented opcode behavior and all NEC-vs-Intel corner cases are not
certified. The manual explicitly disallows nested BRKEM while an emulation
session is active; it remains unsupported. A full CP/M-style application/OS and
an independent external diagnostic binary have not yet been accepted. These
are the next integration checks, not evidence of a known application failure.

Reference: [NEC uPD70108/70116 User's Manual](https://www.dosdays.co.uk/media/nec/NEC-V20-V30-Users-Manual.pdf), section 8 (register mapping and modes), POP PSW/RETI MD restrictions, and printed pages 12-169, 12-176/177 (transitions).


## Real application acceptance — 2026-10-27

The user's screenshot confirms `vcpm asm helloa` completes CP/M Assembler 2.0
and creates assembly outputs. After direct HEXCOM conversion and renaming
HELLOA.COM to HELLOA.CPM, `vcpm helloa` prints its greeting and returns to
CP/M-86. This supersedes the earlier absence of real-application evidence for
this workflow. Independent diagnostic and broader compatibility remain open.
