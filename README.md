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

have fun
Hampa Hug <hampa@hampa.ch>
