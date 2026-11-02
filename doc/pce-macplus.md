# PCE/macplus - Macintosh Plus Emulator

PCE/macplus is a Macintosh Plus emulator. It emulates a Macintosh 128K,
Macintosh 512k, Macintosh 512ke or a Macintosh Plus on Unix-like and Windows
host operating systems.

Emulated parts:

```text
Part		Status
```

```text
CPU		A complete MC68000 emulator.
```

```text
ROM		An unmodified ROM image from a Macintosh Plus is needed.
```

```text
RAM		Memory configurations of 128K, 512K, 1M, 2.5M and 4M are
		supported.
```

```text
Video		Supported (512*342*2)
```

```text
Sound		Not yet supported
```

```text
Floppy disks	Supported by a custom driver. For this to work the ROM is
		patched at runtime.
```

```text
SCSI		Up to 7 harddisks are supported
```

```text
Serial ports	Supported
```

```text
Mouse		Supported
```

```text
Keyboard	Supported
```

## Configuration

PCE/macplus is configured by means of a config file. A documented example
config file is installed in $(prefix)/etc/pce-macplus.cfg.

## Running PCE/macplus

After starting the emulator, pce/macplus is in command mode (unless the -r
option was used). Type 'h' to get a short summary of the available
commands.

The 'g' command starts the simulation. It can be interrupted by
pressing Ctrl-`.
