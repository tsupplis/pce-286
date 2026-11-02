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

[Full template](src/arch/ibmpc/pce-ibmpc.cfg.in) · [Headless operation](host-debugging.md)
