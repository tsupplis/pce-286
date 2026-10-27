# Memory configuration: XT, accelerated XT/286 and AT

This describes the current implementation and its limits. Full 5170 POST and
native DOS memory-manager acceptance remain pending.

| Configuration | Physical memory | PCE DOS helpers |
| --- | --- | --- |
| PC/XT, including 808x/8018x/V20/V30 and existing variants | Preserve each motherboard's conventional RAM, video and ROM map | Existing host-backed XMS/HMA extension and EMS remain available; these are emulator services |
| 5160 with 80286 accelerator | XT motherboard plus configured extended RAM and the 286 address bus | XMS can use physical extended RAM; selecting the CPU does not add AT keyboard/CMOS hardware |
| 5170 AT | Conventional RAM plus contiguous extended RAM starting at 0x100000; keep 0xFE0000–0xFFFFFF reserved for high ROM | Optional convenience services; use a helper-free configuration for native hardware validation |

## RAM and memory reporting

`ram` sections install addressable memory. On the 286, the PCE INT 15h/AH=88h
hook reports contiguous writable memory starting at 1 MB. AT CMOS uses the same
calculation without requiring an `xms` section. ROM and gaps terminate the count.
Host-only XMS storage is not installed extended RAM. The older CPU helper
reporting convention is retained for compatibility.

A contiguous example, leaving one megabyte for XMS after the HMA reservation:

```
ram {
    address = 0x100000
    size = 1088K
}
xms {
    xms_size = 0
    hma = 1
    umb_size = 0
}
```

The 286 helper selects the lowest suitable writable RAM block above 1 MB and
below the high ROM window. It uses one block, not a union of all RAM sections;
its physical pool size replaces `xms_size`. With RAM starting at 1 MB, HMA shares
that RAM and reserves the first 64 KB from XMS allocations. The exposed HMA is
65520 bytes. With a pool starting at 0x110000 or higher, a separately allocated
HMA can remain; prefer contiguous physical RAM for AT configurations.

Without a suitable physical pool, the existing host-backed XMS helper remains
available. Its legacy lock address is not a usable physical extended-memory
allocation. Do not rely on it for protected-mode physical addressing.

## UMB and EMS

UMB is writable space in an explicitly chosen upper-memory window. EMS uses a
configured page frame backed by separate emulator storage. Neither increases
the installed extended RAM reported by CMOS. Their windows must avoid video
RAM, video ROM, BIOS, option ROMs and each other. At setup, a conflicting EMS frame is disabled with a diagnostic identifying
both address ranges. A conflicting UMB window disables UMB service only; XMS
and HMA remain available. Existing RAM, ROM and device mappings take precedence.
EMS is set up before UMB, so UMB yields when the two helper windows overlap.
Checks cover active installed blocks; later mapping changes and general
RAM/ROM/device overlaps still require review.

The regression configuration uses an option ROM at C8000–C9FFF, an 88 KB UMB
at CA000–DFFFF, an EMS frame at E0000–EFFFF and BIOS at F0000–FFFFF. These
addresses are specific to that configuration: another BIOS or expansion card
may occupy them. Preserve Atari PC, M24 and Compaq-specific mappings.

## Choose one extended-memory owner

For native AT validation, omit `xms` and `ems` sections, omit PCEHIMEM/PCEEMM
from guest startup, and disable `patch_bios_init` and `patch_bios_int19`. Supply
physical RAM explicitly. This is a validation target, not a claim that native
AT POST or a particular guest memory manager already works.

For PCE DOS convenience, enable the desired sections and corresponding
PCEHIMEM.SYS / PCEEMM.SYS guest drivers. Do not load another XMS manager alongside
PCEHIMEM: both could allocate the same installed RAM independently. Physical
allocation ownership is not coordinated with a second guest manager.

## Remaining work

- Extend validation to general RAM/ROM/device overlaps and later mapping changes.
- Decide explicit configuration policy for physical versus host-backed XMS,
  multiple RAM blocks and `xms_size` limits.
- Audit global/local A20 reference counts, lock-count limits and remaining XMS
  error semantics; current tests do not establish full XMS conformance.
- Regress real guest managers and DOS drivers on XT, XT/286 and AT separately.
- Keep CMOS persistence and actual BIOS memory detection in the AT POST work.
