# Global CPU regression and conformance coverage

## Run the gate

```sh
make check-cpu-regression
```

Builds current tests and runs a ROM-free regression grouped by CPU family:
8086/8088, 80186/80188, 80286 and V20/V30. Current runtime is normally below
three seconds after compilation. Each run saves full logs and `results.json`
in a new directory under `tests/memory-validation/cpu-regression/`.

Compare against a previous run after a change:

```sh
python3 tests/run_cpu_regression.py --compare /absolute/path/to/previous/results.json
```

The runner returns failure on any test failure, timeout or crash, including the
NEC audit. No known-failure exemptions remain. Passing this regression gate
covers the tested scope only; it is not exhaustive hardware conformance.

## Coverage as of 2026-10-26

| Profile | Automated evidence | Limits |
| --- | --- | --- |
| 8086 / 8088 | 2,506 arithmetic/shift checks each; shared comparison-repeat matrix; XT guest keyboard/disk/reboot acceptance | Independent audit of the entire ISA is still open. |
| 80186 / 80188 | Same 2,506 checks each with Intel-specific shift counts; existing 186 extension C/guest tests; shared repeat matrix | Existing extension tests need expansion to full edge-case and fault coverage. |
| 80286 | Existing protection, task/gate, privilege, reset, LOADALL, exceptions, addressing and disassembly tests; shared repeat matrix | OS/2 smoke is a separate integration test; not cycle-accurate certification. |
| V20 / V30 | 9,224 byte/word/register/memory bit and nibble checks; native CHKIND/model smoke; carry-repeat matrix; 36,211 BCD/bit-field/reset/shift/disassembly checks; all 54 audit checks; expanded DOS guests | Reviewed native inventory closed; 8080 mode, cycle accuracy and exhaustive shared-ISA/interrupt conformance remain outside this closure. |

`test_x86_compliance.c`: independent arithmetic oracle for ADD/ADC/SUB/SBB/CMP
on byte operands, signed overflow, carry/borrow, auxiliary carry/borrow, parity,
zero/sign flags, preserved direction flag and instruction length. Tests boundary
operand pairs with both incoming carry values. SHL AX,CL tests every count
0..255 against each Intel model's count rule. This is **not** exhaustive arithmetic
or shift coverage: word arithmetic, memory forms and the other shift/rotate
operations remain to be added.

`test_repeat_compliance.c`: 864 cases across all seven models. CMPS/SCAS,
byte/word, both directions, zero/one/multiple counts, both initial flag states;
REPE/REPNE on all models and REPC/REPNC on NEC. Checks termination, CX, pointers,
instruction completion and zero-count flag preservation. Initial CF/ZF does not
replace the first comparison. Interrupt-between-iterations testing remains separate.

## Why NEC tests cannot replace Intel profiles

Reuse the harness and common instruction cases, but derive each profile from its
own specification. Instruction availability, shift counts, reset state, flags,
interrupt restart and undefined behavior differ. Do not infer an Intel pass from
a NEC pass, or derive expected results from the emulator under test.

Primary references:
- [Intel 8086 Family User's Manual, October 1979](https://bitsavers.org/components/intel/8086/9800722-03_The_8086_Family_Users_Manual_Oct79.pdf), arithmetic and shift descriptions.
- [Intel iAPX 86/88/186/188 Programmer's Reference, 1983](https://ftpmirror.your.org/pub/misc/bitsavers/components/intel/80186/210911-001_iAPX86_88_186_188_Programmers_Reference_1983.pdf), arithmetic and processor differences, especially “SHIFT/ROTATE BY VALUE GREATER THAN 31”.
- [NEC uPD70108/70116 User's Manual](https://www.dosdays.co.uk/media/nec/NEC-V20-V30-Users-Manual.pdf), printed pages 12-14, 12-45/46 and bit-operation section.

## Expansion checklist

- [x] One fast command, grouped reports, retained baseline and comparison.
- [x] Separate model expectations; all NEC audit failures resolved and mandatory.
- [x] Arithmetic boundary/flag baseline and all CL shift counts for Intel profiles.
- [x] Shared repeat-path coverage on all seven models.
- [ ] Word arithmetic and all operand/addressing forms; stack and segment wrap differences.
- [ ] Complete shifts/rotates, decimal adjust, multiply/divide and undefined-flag policy.
- [ ] Prefix combinations, interrupt restart/shadow, reset and exception differences.
- [x] Native NEC inventory closure, with positive tests and no failure exemptions.
- [ ] Full 80186 extension boundary coverage.
- [ ] Independently sourced hardware traces/test vectors where manuals leave ambiguity.

## Change workflow

1. Run the global gate before editing a shared CPU path; keep the report.
2. Add a small failing, specification-derived case for the actual defect.
3. Fix it, rerun the global gate and compare with the saved report.
4. For shared paths, run the short XT keyboard/write/readback/reboot check. Run
   OS/2 Window smoke when protected-mode execution or a shared instruction path
   used by OS/2 changes. Native-only changes also run the V20/V30 DOS guest.
5. Update status with resolved findings, remaining gaps and evidence. Do not
   start a broad soak campaign without a concrete coverage question.

## Native closure comparison

The final 20261026-233855-213742 report resolves all six failures from
20261026-233346-408295, with no new failures in any CPU profile. Full C tests,
V20/V30 instruction guests, XT keyboard/disk/reboot and OS/2 Window ECHO/VER
also pass. See status.md for retained evidence paths and sanitizer scope.


## 8080 addition — 2026-10-27

The gate also runs `test_8080_audit`: 3,156,288 passing checks after all five
review findings were repaired. Register/memory/immediate byte arithmetic,
logical flags, decimal additions, callbacks and interrupt handling are covered;
focused Z80 flag and IM1/IM2 checks protect the shared core. This does not imply
complete Z80 conformance or implementation of NEC 8080 mode. See [review and
repair evidence](8080-review.md).


## NEC emulation-mode addition — 2026-10-27

`test_nec_8080` adds 564 transition, mapping, bus and bounded-guest checks to the
global gate. [Mode coverage and remaining limits](nec-8080-mode.md) supersede
previous statements that NEC 8080 mode was not implemented.

### Real-mode 286 interrupt-stack wrapping (2026-10-28)

`test_286_prot` covers INT/IRET frames at SP 0 through 8 and near FFFFh,
including wrapped even offsets, shutdown at odd SP 1/3/5 without partial stack
writes, and cached short/expand-down SS limits. The old CPU fails four cases.
This closes the DR DOS DOSKEY startup reboot observed on the native AT setup.
See `tests/memory-validation/drdos-int-stack-20261028/findings.txt` for traces,
Intel PRM references and the DR DOS/XT/OS2 acceptance scope.
