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

After configuring the project, run tests from the repository root:

```sh
make check-unit            # Build and run all C unit tests
make check-cpu-regression  # Run the CPU regression suite (requires Python 3)
make check-build           # Compile test binaries without running them
make check-exit            # Test emulator exit behavior (requires Python 3)
```

To build and run a single test:

```sh
make tests/test_286_prot
./tests/test_286_prot
```

`make check` runs the C unit tests and emulator end-to-end tests. The end-to-end
tests require NASM, the built IBM PC emulator, and test floppy images. To prepare
the images, install `mtools` (`mcopy` and `mdir`) and provide `images/dos.img`, then
run:

```sh
make
make check-images
make check
```

Install NASM before running `./configure` so the build detects it. End-to-end
tests are skipped if NASM, the emulator, or the required images are missing;
check the output for `SKIP` messages.

See [CPU regression and conformance coverage](tests/cpu-compliance.md) for
coverage details and regression logs.

have fun
Hampa Hug <hampa@hampa.ch>
