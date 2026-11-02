# V20/V30 native-mode review — 2026-10-26

Scope: native instruction behavior, model selection and debugger/API support.
8080 core and emulation-mode implementation are deferred to steps 2 and 3.
The original review was read-only; subsequent implementation and closure are recorded below.

## Current closure — 2026-10-26

The reviewed native inventory is implemented and tested: all 13 NEC-specific
families, CHKIND, full-byte shift counts, FFFF:0000 reset/native MD and
model-aware disassembly. All 54 targeted audit checks now pass; no known-failure
exemptions remain in the global gate.

The final native suite adds 36,211 checks, using independent digit-by-digit BCD
and bit-by-bit field oracles. It covers carry/borrow chains, lengths and offsets,
word crossing, offset wrap, pointer/count preservation, segment overrides,
reset, shift counts and disassembly. Existing 9,224 bit/nibble checks and the
864-case cross-model repeat matrix also pass. Expanded DOS instruction guests
pass on both V20 and V30; XT disk/input/reboot and OS/2 Window smoke pass.

This closes the agreed native implementation inventory. It does not certify
cycle/bus timing, all shared-ISA edge cases, native interrupt/NMI restart or
undocumented behavior. Undefined flags and invalid BCD inputs are not given
invented hardware guarantees. The 8080 core review and NEC 8080 mode remain
separate next steps. See [global coverage](cpu-compliance.md) and status.md.

## Original assessment (before repair)

The assessment tables below are historical. See current closure immediately above.

### Assessment

The models are selectable and inherit a substantial 8086/80186 execution core,
but NEC-native support is not complete or reliable. None of the 13 NEC-specific
native instruction families below is fully accepted. This is a family-level
inventory, not a percentage of the entire ISA or DOS compatibility.

Existing test_v20 and test_v20ext pass, but their bit-operation byte sequences
and some expected flag/rotate results reproduce the implementation's mistakes.
They must not be used as NEC conformance evidence. The independent audit has
54 checks and 50 failures (the same defects occur on both bus variants).
The four passes are the ROR4 data example and a basic MOV/ADD/PUSH/POP sequence
on each model. These deliberately selected cases are not an ISA coverage sample.

## Native coverage and confirmed findings

| Area | State | Finding / impact |
| --- | --- | --- |
| Model selection / host callbacks | Present | `e86_set_v20/v30`, machine config names, memory/port callbacks and normal stepping work. V20 sets the 8-bit-bus flag and four-byte queue; V30 uses six bytes. Bus timing accuracy is not established. |
| Shared 8086/80186 instructions | Substantial implementation; incomplete NEC validation | Inherits 80186 handlers. Basic MOV/ADD/PUSH/POP passes. Native differences cannot be inferred from 80186 tests. |
| TEST1, CLR1, SET1, NOT1 (4 families) | Incorrect | Manual encodings are 0F 10..1F followed by ModRM (byte/word and CL/immediate variants). Current dispatcher treats the second opcode byte itself as ModRM. All 16 documented forms fail the probe on each model; word variants have no implementation. Modifying bit operations must preserve flags, but current handlers change them; TEST1 must clear carry rather than copy the tested bit into it. |
| ROL4 | Incorrect | Performs right rotation, identical to ROR4. Manual example BL=95h, AL=03h must yield BL=53h and low AL=9; it fails. |
| ROR4 | Partial | Manual data example passes, but both nibble rotates change flags which should be preserved. Upper AL nibble is undefined and is deliberately not asserted. |
| ADD4S, SUB4S, CMP4S (3 families) | Missing | No BCD-string handlers. Their opcodes fall into the erroneous bit-operation dispatcher rather than executing BCD strings. |
| INS, EXT (2 families) | Missing | No NEC bit-field handlers (register and immediate length forms). These are distinct from the inherited port-string instructions INS/OUTS. Their opcodes also fall into the erroneous dispatcher. |
| REPC, REPNC (2 families) | Missing/incorrect | Correct native prefixes are 65h and 64h respectively. Neither is installed. F1h is incorrectly installed as REPC and behaves as ordinary REP without checking carry. Initial carry is irrelevant; comparison results control termination. |
| CHKIND / BOUND | Broken native opcode | 62h should check signed bounds. It is overwritten with a purported RETEM stub and raises INT 6 even for an in-range operand. This is a native regression independent of adding 8080 mode. |
| Shift counts | Incorrect inherited model policy | 80186 setup enables five-bit count masking. NEC's CL-count operation uses the full byte. SHL AX,CL with AX=1, CL=32 incorrectly leaves AX=1 instead of zero. Immediate-count and other shifts/rotates need the same policy audit. |
| Reset | Incorrect visible state | NEC specifies CS:IP=FFFF:0000; shared reset uses F000:FFF0. Both fetch physical FFFF0h, so boot can work despite different visible registers. Native MD state also needs an explicit representation for step 3. |
| Disassembly / CPU API | Incomplete | Execution has model setters, but `e86_disasm` has no model argument; `e86_disasm_mem` simply calls that same decoder. The 0F decoder is 286-specific, native TEST1 is not decoded, and F1/63 are mislabeled NEC instructions globally. A model-aware disassembly path is needed without breaking existing callers. |
| Timing, interrupts and edge cases | Not certified | Per-opcode timings are mostly inherited, with incorrect known ROR4 cycle constants. Native interrupt restart/NMI behavior, reserved opcodes, division differences and all shared-instruction flags still need focused verification. Do not call the base ISA fully conformant. |

The 13-family count is: four bit families, two nibble rotations, three BCD-string
families, two bit-field families and two carry-repeat prefixes. It excludes
8080 mode and CHKIND/shared-instruction differences.

## Evidence and reproduction

Primary reference: NEC **uPD70108/70116 User's Manual**, original V20/V30,
[manual PDF](https://www.dosdays.co.uk/media/nec/NEC-V20-V30-Users-Manual.pdf).
Not V30MZ or V25/V35, which have different instruction subsets.
Relevant printed pages: reset 7-1; carry repeats 12-14; bit-field instructions
12-21 onward; BCD strings 12-42 onward; ROL4/ROR4 12-45/46; bit instructions
12-81 through 12-96; shifts 12-105 onward; CHKIND 12-170.

Run from repository root:

```sh
make tests/test_v20 tests/test_v20ext
cc -Isrc tests/test_v20_native_audit.c src/cpu/e8086/e8086.a -o /tmp/test_v20_native_audit
/tmp/test_v20_native_audit
```

The audit intentionally exits nonzero while the defects remain. It is separate
from `check-unit`, not an expected-failure test that masks regressions.
Logs: `tests/memory-validation/v20-native-review/{audit,legacy-v20,legacy-v20ext}.log`.
The probe covers both models, register forms, flags, prefix termination, reset,
shift count, CHKIND and disassembly. Memory/displacement/segment variants and
boundary lengths must be added during fixes; missing-family findings are source
inspection findings rather than assertions of exhaustive runtime coverage.

## Recommended native-mode repair order

1. Replace the extension dispatcher with the documented encoding map; restore
   native 62h; correct rotates and flags; replace self-consistent legacy tests
   with manual-derived register/memory and byte/word cases.
2. Implement carry-repeat, BCD strings and bit-field instructions, with short
   targeted tests for termination, lengths, segment overrides and wraparound.
3. Correct NEC model policies (shift counts, reset/native flags), provide
   model-aware disassembly, and verify native interrupt/flag differences.
4. Run the existing DOS guest cases on both V20 and V30 plus the short XT
   keyboard/disk/reboot regression after shared-path changes. Preserve the 286
   suite and OS/2 smoke when any shared path is modified.

Then proceed with the separate 8080-core review and NEC emulation-mode work.

## Earlier repair checkpoint — superseded by native closure

Implemented the documented 0F 10..1F bit decoder (including word forms), corrected
bit-operation flags, ROL4 direction and both rotate flag behaviors. Restored native
62h CHKIND; removed false native CALLN/RETEM and F1 REPC overrides. Unsupported
extensions no longer silently execute an unrelated bit operation. Replaced the
legacy C and DOS tests with manual-derived encodings and expectations.

Implemented 64h REPNC and 65h REPC in the shared comparison-string path. Positive
regression tests cover CMPS/SCAS byte/word, DF, zero count, termination and initial
flags across all seven CPU models. Intel REPE/REPNE behavior remains covered.

9,224 extension checks pass. The original targeted audit improves from 50 to
6 remaining failures in 54 checks: shift-count policy, reset CS:IP and disassembly
on each NEC model. These are still explicit known gaps. BCD strings and INS/EXT
are still missing and are source-review gaps outside that 54-check probe.

Global regression command and coverage limits: [CPU regression](cpu-compliance.md).
Corrected native DOS guest passes on V20 and V30; XT keyboard/disk/reboot passes.


## VCPM compatibility finding — 2026-10-27

The real VCPM detector exposed an omitted shared-instruction difference:
D5 10 with AX=0F0Fh must yield A5h on NEC, not Intel's FFh. Fixed with NEC-only
AAD/AAM handlers (fixed decimal base), preserving Intel behavior and PCE's
explicit host-service hook. The exact detector and conversion matrix are now
in `test_nec_native` (298,361 checks total). Full application acceptance is
still pending; this finding narrows the earlier inventory-closure claim.
Reference: NEC manual printed page 12-63, CVTBO/CVTOB, and the supplied VCPM
AAD10 macro/detection routine. Timing constants are 15/7; no broader cycle
accuracy claim is made.
