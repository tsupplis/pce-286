# IBM AT quick configuration

Native 5170 profile for DOS or OS/2 1.3. Save as `ibmat.cfg` in the machine directory and run `pce-ibmpc -c ibmat.cfg -r`. Supply the ROMs in `rom/`. This example expects an existing bootable type-4 image with 940 cylinders, 8 heads and 17 sectors (65,454,080 bytes).

```ini
path = "."
path = "rom"
system {
    model = "5170"
    boot = 128
    memtest = 0
    floppy_disk_drives = 1
    patch_bios_init = 0
    patch_bios_int19 = 0
}
cpu { model = "80286" speed = 24 }
ram { address = 0 size = 640K }
ram { address = 0x100000 size = 4M }
rom { address = 0xf0000 size = 64K default = 0xff }
load { format = "binary" address = 0xf0000 file = "ami-at-158c.rom" }

terminal { driver = "sdl" }
video {
    device = "vga"
    rom { address = 0xc0000 size = 24K file = "ibm-vga.rom" }
}
fdc { address = 0x3f0 irq = 6 drive0 = 0 }
disk { drive = 0 type = "auto" file = "fd0.img" optional = 1 }
cmos { floppy0_type = 4 hard_disk0_type = 4 }
wd1003 { drive0 = 128 }
disk { drive = 128 type = "auto" file = "hd0-ibmat.img" c = 940 h = 8 s = 17 }
```

| Goal | Config delta |
|---|---|
| Install from 1.44 MB floppy | Supply bootable `fd0.img`, set `boot = 0` and select floppy first in the guest BIOS where applicable. |
| Full POST memory test | Set `memtest = 1`. Zero requests a simulated warm start. |
| Existing PCEX-managed DOS disks | Keep `patch_bios_init = 0`, set `patch_bios_int19 = 1`, map/load PCEX at C8000h as in the [XT guide](ibmxt-config.md), use `wd1003 {}` and `hard_disk0_type = 0`. Preserve each image’s existing geometry. |
| 80287 | Add `npx { model = "80287" }`. |
| CDOS XM EEMS | Change base RAM to 256K and add `eems {}`. Enable the internal CDOS driver. |
| MS-DOS EEMS | Change base RAM to 256K and add `eems { dos_frame = 1 }`. Load `PCEEEMM.SYS`. |

At startup, check the log for model 5170, CPU 80286, the expected CMOS memory size and successful BIOS/VGA ROM loads. For native disk access, match the CMOS drive type to the disk geometry.

[Full template](src/arch/ibmpc/pce-ibmpc.cfg.in) · [Memory configuration](doc/pce-ibmpc-memory.md) · [Host debugging](host-debugging.md)
