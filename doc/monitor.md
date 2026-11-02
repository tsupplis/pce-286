# The PCE monitor interface

## Generic monitor commands

These commands are available in both PCE/ibmpc and PCE/macplus. Addresses
in PCE/ibmpc take the form `<expr>`[:`<expr>`], in PCE/macplus they are a
single expression.

```text
b c [index]
	Clear a breakpoint. If no breakpoint index is specified, clear all
	breakpoints.
```

```text
b l
	List all breakpoints.
```

```text
b s addr [pass [reset]]
	Set an address breakpoint [pass=1 reset=0]. The breakpoint triggers
	after <addr> has been reached <pass> times. If <reset> is 0 then
	the breakpoint is cleared after triggering, otherwise it is reset with
	the original values of <addr>, <pass> and <reset>.
```

```text
b s x expr [pass [reset]]
	Set an expression breakpoint [pass=1 reset=0]. The breakpoint triggers
	after <expr> evaluates to true (non-zero) <pass> times. Note that
	<expr> is a string and will not be evaluated until the emulator is
	run with breakpoints enabled (gb, see below). If <expr> contains
	whitespace, it must be enclosed in double quotes.
```

```text
c [cnt]
	Clock the emulated machine <cnt> times. The default is 1.
```

```text
di name [drive] ["ro"] ["rw"]
	Insert the disk image <name> into drive <drive>.
```

```text
d [addr [cnt]]
	Display <cnt> bytes of memory at address <addr>.
```

```text
e addr [val | string...]
	Enter bytes into memory.
```

```text
g b [addr...]
	Run with breakpoints. If one or more addresses are given, address
	breakpoints are set at those addresses with pass=1 and reset=0.
	Otherwise only breakpoints set by bss or bsx are used.
```

```text
g
	Run without breakpoints.
```

```text
h
	Print help summary.
```

```text
m msg [val]
	Send a message to the emulation core. Refer to doc/messages.md
	for a list of valid messages.
```

```text
p [cnt]
	Execute cnt instructions, without trace in calls. The default is 1.
```

```text
q
	Quit.
```

```text
r [reg [val]]
	Set a register or print a register value.
```

```text
t [cnt]
	Execute cnt instructions. The default is 1.
```

```text
u [addr [cnt]]
	Disassemble <cnt> bytes at <addr>.
```

```text
v [expr...]
	Evaluate expressions. If no expression is given, print a list of
	all user-defined variables.
```

```text
w <filename> addr cnt
	Save <cnt> bytes of memory at address <addr> to file <filename>.
```

## PCE/ibmpc monitor commands

```text
g far
	Run until CS changes.
```

```text
h m
	Print help on messages.
```

```text
i [b|w] <port>
	Input a byte or word from a port.
```

```text
key [[+|-]<key>...]
	Simulate pressing (+) or releasing (-) a key. Key names are
	documented in doc/keys.md.
```

```text
log int l
	List all interrupt log expressions.
```

```text
log int <n> [<expr>]
	Set an interrupt log expression. Whenever interrupt <n> is triggered
	the expression <expr> is evaluated and if it is non-zero the interrupt
	is logged.
	If <expr> is omitted the interrupt log expression for interrupt <n>
	is cleared.
```

```text
o [b|w] <port> <val>
	Output a byte or word to a port.
```

```text
pq c
	Clear the prefetch queue.
```

```text
pq f
	Fill the prefetch queue.
```

```text
pq s
	Print prefetch queue status.
```

```text
s [what]
	Print status (pc|cpu|mem|pit|ppi|pic|time|uart|video|xms).
```

## PCE/macplus monitor commands

```text
copy src dst cnt
	Copy <cnt> bytes of memory from address <src> to address <dst>.
```

```text
f addr size [val...]
	Find bytes in memory
```

```text
g e [exception]
	Run until exception"
```

```text
halt [val]
	Set halt state [2]
```

```text
reset
	Reset the emulated machine.
```

```text
rte
	Execute to next rte.
```

```text
s [what]
	Print status (cpu|mem|scc|via).
```

## Expressions

Expressions can be used in commands wherever a numerical value is expected.
All expressions are evaluated using 32 bit unsigned arithmetic.

```text
%<ident>	Variables defined by the emulation core (e.g. CPU registers,
		see below)
$<ident>	User defined variables
const		A hexadecimal constant
(<expr>)	Force priority
```

```text
!x		Logical negation
~x		Binary negation
+x		Positive sign (ignored)
-x		Arithmetic negation
```

```text
x * y		Multiplication
x / y		Division
x % y		Modulo
```

```text
x + y		Addition
x - y		Subtractions
```

```text
x << y		Shift left
x >> y		Shift right
```

```text
x < y		Less than
x > y		Greater than
x <= y		Less than or equal
x >= y		Greater than or equal
```

```text
x == y		Equal
x != y		Not equal
```

```text
x & y		Binary and
```

```text
x ^ y		Binary exclusive or
```

```text
x | y		Binary or
```

```text
x && y		Logical and
```

```text
x || y		Logical or
```

```text
x ? y : z	If .. then .. else
```

```text
x = y		Assignment
x += y		Assignemnt with addition
x -= y		Assignment with subtraction
x *= y		Assignment with multiplication
x /= y		Assignment with division
x <<= y		Assignment with shift left
x >>= y		Assignment with shift right
x |= y		Assignment with binary or
x &= y		Assignment with bianry and
x ^= y		Assignment with binary exclusive or
```

```text
x, y		Expression sequence
```

## PCE/ibmpc symbols

```text
%intv					If an interrupt occured before the
					current instruction, %intv contains
					the number of that interrupt. Otherwise
					%intv contains the number of the last
					interrupt plus 256.
```

```text
%ax	%ah	%al			Data registers
%bx	%bh	%bl
%cx	%ch	%cl
%dx	%dh	%dl
%sp					The stack pointer
%bp
%si
%di
```

```text
%cs					Segment registers
%ds
%es
%ss
```

```text
%ip					The instruction pointer
%flags					The flag register
```

## PCE/macplus symbols

```text
%d0	%d0.b	%d0.w	%d0.l		Data registers
%d1	%d1.b	%d1.w	%d1.l
%d2	%d2.b	%d2.w	%d2.l
%d3	%d3.b	%d3.w	%d3.l
%d4	%d4.b	%d4.w	%d4.l
%d5	%d5.b	%d5.w	%d5.l
%d6	%d6.b	%d6.w	%d6.l
%d7	%d7.b	%d7.w	%d7.l
```

```text
%a0	%a0.b	%a0.w	%a0.l		Address registers
%a1	%a1.b	%a1.w	%a1.l
%a2	%a2.b	%a2.w	%a2.l
%a3	%a3.b	%a3.w	%a3.l
%a4	%a4.b	%a4.w	%a4.l
%a5	%a5.b	%a5.w	%a5.l
%a6	%a6.b	%a6.w	%a6.l
%a7	%a7.b	%a7.w	%a7.l
```

```text
%sp					The stack pointer
%usp					The user stack pointer
%ssp					The supervisor stack pointer
```

```text
%sr					The status register
%ccr					The condition code register
```

```text
%pc					The current PC
%lpc					The last PC
```

```text
%op0					The word at (PC)
%op1					The word at (PC + 2)
%op2					The word at (PC + 4)
%op3					The word at (PC + 6)
```
