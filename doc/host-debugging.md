# Host debugging and headless DOS

Keep the guest console and emulator monitor on separate host terminals.

| Channel | Endpoint | Use |
|---|---|---|
| Guest DOS console | COM1 → host PTY or TCP | Shell input/output through DOS `CTTY` |
| PCE monitor | Emulator stdin/stdout | Pause, inspect registers and memory, resume |
| Host profiler | Emulator PID | Measure host CPU while the guest runs |

## Headless console

Replace the COM1 section in the machine config. Use a free serial port and select a backend supported by your build.

```ini
serial { address = 0x3f8 irq = 4 uart = "8250" driver = "pty:symlink=com1" }
speaker { driver = "null" }
```

1. From the machine directory, run `pce-ibmpc -c ibmxt.cfg -t null`. Enter `g` at the monitor.
2. In another host terminal, run `screen ./com1 9600`. PCE creates the PTY link in its working directory.
3. For unattended boot, put the following in a test DOS AUTOEXEC.BAT. For initial setup, run the same commands at a visible DOS prompt.

```bat
MODE COM1:9600,N,8,1
CTTY COM1
```

Use `CTTY CON` to restore the guest keyboard/display. Commands using DOS console handles follow CTTY. BIOS keyboard input and direct VGA writes bypass it. The null terminal suppresses display output while VGA emulation continues.

For a local TCP socket, replace the serial driver with `"tcp:host=127.0.0.1:port=5555:telnet=0:usectl=0"` and connect with `nc 127.0.0.1 5555`. The PTY provides a bidirectional terminal endpoint. A log file only captures output.

## Guest CPU inspection

Send SIGINT to the emulator PID while it is running under the monitor. Issue commands on its original stdin.

| Command | Effect |
|---|---|
| `s cpu` | Show guest registers, flags and CS:IP |
| `u` | Disassemble at the current instruction |
| `d b800:0 fa0` | Dump an 80×25 color text page, including attributes |
| `t 1` | Step one guest instruction |
| `g` | Resume execution |
| `q` | Exit from the monitor |

Verify the PID before sending `kill -INT PID`. For scripted sessions, retain the child PID, send SIGINT, wait for the monitor prompt, then issue commands. Finish with `q` and wait for the process to exit.

## DOS prompt idle CPU

For single-task real-mode DOS, copy the built `src/arch/ibmpc/pceutils/pceidle.com` to the guest and run `PCEIDLE` once from AUTOEXEC.BAT. Reboot without that line to disable it. The TSR chains INT 28h and executes HLT when the caller allowed interrupts. Timer or keyboard interrupts wake the CPU. Programs that poll without calling the DOS idle hook receive no benefit from the TSR.

MS-DOS 6.22, XT/8088, speed 24, null display/audio, 10-second samples (% of one host core):

| Build | Plain DOS prompt | With PCEIDLE |
|---|---:|---:|
| Before halted-CPU optimization | 29.2% | 21.2% |
| With halted-CPU optimization | 28.6% | 6.1% |

BIOS ticks advanced and an injected VER command returned the DOS version in each run. A separate headless OS/2 1.3 run measured 31.4% over 15 seconds. SDL and serial-console measurements for this change remain open.

[Monitor commands](monitor.md) · [Character drivers](char-drivers.md)
