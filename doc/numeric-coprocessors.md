# Optional 8087 / 80287 coprocessor

Coprocessors are disabled by default. Add one section to the machine config:

```ini
# With system.model = "5150" or "5160", cpu.model = "8086" or "8088":
npx { model = "8087" }

# Alternatively, with system.model = "5170", cpu.model = "80286":
# npx { model = "80287" }
```

Omitting the section, or using `model = "none"`, keeps the previous no-NPX
behaviour. Other pairings (including NEC and 80186 CPUs) are rejected at startup;
incompatible runtime CPU-model changes are rejected too. No NPX speed setting
or PCE DOS driver is required. This remains **experimental functional emulation**:
physical execution timing, some numeric edge cases and original-chip
transcendental results are not complete. Unsupported numeric cases retain the
CPU's explicit NPX development-stop behaviour. The machine log reports the
stopped CS:IP, opcode bytes, NPX model, control/status words and tags once per
stop. This diagnostic identifies an unsupported instruction or operand case;
it is distinct from a guest numeric exception or a normal WAIT.

The 80287 defaults to `timing = 1`: a partial typical-cycle profile for
register and memory-operand arithmetic, square root and transcendental instructions. Set
`npx { model = "80287" timing = 0 }` for the previous functional timing.
By default each NPX clock follows one emulated CPU clock. To select a fixed
4 MHz execution clock, add `clock_hz = 4000000` inside the 80287 NPX section.
With timing enabled, a positive frequency uses emulated motherboard time across
CPU speed changes. Each instruction duration rounds up to a whole PIT tick
(about 0.84 microseconds). A zero frequency selects the default. Integer execution can overlap NPX occupancy; WAIT and
checking numeric instructions wait for it. Timed arithmetic publishes its result and exception state at the modeled
completion deadline. FNSTSW during execution reports BUSY with the prior
status. The board raises IRQ13 for a newly signalled error at completion.
With 80287 timing enabled, each operand word transferred between the NPX and
memory occupies four CPU clocks at an even physical address or six at an odd
address. The emulator accounts for these zero-wait-state bus cycles together
at the ESC instruction. Memory wait states, bus arbitration, individual
PEREQ/PEACK transfers, operand-dependent execution and exception penalties
remain open. On an XT, `npx { model = "8087" timing = 1 }` enables
representative CPU-clock durations for register add, subtract, multiply,
divide and square root. Other XT forms retain functional timing. The
8087 defaults to timing disabled.
This profile improves pacing but does not make benchmark MHz hardware-accurate.

XT presence is reported through the equipment switches; errors use NMI with
port A0h bit 7 enabling delivery. AT presence sets the initial CMOS equipment
bit, and errors use IRQ13 instead of CPU exception 16. Port F0h acknowledges the
board error/BUSY latch without clearing NPX exception flags; F1h resets the NPX,
including its protected-mode state. CPU-only reset preserves the NPX. Full
machine reset resets it. Saved CMOS files and guest CMOS writes retain their
existing precedence: update the saved setup when adding a coprocessor.

`make check-npx-board` tests actual machine wiring without external ROMs/media,
including protected-mode IRQ13 delivery. It is also part of `check-npx-unit`
and the cross-CPU regression gate. `make check-npx-guest` tests BIOS equipment
reporting, arithmetic, divide-by-zero handling and recovery in DOS on XT
(GLABIOS + PCEX) and native IBM/AMI AT. It needs NASM, mtools, `images/dos.img`,
the ROMs listed above and the built `ibmpc-pcex.rom`; it creates disposable
images under `tests/memory-validation/npx-board-guest`. IBM uses its established
2x diagnostic speed for early POST; AMI uses 24x. These are bounded smoke tests,
not full application or OS/2 coprocessor acceptance.

The IBM PC binary includes the SoftFloat subset. `make install` also installs
its BSD notice under `share/pce/licenses/softfloat/LICENSE.md`; retain that notice
when distributing binaries separately.

`make check-npx-trans` checks the development NPX constant loads and the
F2XM1, FYL2X, FYL2XP1, FPATAN and FPTAN approximation paths. It is included in
`check-npx-unit`. Reference vectors are mathematical values, not chip readbacks;
passing them does not establish bit-identical 8087/287 transcendental behavior.
FPTAN returns a functional numerator/denominator pair; its individual chip
values/scaling and masked stack-error results remain unverified. Transcendental
range results remain unsupported except masked/unmasked F2XM1 and FPATAN
underflow. These use functional approximations; exact chip-result bits remain
unverified. Regenerating
`tests/npx_trans_vectors.h` is optional and requires Python with mpmath 1.3.0:
run `python3 tests/generate_npx_trans_vectors.py` from the source root. The
emulator and ordinary tests have no Python or host-floating-point dependency.
`make npx-trans-probe` builds an explicit real-chip capture utility; see
[NPX characterization instructions](npx-overflow-probe.md). It is never
run by the ordinary check targets.
