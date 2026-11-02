# PCE/atarist - Atari ST Emulator

PCE/atarist is an Atari ST emulator. It emulates the various ST and Mega ST
models on Unix-like and Windows host operating systems.

Emulated parts:

```text
Part		Status
------------------------------------------------------------------------------
```

```text
CPU		A complete MC68000 emulator.
```

```text
ROM		An unmodified ROM image from an Atari ST is needed. Supported
		TOS versions are 1.00, 1.02, 1.04 and 2.06.
```

```text
RAM		Up to 14 MiB of memory are supported.
```

```text
Video		All 3 standard Atari ST video modes are supported. Special
		border effects, used by some demos and games, are not yet
		supported.
```

```text
Sound		Not yet supported
```

```text
FDC		Supported
```

```text
ACSI		Up to 7 harddisks are supported.
```

```text
Serial port	Supported
```

```text
Parallel port	Supported
```

```text
Mouse		Supported
```

```text
Keyboard	Supported
```

```text
MIDI		Output can be written to a standard MIDI file.
```

## Configuration

PCE/atarist is configured by means of a config file. A documented example
config file is installed in $(prefix)/etc/pce-atarist.cfg.

## Running PCE/atarist

After starting the emulator, pce/atarist is in command mode (unless the -r
option was used). Type 'h' to get a short summary of the available
commands.

The 'g' command starts the simulation. It can be interrupted by
pressing Ctrl-`.
