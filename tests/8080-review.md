# Existing 8080 core review — 2026-10-26

Scope: review the reusable core before adding NEC 8080 emulation mode. The initial review made no CPU changes; the repair checkpoint below records subsequent fixes. This is a focused baseline,
not exhaustive 8080 or Z80 certification.

## Current repair checkpoint — 2026-10-27

All five findings below are fixed. `make check-8080-audit` now passes and is part
of both the full C suite and the global CPU regression gate, with no exemptions.
The original result and findings below are retained as historical evidence.

- SUB/SBB/CMP/DCR use Intel 8080 auxiliary carry; ANA/ANI use operand bit-3 OR.
  Z80's separate flag helpers retain its own H behavior.
- Separate input callback registration preserves the memory context.
- INT is sampled as a level; explicit EI delay covers EI/EI, EI/DI and EI/HLT.
- New `e8080_set_inta_fct` supplies opcode and operand bytes by index. A supplied
  instruction consumes no bytes from program memory; CALL/RST save the exact
  resume PC. A missing supplier models FF (RST 7). This is an Intel 8080 API;
  Z80 IM0 remains outside this repair. Existing Z80 IM1/IM2 are tested.

Verification: 3,156,288 checks pass, including all byte operand/carry combinations
for register, memory and immediate arithmetic/logical forms, valid BCD additions,
all RST vectors, supplied CALL/MVI, interrupt level withdrawal, EI sequencing,
HLT wakeup, Z80 IM1/IM2 and selected Z80 flag differences. Full C tests and the
8086/80186/80286/V20 global gate pass. ASan/UBSan passes the same suite with
leak detection disabled. This is functional verification, not bus-cycle timing
or exhaustive Z80 certification. No x86 implementation paths were changed.

Evidence: `tests/memory-validation/8080-fixed/`. Next: NEC-specific 8080 adapter
and mode transitions; broader diagnostic coverage below remains separate.

## Original review result

The core is a useful instruction-engine foundation, but cannot be integrated
unchanged. It shares infrastructure with Z80 and has confirmed Intel-8080 flag
and interrupt defects. The dispatch table contains handlers for the documented
instruction set; undocumented encodings use the undefined hook/default NOP.
Presence of a handler is not proof of conformance. Memory/port callbacks,
register access, single stepping, cycle accounting and disassembly are present.

Run `make check-8080-audit`. It deliberately returns nonzero while findings remain;
it is separate from the passing x86 regression gate, with no hidden exemptions.
The test executes 1,059,096 checks and currently fails 426,499 input cases. This
large count comes from exhaustive byte operand combinations, not that many bugs.

## Original findings — all repaired above

| ID | Finding | Evidence and consequence |
| --- | --- | --- |
| 8080-01 | SUB/SBB/CMP and DCR auxiliary carry is inverted | All 393,216 tested subtraction/compare cases and 512 decrement cases fail their full flags check. Helpers calculate half-borrow; Intel 8080 exposes the opposite carry polarity. Results and other flags require preservation during repair. |
| 8080-02 | ANA/ANI use the wrong auxiliary-carry rule | Register ANA fails 32,768 of 131,072 cases. It always sets AC; Intel 8080 requires OR of operand bit 3. Memory and immediate handlers contain the same unconditional setting and need corresponding regression cases. |
| 8080-03 | Separate port-read setter writes the memory context | `e8080_set_port_read_fct` assigns `mem_rd_ext` instead of `port_rd_ext`. Reproducer receives the wrong input context; it can also corrupt the memory callback context. Combined port setter is not affected. |
| 8080-04 | Asserted INT while disabled is lost across EI | INT only sets `int_req` when IFF is already set. EI changes IFF without resampling the line. EI then NOP does not acknowledge an already asserted request. |
| 8080-05 | 8080 interrupt acknowledge is absent | Reset selects mode 0; interrupt entry only implements Z80 modes 1/2, otherwise prints a diagnostic. Direct enabled-INT probe neither transfers via a supplied instruction nor disables interrupts. There is no interrupt-acknowledge instruction provider API. |

## Passing focused checks

- ADD and ADC: all byte operand pairs and both incoming carry states, including
  result, S/Z/P/AC/CY and instruction length: 262,144 cases.
- XRA and ORA: the same matrix, 262,144 cases.
- ADD followed by DAA: all 10,000 pairs of decimal values 0..99, result and carry.
  Arbitrary DAA inputs and all DAA flags are not certified by this test.
- Immediate instruction fetch, word memory access and CALL/RET stack wrapping
  across FFFF; EI does not accept an interrupt immediately when previously disabled.

## Integration plan (first two steps completed for the five findings)

1. Repair the flag and callback findings with register/memory/immediate coverage.
   Preserve Z80 behavior: it has distinct arithmetic helpers but shares core APIs.
2. Define and test interrupt acknowledge, EI delay, DI, HLT wakeup and level
   handling. Test both 8080 and existing Z80 users before altering shared dispatch.
3. Add a NEC-specific adapter: register mapping, segment/address rules, MD state,
   BRKEM/CALLN/RETEM and native interrupt return. Do not substitute standalone
   Intel interrupt behavior for NEC's documented transition rules.
4. Add native-to-emulation-to-native guest acceptance and rerun the x86 gate.

Outstanding review depth: full data movement/control flow/conditional timing,
all rotate forms, I/O bus behavior, undocumented aliases, disassembler exhaustiveness
and an independently sourced diagnostic program. NEC-specific 8080 behavior
must be checked against its own manual before selecting flag policies.

## References

- [Intel 8080 Assembly Language Programming Manual, 1975](https://maizure.org/projects/decoded-altair-basic/Intel-8080%20Programmers%20Manual.pdf): SUB/SBB/SUI examples, printed pages 18–19 and 27–28, distinguish auxiliary carry from borrow.
- [Intel 8080/8085 Assembly Language Programming, 1977](https://st.sdf-eu.org/i8080/Intel%208080-8085%20Assembly%20Language%20Programming%201977%20Intel.pdf): printed page 1-12 specifies the 8080 AND rule; interrupt chapter specifies acknowledge and EI behavior.
- [NEC V20/V30 user's manual](https://www.dosdays.co.uk/media/nec/NEC-V20-V30-Users-Manual.pdf): native/8080 mode transitions must be implemented separately.

Evidence: `tests/memory-validation/8080-review/audit.log`. The standalone test
contains the independent arithmetic expectations; no production fixes are hidden
inside the review.
