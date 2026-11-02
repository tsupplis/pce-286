# Hardware and programming references

## Processor architecture

| Processor | What to look up | Manufacturer reference |
|---|---|---|
| Intel 8086 / 8088 | Instruction semantics, flags, interrupts, bus width and prefetch | [The 8086 Family User’s Manual, October 1979](https://bitsavers.org/components/intel/8086/9800722-03_The_8086_Family_Users_Manual_Oct79.pdf) |
| Intel 80186 / 80188 | Additions to the 8086 instruction set and exception behavior | [iAPX 86, 88, 186 and 188 User’s Manual, Programmer’s Reference, 1983](https://ftpmirror.your.org/pub/misc/bitsavers/components/intel/80186/210911-001_iAPX86_88_186_188_Programmers_Reference_1983.pdf) |
| Intel 80286 | Descriptor validation, gates, task switches, exceptions and restart state | [80286 and 80287 Programmer’s Reference Manual, 1987](https://bitsavers.trailing-edge.com/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf) |
| Intel 8080 | Opcode and flag baseline for the shared core | [8080 Programmer’s Manual](https://maizure.org/projects/decoded-altair-basic/Intel-8080%20Programmers%20Manual.pdf) |
| NEC V20 / V30 | Native extensions, 8080 register mapping, BRKEM, CALLN and RETEM | [NEC V20/V30 User’s Manual](https://www.dosdays.co.uk/media/nec/NEC-V20-V30-Users-Manual.pdf), [NEC V-Series Data Book](https://bitsavers.trailing-edge.com/components/nec/_dataBooks/1990_NEC_16-bit_V-Series_Microprocessor_Data_Book.pdf) |

## NEC µPD9002 and the PC-88VA family

| Evidence | Scope |
|---|---|
| [NEC PC-88VA2 product specification](https://support.nec-lavie.jp/support/product/data/spec/cpu/b048-1.html) | Machine identification and V30-compatible and Z80-compatible operation. V3 denotes a PC-88VA machine mode. |
| [SUZUKI PLAN Z80 implementation and tests](https://github.com/suzukiplan/z80) | PC-88VA2 observations useful for Z80 flag comparisons. |
| [PC-88VA Double Dragon source](https://github.com/nakatamaho/pc88va-dd) | Example of software written for the machine. |

**Open question for emulation.** The machine specification establishes the two instruction sets. A reproducible transition trace is still needed to determine the entry mechanism, register state, memory mapping and interrupt routing. The V20 manual describes a different transition, between native code and 8080 emulation.

## IBM PC/AT system board and adapters

IBM’s [5170 Technical Reference, March 1986](https://www.minuszerodegrees.net/manuals/IBM/IBM_5170_Technical_Reference_MAR86.pdf) gives the system I/O map, schematics and BIOS listings. Use the board revision matching the ROM under test. Earlier PC and XT manuals are available in the [IBM technical reference collection](https://www.pcjs.org/documents/manuals/ibm/).

| Device | Relevant AT behavior |
|---|---|
| 8259A interrupt controllers | Master/slave cascade, IRQ routing and acknowledgment |
| 8237A DMA controllers | Cascade channel, byte/word transfers and page registers |
| 8254 interval timer | Timer IRQ, refresh and speaker channels |
| 8042 keyboard controller | Keyboard commands, A20 gate and reset output |
| MC146818 RTC/CMOS | Clock and alarm interrupts, setup bytes and NMI mask at the board interface |
| µPD765/8272 family FDC and WD1003 fixed-disk controller | Adapter registers, floppy DMA/IRQ6, fixed-disk PIO/IRQ14 |

## Numeric coprocessors

| Device | Scope | Source |
|---|---|---|
| Intel 8087 | Stack, exception masks, infinity modes, CPU synchronization | [8086 Family User’s Manual, Numerics Supplement](https://ethw.org/w/images/2/2f/Intel_8086_family_users_numeric_supp.pdf) |
| Intel 80287 | Real/protected-mode interface, exception delivery and saved environment | [80286 and 80287 Programmer’s Reference Manual](https://bitsavers.trailing-edge.com/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf) |
| IIT 3C87 | Integrated Information Technology clone-specific behavior and identification. Vendor abbreviation is IIT. | [IIT 3C87 data sheet](https://www.ardent-tool.com/CPU/docs/IIT/3c87dx.pdf) |

Original-chip timing and ambiguous numeric results still require captures from identified silicon. The [hardware probe notes](npx-overflow-probe.md) describe the capture procedure.

## Programming and validation sources

| Source | Use in this work |
|---|---|
| [Hampa Hug’s PCE](http://www.hampa.ch/pce/) | Upstream emulator, device models and guest utilities |
| [Berkeley SoftFloat](https://www.jhauser.us/arithmetic/SoftFloat.html) | Software floating-point primitives used by the coprocessor implementation |
| [mpmath](https://mpmath.org/) | High-precision mathematical references for numeric vectors |
| [frankischilling/i8087](https://github.com/frankischilling/i8087/blob/main/src/i8087.c) | Independent implementation for comparison |
| [Microsoft DOS 1.25 source](https://github.com/microsoft/MS-DOS/tree/main/v1.25/source) | Early DOS services when reviewing guest utility compatibility |
| [Digital Research Concurrent DOS Installation Guide](https://www.bitsavers.org/pdf/digitalResearch/concurrent/1095-1001-001_Concurrent_DOS_Installation_Guide_1987.pdf) | Expanded-memory configuration and banking |
| VCPM (vcpm.a86), CheckIt 3.0, 87TEST | Guest workloads for NEC detection and mode transitions, system diagnostics and numeric probes |

Project coverage and known limits are recorded in the [CPU compliance matrix](cpu-compliance.md), [8080 review](8080-review.md), [NEC mode notes](nec-8080-mode.md) and [numeric coprocessor notes](numeric-coprocessors.md).
