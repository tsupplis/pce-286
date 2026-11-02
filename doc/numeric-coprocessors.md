# 8087 and 80287

| Machine | CPU | Configuration | Error signal |
|---|---|---|---|
| PC/XT (5150, 5160) | 8088 or 8086 | `npx { model = "8087" }` | NMI, enabled by port A0h bit 7 |
| AT (5170) | 80286 | `npx { model = "80287" }` | IRQ13 |

Omit `npx` to disable the coprocessor (default). No guest driver required. On an AT with saved CMOS, update the equipment setting when adding the 80287.

## Timing

- **8087** defaults to functional execution. Set `timing = 1` for register add, subtract, multiply, divide and square-root timing.
- **80287** defaults to timed arithmetic. Integer instructions can run while the NPX is busy. WAIT synchronizes completion. Set `timing = 0` for functional execution.
- **80287 clock** follows the CPU by default. Set `clock_hz = 4000000` for a fixed 4 MHz clock. Fixed-clock durations round up to a PIT tick.
- **AT reset** preserves NPX state across CPU-only reset. Port F0h acknowledges the board error latch. Port F1h or a full machine reset resets the NPX.

## Limits and diagnostics

Transcendental results are approximations. Original-chip rounding, selected overflow and stack-error cases, and operand-dependent timing remain open. Unsupported cases stop the CPU and log CS:IP, opcode, control word, status and tags.

| Command | Coverage |
|---|---|
| `make check-npx-unit` | Arithmetic, conversions, transcendental vectors and board wiring |
| `make check-npx-guest` | DOS detection, arithmetic and exception recovery on XT and AT |
| `make npx-trans-probe` | Build the DOS hardware capture utility |

[Guest test prerequisites](testing.md) · [Hardware capture procedure](npx-overflow-probe.md)

Binary distributions must retain the [SoftFloat notice](../src/cpu/e8087/softfloat/LICENSE.md). `make install` places it in `share/pce/licenses/softfloat/`.
