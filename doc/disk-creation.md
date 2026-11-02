# Disk images for IBM XT and AT

## Create blank media

Run in the machine directory, using new filenames. Geometry is cylinders, heads, sectors per track (CHS). Each sector is 512 bytes.

| Media | Command | Expected bytes |
|---|---|---:|
| XT hard disk, IBM HDC type 0 | `pce-img create -O raw -g 306 4 17 hd-xt.img` | 10,653,696 |
| AT hard disk, BIOS type 4 | `pce-img create -O raw -g 940 8 17 hd-at.img` | 65,454,080 |
| XT 360 KiB floppy | `pce-img create -O raw -g 40 2 9 fd360.img` | 368,640 |
| AT 1.44 MiB floppy | `pce-img create -O raw -g 80 2 18 fd1440.img` | 1,474,560 |

Check with `wc -c hd-xt.img` or `pce-img info hd-xt.img`. Raw images store sector data only. Keep the chosen CHS in the config; the geometry guessed by `info` may differ. The locally installed utility returned status 1 even when it created a file of the expected length. Verify the file before using it.

## IMG vs PIMG

| | Raw IMG | PCE PIMG |
|---|---|---|
| Layout | Sector bytes from byte 0 | Header with geometry and data offset, followed by sector data |
| Geometry | Supply CHS for ambiguous sizes | Stored in the image |
| Creation / config | `-O raw` / `type = "image"` | `-O pimg` / `type = "pce"` |
| Typical use | Exchange with DOS tools and other emulators | Preserve disk geometry within PCE |

`type = "auto"` works for either example. Both represent ordinary block media. For per-sector floppy encoding or irregular track layouts use a suitable sector image such as PSI.

```sh
pce-img create -O pimg -g 80 2 18 floppy.pimg
pce-img info floppy.pimg
pce-img convert -O raw floppy.pimg floppy.img
```

Both examples expose 2,880 sectors. The raw output is 1,474,560 bytes; the default PIMG created above is 512 bytes larger on disk.

## Attach the image

Add **one** matching hard-disk block to the machine config. Drive 128 is BIOS disk 80h; drive 129 is 81h. Preserve the original CHS when attaching an existing partitioned image.

```ini
# XT example
disk { drive = 128 type = "auto" file = "hd-xt.img" c = 306 h = 4 s = 17 }
# AT example (use instead of the XT entry)
#disk { drive = 128 type = "auto" file = "hd-at.img" c = 940 h = 8 s = 17 }
```

Choose the access path below. Edit fields inside the existing system/CMOS sections.

| Machine and path | System settings for hard-disk boot | ROM and controller |
|---|---|---|
| XT, PCEX-assisted | `boot = 128`, `patch_bios_init = 1`, `patch_bios_int19 = 1` | Load PCEX at C8000h. PCEX serves the disk through BIOS calls. |
| AT, PCEX-assisted DOS | `boot = 128`, `patch_bios_init = 0`, `patch_bios_int19 = 1` | Load PCEX at C8000h. Use `wd1003 {}` and CMOS hard-disk type 0. |
| XT, native HDC | Both BIOS patches 0 | Load the XT controller ROM at C8000h. Bind drive 128 to the HDC and match its switches to the image. |
| AT, native WD1003 | Both BIOS patches 0 | AT system BIOS handles disk I/O. Bind drive 128 and match CMOS type to CHS. Use this path for OS/2. |

**PCEX-assisted XT or AT** — add the PCE option ROM alongside the machine BIOS:

```ini
rom  { address = 0xc8000 size = 8K default = 0xff }
load { format = "binary" address = 0xc8000 file = "ibmpc-pcex.rom" }
```

**Native XT** — use this in place of the PCEX block. These switch settings are specific to the named IBM controller BIOS and the 306/4/17 example:

```ini
rom  { address = 0xc8000 size = 32K default = 0xff }
load { format = "binary" address = 0xc8000 file = "ibm-hdc-1985-10-28.rom" }
hdc  { address = 0x320 irq = 5 drive0 = 128 sectors = 17 switches = 0 }
```

**Native AT** — for the 940/8/17 example:

```ini
wd1003 { drive0 = 128 }
cmos   { hard_disk0_type = 4 }
```

Saved CMOS takes precedence. Check the final CMOS settings and disk CHS in the startup log.

## Floppy access: flexible to enforced

The `disk` block selects media, `fdc.drive0` binds drive A: to the controller, and AT CMOS advertises the installed drive type to BIOS. Apply these settings inside the existing sections.

| Profile | Settings | Effect |
|---|---|---|
| PCEX-assisted BIOS access | Load PCEX; omit the `drive0` binding for drive A: | PCEX handles guest INT 13h floppy requests. Use a bound FDC for guests that program the controller directly. |
| Native FDC, older BIOS workaround | `drive0 = 0`, `ignore_eot = 1`, `accurate = 0` | Ignores the BIOS end-of-track value for higher sector counts. Multi-track transfers are unavailable in this mode. |
| Native FDC, normal operation | `drive0 = 0`, `ignore_eot = 0`, `accurate = 0` | Honors end-of-track. On AT, known MFM HD media require 500 kbit/s; known DD media accept 250 or 300 kbit/s by default. |
| AT, enforce nominal DD drive speed | Add `drive0_rpm = 300` or `360` | DD requires 250 or 300 kbit/s respectively. XT ignores this setting. Unknown encodings remain permissive. |

```ini
# AT A: 3.5-inch 1.44 MiB drive, currently holding a 720 KiB DD disk
cmos { floppy0_type = 4 }
fdc  { address = 0x3f0 irq = 6 drive0 = 0 drive0_rpm = 300 ignore_eot = 0 }
disk { drive = 0 type = "image" file = "fd720.img" c = 80 h = 2 s = 9 }
```

AT CMOS drive types: 1 = 360 KiB, 2 = 1.2 MiB, 3 = 720 KiB, 4 = 1.44 MiB. Choose a type supported by the guest BIOS. For B:, use `drive1 = 1`, `floppy1_type` and `drive1_rpm`. The AT adapter reads PSI sector encoding or infers density from recognized block geometry.

Timing and sides are independent controls. `accurate = 1` enables controller timing; the template recommends `cpu.speed = 1` to avoid guest timeouts. The RPM setting selects a rate check only. `single_sided = 1` restricts A: to one side (bit 1 restricts B:).

## First boot

Attach a bootable installer floppy as drive 0. Use a floppy format supported by the guest BIOS; the AMI AT example uses 1.44 MiB (`floppy0_type = 4`). A blank floppy from the table is suitable as a data disk after guest formatting.

```ini
disk { drive = 0 type = "auto" file = "install.img" }
fdc  { address = 0x3f0 irq = 6 drive0 = 0 }
```

Boot from A: (select `boot = 0` for PCEX-assisted boot). For DOS, use FDISK to create an active primary partition, reboot from A:, then format the **new** C: volume and install DOS. For OS/2, use the installer’s partitioning and formatting steps. Eject the installer and verify a hard-disk boot.

[XT quick config](ibmxt-config.md) · [AT quick config](ibmat-config.md) · [Controller and drive template](../src/arch/ibmpc/pce-ibmpc.cfg.in)
