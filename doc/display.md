# IBM PC display configuration

These snippets replace the `video` section in an existing working XT or AT
configuration; they are not complete machine configurations. They describe the
options implemented in PCE, not a claim that every BIOS mode has been tested.

## Selecting an adapter

The first `video` section is selected by default. The command-line option
`-g ega`, `-g cga`, `-g vga`, `-g hgc`, or `-g mda` selects the matching section.
For example:

```sh
pce-ibmpc -c ibmat.cfg -g ega -r
```

Check launch scripts for a hard-coded `-g vga`. Keep only one EGA section in a
configuration: `-g ega` cannot distinguish color and monochrome EGA sections.
Use separate configuration files for those alternatives. Multiple sections are
alternative adapters, not simultaneous dual-monitor emulation.

ROM filenames below are examples. Adjust them to your actual ROM collection and
configured search paths. EGA and VGA require their respective video BIOS ROMs;
CGA, Hercules and MDA use built-in fonts and need no separate video ROM here.
Keep the machine's main BIOS. Remove any unconditional load of the old video
ROM so it cannot overlap the selected adapter's ROM at C0000h.

## EGA with an EGA color monitor

```ini
video {
    device = "ega"
    switches = 0x09
    blink = 16
    enable_irq = 0

    rom {
        address = 0xc0000
        size = 16384
        default = 0xff
        file = "../rom/bios/ibm-ega-1984-09-13.rom"
    }
}
```

`0x09` selects the EGA color monitor in EGA mode. Other implemented switch
settings are `0x06` (CGA monitor, 40-column), `0x07` (CGA monitor,
80-column), and `0x08` (EGA monitor in CGA mode).

## EGA with a monochrome monitor — mode 0Fh

```ini
video {
    device = "ega"
    switches = 0x0a
    blink = 16
    enable_irq = 0

    rom {
        address = 0xc0000
        size = 16384
        default = 0xff
        file = "../rom/bios/ibm-ega-1984-09-13.rom"
    }
}
```

The source maps `0x0a` to an MDA-type monochrome monitor and implements grayscale
intensity output. The template also lists `0x0b` as a monochrome-monitor setting;
use `0x0a` for this example.

This remains an **EGA adapter**, not an MDA or Hercules adapter. The guest selects
640 × 350 monochrome graphics mode 0Fh through BIOS `INT 10h` with `AX=000Fh`.
The switch setting selects the monitor; it does not force graphics mode at boot.
Do not substitute `device = "mda"` or an MDA system-display setting to obtain it.
The `color` phosphor option used by MDA/Hercules is not an EGA configuration option.

**Validation status:** configuration parsing and the monochrome rendering path
have been checked in source. An end-to-end BIOS mode-0Fh drawing test remains to
be run; full mode-0Fh compatibility is not yet verified.

## CGA

```ini
video {
    device = "cga"
    font = 0
    blink = 16
}
```

`font = 0` selects the built-in thick text font; `font = 1` selects the thin font.
CGA composite rendering defaults to `auto`; see below for RGB-only output.

## VGA

```ini
video {
    device = "vga"
    blink = 16
    enable_irq = 0

    rom {
        address = 0xc0000
        size = 24576
        default = 0xff
        file = "../rom/vga/ibm-vga.rom"
    }
}
```

This example reserves 24 KiB for the IBM VGA ROM used in this project. Match the
mapping to your ROM: the generic configuration template instead reserves 32 KiB.
Keep other ROMs and UMBs outside the reserved range. A 32 KiB mapping occupies
C0000h–C7FFFh, so a PCEX ROM at C6000h would overlap it.

## Hercules

```ini
video {
    device = "hgc"
    color = "amber"
    blink = 16
}
```

The device name is `hgc`, including on the command line. Hercules supports
monochrome text and Hercules graphics; software must support that adapter.
Its graphics are not EGA BIOS mode 0Fh.

## MDA

```ini
video {
    device = "mda"
    color = "green"
    blink = 16
}
```

MDA is the monochrome text adapter; choose Hercules or monochrome EGA when the
application requires graphics.

For both MDA and Hercules, `color` accepts `amber`, `green`, or `gray`. Optional
RGB overrides are `color_background`, `color_dim`, `color_normal`, and
`color_bright`; Hercules also supports `color_graphics`. For example:

```ini
    color_background = 0x000000
    color_normal = 0x00aa25
    color_bright = 0x55ff7a
```

Place these inside the selected `video` section.

## CGA composite output

Yes: the CGA implementation has composite graphics rendering. Use the CGA
configuration above, then send one of these commands from the **PCE monitor**
(not the guest DOS prompt):

```text
m emu.video.composite auto
m emu.video.composite 1
m emu.video.composite 0
```

| Value | Behavior |
| --- | --- |
| `auto` | Default: composite in high-resolution CGA graphics when color burst is enabled. |
| `1` | Force composite rendering in both CGA graphics resolutions. |
| `0` | Disable composite rendering; use RGB output. |

To cycle the choices:

```text
m emu.video.composite.cycle
```

There is currently **no `composite` key parsed by the CGA `video` section**.
These messages affect the running emulator and must be reapplied after restart
when a nondefault setting is wanted. The implementation selects composite paths
for graphics, not text. These controls belong to `device = "cga"`; they do not
turn VGA/EGA CGA-compatible modes into composite output.

See [monitor commands](monitor.md), [terminal keys](keys.md), and
[emulator messages](messages.md) for operating the monitor.

## Practical choices and checks

- Use VGA for general DOS applications, EGA when testing EGA-specific software,
  and the actual CGA device for composite games or CGA-specific behavior.
- Leave retrace IRQs disabled for EGA/VGA unless an application needs them.
- `blink = 16` sets the text/cursor blink interval; zero disables blinking.
- PCE sets system display equipment information from the selected adapter,
  including AT CMOS display type. EGA monochrome still uses the EGA category.
- Check the startup log for the selected device and successful video-ROM loading.
  Recheck UMB/PCEX placement whenever the video ROM mapping changes.
- For monochrome EGA acceptance, first check boot text, then a mode-0Fh pattern
  with dark/bright pixels, then return to text. Source inspection alone does not
  replace that small guest test.

Other implemented choices in the configuration template are `plantronics`
(ColorPlus), `olivetti` (M24), and `wy700` (Wyse 700). Use those for software that
explicitly supports them; they are not needed for the standard configurations
above.
