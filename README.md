# PCE-286

This project is a enhancement to Hampa's PCE excellent emulator adding protected 286 support to run OS/2 1.X and NEC V20 8080 instructions to run a CP/M-816 configuration.

The original repository is at http://www.hampa.ch/pce/

## Enhancements to Hampa's PCE

- Full 286 support including protected mode
- Support for NEC V20 8080 instruction set

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

have fun
Hampa Hug <hampa@hampa.ch>
