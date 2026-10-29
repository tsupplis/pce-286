# PCE-286

This project is a enhancement to Hampa's PCE excellent emulator adding protected 286 support to run OS/2 1.X and NEC V20 8080 instructions to run a CP/M-816 configuration.

The original repository is at http://www.hampa.ch/pce/

## Enhancements to Hampa's PCE

- Full 286 support including protected mode
- Support for NEC V20 8080 instruction set
- 8087/80287 support (Early days, timing accuracy will come progressively)

## PCE - PC Emulator

PCE is a collection of microcomputer emulators.

For further information check out these files:

- doc/pce-ibmpc.txt PCE/ibmpc, an IBM PC 5150 emulator

- doc/pce-macplus.txt PCE/macplus, a Macintosh Plus emulator

- doc/keys.txt Key combinations to control the emulator

- doc/monitor.txt The monitor interface

- doc/messages.txt Messages to control the emulator

## Install

To install do:

```
./autogen.sh
./configure
make
make install
```

The most important options for configure are:

```
--prefix=path
Set the installation prefix. The default is /usr/local.

--with-x11[=path]
Build the X11 based terminal.

--with-sdl
Build the SDL based terminal.
```

## Testing

Run tests from the repository root after configuring the project:

```sh
make check-help            # List suites and overrides
make check-npx-unit        # Experimental NPX state/arithmetic/conversions/guest-interface tests
make check-npx-convert     # Focused 8087/287 comparisons and format conversions (no ROMs)
make check-build           # Build every C test without running it
make check-unit            # Run every C test; no external ROMs or disk images
make check-cpu-regression  # Cross-CPU regression gate (Python 3)
make check-at-build        # Build AT component tests, emulator and ASM payloads
make check-at-unit         # AT keyboard, CMOS, I/O, port 92 and memory components
```

Individual C tests remain directly buildable and runnable, for example:

```sh
make tests/test_286_prot
./tests/test_286_prot
```

`make check` runs C tests, fixed CPU guest images, emulator exit checks and
native IBM/AMI AT boot and acceptance. Guest tests use null display/audio,
have timeouts and retain logs. CPU guest logs go to
`tests/memory-validation/e2e/`; native AT evidence goes to
`tests/memory-validation/make-at/`, with one directory per test.

Prepare external prerequisites before running the guest tests. Install Python 3,
NASM and mtools (`mcopy`, `mdir`, `mformat`, `mtype`). Install NASM before `./configure`
so the build detects it. Put these ROMs directly under the public repository-root
`testroms/` directory:

| File | Size | Used by |
|---|---:|---|
| `testroms/ibm-basic-1.10.rom` | 32,768 bytes | Fixed CPU guest tests |
| `testroms/glabios-5160-0.4.2.rom` | 8,192 bytes | Fixed CPU guest tests |
| `testroms/ibm-vga.rom` | 24,576 bytes | CPU and native AT guest tests |
| `testroms/ibm-at-1985-11-15-02.rom` | 65,536 bytes | Native IBM AT tests |
| `testroms/ami-at-158c.rom` | 65,536 bytes | Native AMI AT and HDD tests |

ROMs are supplied locally and excluded from Git. Tests do not use `private/` or
require the `ibmat-os2` or `ibmxt-dos` sample installations.

Provide `images/dos.img`, a bootable IBM/PC DOS floppy containing `IBMBIO.COM`,
`IBMDOS.COM` and `COMMAND.COM`. Native floppy tests extract those exact filenames;
a blank image or an MS-DOS image with differently named system files will not do.
Test builders work on copies, add payloads and guest startup files, and leave the
source image unchanged. Build the fixed CPU guest images once:

```sh
make
make check-images
make check
```

The fixed CPU tests use `tests/test.cfg` and the generated
`src/arch/ibmpc/bios/ibmpc-pcex.rom`. PCEX provides host services for those tests;
they do not establish native AT controller compatibility. The separate native AT
suites exercise the BIOS and real emulated floppy/controller paths.

| Target | Scope |
|---|---|
| `check-exit` | Emulator CLI exit and cleanup, no external ROMs |
| `check-e2e` | Fixed 186/286/NEC CPU guest images |
| `check-at` | AT C components plus both BIOS smoke suites |
| `check-at-boot` | IBM and AMI native POST/DOS boot |
| `check-at-acceptance` | Keyboard, floppy writes/readback, Ctrl-Alt-Del and persistence |
| `check-at-full-ram` | Boot and acceptance with 15,232 KB extended RAM; longer run |
| `check-at-floppy-soak` | Repeated writes/readback and reboot on both BIOSes; longer run |
| `check-at-disk` | Native WD1003 hard-disk acceptance using an explicitly supplied image |
| `check-at-pm-soak` | Prepare native HDD fixture, then repeated CPL0 protected-mode entries |
| `check-all` | Normal checks plus full-RAM/floppy soaks; adds HDD/PM when `AT_DISK_IMAGE` is set |

Boot and acceptance targets also have `-ibm` and `-ami` variants. Full-RAM variants
are `check-at-boot-full-ibm`, `check-at-boot-full-ami`,
`check-at-acceptance-full-ibm` and `check-at-acceptance-full-ami`. Floppy variants
are `check-at-floppy-ibm` and `check-at-floppy-ami`.

HDD and PM tests require a standalone bootable DOS hard-disk image with an active
FAT16 partition and total image size at most 65,454,080 bytes. Its startup must
work with only that drive attached. Supply it explicitly; there is no sample
image default. The test copies it and adjusts geometry only in the copy for
AMI type 4 (940 cylinders, 8 heads, 17 sectors). PM soak tests 10,000 CPL0 rounds
per boot, including memory and INT/IRET; it does not cover privilege changes,
task switching or PM exit.

```sh
make check-at-disk AT_DISK_IMAGE=/absolute/path/to/dos-hd.img
make check-at-pm-soak AT_DISK_IMAGE=/absolute/path/to/dos-hd.img
make check-at-floppy-ami AT_SOAK_CYCLES=1 AT_SOAK_ROUNDS=8
```

Overrides: `TEST_EMULATOR` selects the executable for guest/exit tests;
`AT_TEST_DIR` and `E2E_TEST_DIR` select evidence directories. `AT_TIMEOUT` overrides
the timeout in seconds per native guest boot, `E2E_TIMEOUT` defaults to 30 seconds
per CPU guest, `AT_SOAK_CYCLES=3`, `AT_SOAK_ROUNDS=64` and `AT_PM_RUNS=3` control
soaks. Native defaults allow longer BIOS POST runs, especially with full RAM.

Missing native prerequisites fail immediately with a diagnostic. Missing fixed
CPU images (or NASM not detected at configure time) produce explicit `SKIP`
messages; use `check-images` to prepare them. C tests and the CPU regression gate
need no external ROMs or DOS media. OS/2 and daily XT sample acceptance remain
separate workflows and are not prerequisites of any target above.

### Optional 8087 / 80287 coprocessor

Coprocessors are disabled by default. Add one section to the machine config:

```ini
# With system.model = "5150" or "5160", cpu.model = "8086" or "8088":
npx { model = "8087" }

# Alternatively, with system.model = "5170", cpu.model = "80286":
# npx { model = "80287" }
```

Omitting the section, or using `model = "none"`, keeps the previous no-NPX
behaviour. Other pairings (including NEC and 80186 CPUs) are rejected at startup;
incompatible runtime CPU-model changes are rejected too. No NPX speed setting
or PCE DOS driver is required. This remains **experimental functional emulation**:
physical execution timing, some numeric edge cases and original-chip
transcendental results are not complete. Unsupported numeric cases retain the
CPU's explicit NPX development-stop behaviour. The machine log reports the
stopped CS:IP, opcode bytes, NPX model, control/status words and tags once per
stop. This diagnostic identifies an unsupported instruction or operand case;
it is distinct from a guest numeric exception or a normal WAIT.

The 80287 defaults to `timing = 1`: a partial typical-cycle profile for
register arithmetic, square root and transcendental instructions. Set
`npx { model = "80287" timing = 0 }` for the previous functional timing.
One modeled NPX clock equals one emulated CPU clock; this is not a historical
AT clock-ratio model. Integer execution can overlap NPX occupancy; WAIT and
checking numeric instructions wait for it. Results and errors are still
committed synchronously, and no-wait controls can observe them immediately.
Memory-form execution costs, transfer cycles, operand-dependent timing and
exception penalties are not yet modeled. The 8087 retains functional timing.
This profile improves pacing but does not make benchmark MHz hardware-accurate.

XT presence is reported through the equipment switches; errors use NMI with
port A0h bit 7 enabling delivery. AT presence sets the initial CMOS equipment
bit, and errors use IRQ13 instead of CPU exception 16. Port F0h acknowledges the
board error/BUSY latch without clearing NPX exception flags; F1h resets the NPX,
including its protected-mode state. CPU-only reset preserves the NPX. Full
machine reset resets it. Saved CMOS files and guest CMOS writes retain their
existing precedence: update the saved setup when adding a coprocessor.

`make check-npx-board` tests actual machine wiring without external ROMs/media,
including protected-mode IRQ13 delivery. It is also part of `check-npx-unit`
and the cross-CPU regression gate. `make check-npx-guest` tests BIOS equipment
reporting, arithmetic, divide-by-zero handling and recovery in DOS on XT
(GLABIOS + PCEX) and native IBM/AMI AT. It needs NASM, mtools, `images/dos.img`,
the ROMs listed above and the built `ibmpc-pcex.rom`; it creates disposable
images under `tests/memory-validation/npx-board-guest`. IBM uses its established
2x diagnostic speed for early POST; AMI uses 24x. These are bounded smoke tests,
not full application or OS/2 coprocessor acceptance.

The IBM PC binary includes the SoftFloat subset. `make install` also installs
its BSD notice under `share/pce/licenses/softfloat/LICENSE.md`; retain that notice
when distributing binaries separately.

`make check-npx-trans` checks the development NPX constant loads and the
F2XM1, FYL2X, FYL2XP1, FPATAN and FPTAN approximation paths. It is included in
`check-npx-unit`. Reference vectors are mathematical values, not chip readbacks;
passing them does not establish bit-identical 8087/287 transcendental behavior.
FPTAN returns a functional numerator/denominator pair; its individual chip
values/scaling and masked stack-error results remain unverified. Transcendental
range results remain unsupported except masked/unmasked F2XM1 and FPATAN
underflow. These use functional approximations; exact chip-result bits remain
unverified. Regenerating
`tests/npx_trans_vectors.h` is optional and requires Python with mpmath 1.3.0:
run `python3 tests/generate_npx_trans_vectors.py` from the source root. The
emulator and ordinary tests have no Python or host-floating-point dependency.
`make npx-trans-probe` builds an explicit real-chip capture utility; see
[NPX characterization instructions](tests/npx-overflow-probe.md). It is never
run by the ordinary check targets.

have fun
Hampa Hug <hampa@hampa.ch>
