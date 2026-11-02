# IBM XT quick configuration

Save as `ibmxt.cfg` alongside a bootable DOS hard-disk image. Put the named ROMs in `rom/` or adjust the search path. Start from this directory with `pce-ibmpc -c ibmxt.cfg -r`.

```ini
path = "."
path = "rom"
system {
    model = "5160"
    boot = 128
    patch_bios_init = 1
    patch_bios_int19 = 1
}
cpu { model = "8088" speed = 24 }
ram { address = 0 size = 640K }

rom { address = 0xf0000 size = 64K default = 0xff }
load { format = "binary" address = 0xf0000 file = "ibm-xt-1986-05-09.rom" }
rom { address = 0xc8000 size = 8K default = 0xff }
load { format = "binary" address = 0xc8000 file = "ibmpc-pcex.rom" }

terminal { driver = "sdl" }
video {
    device = "vga"
    rom { address = 0xc0000 size = 24K file = "ibm-vga.rom" }
}
disk { drive = 128 type = "auto" file = "hd0-ibmxt.img" }
```

| Option | Config delta | Guest setup |
|---|---|---|
| Software EMS | Add `ems { size = 16M segment = 0xe000 }` | DOS: load `PCEEMM.SYS` |
| CDOS XM banked memory | Change base RAM to 256K, add `eems {}` | Enable **Internal EEMS** in SETUP and save. Use the internal driver alone. |
| DOS with EEMS board | Change base RAM to 256K, add `eems { dos_frame = 1 }` | Load `PCEEEMM.SYS`. Reserve E0000h–EFFFFh for the frame. |
| Numeric coprocessor | Add `npx { model = "8087" }` with 8088/8086 | BIOS reports coprocessor presence |
| NEC V20 | Change CPU model to `"v20"`, omit NPX | Enables NEC native and 8080 emulation modes |

Choose one EMS row. The EEMS board supplies 384 KiB of conventional RAM above the motherboard’s 256 KiB. PCEX in this profile supplies assisted boot and disk access. The speed setting is a multiplier.

[Full template](../src/arch/ibmpc/pce-ibmpc.cfg.in) · [Headless operation](host-debugging.md) · [Disk creation](disk-creation.md) · [Memory](ibmpc-memory.md)

### Experimental µPD9002 CPU

Set `model = "upd9002"` in the XT configuration’s `cpu` block to run native NEC code and Z80-mode programs on the PC platform. To check a DOS 2+ guest, build `make tests/asm/tupd9002.com`, copy the resulting file to the guest, and run `TUPD9002`. It prints `UPD9002 PASS` and returns exit code 0 after testing separate code and data segments, indexed Z80 memory access and a native interrupt callback.

For longer Z80 regressions, build `make tests/asm/zexrun.com` and copy
`ZEXRUN.COM` and the two CP/M exercisers to DOS. Run `ZEXRUN ZEXDOC.COM`
(documented flags) and `ZEXRUN ZEXALL.COM` (includes undocumented flags).
The launcher implements the console calls used by these diagnostics. Inspect
their CRC reports for failures. A normal return to DOS means the guest finished.

For a headless run, use `make check-upd9002-zex ZEX_DIR=/path/to/ZEXALL`.
Requires the usual XT test ROMs, `images/dos.img`, NASM and mtools. Each
suite boots from a disposable floppy. Reports go in
`tests/memory-validation/zex/`. Diagnostic binaries and source:
[Frank Cringle’s Z80 instruction exercisers](https://github.com/agn453/ZEXALL/tree/8f71d418bae69a476a5a0e5c6e122c8801b8d9f4).
