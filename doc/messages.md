# PCE Messages

Messages are used to control the emulator. They can be sent using the "m"
command in the monitor, for example

```text
-m emu.disk.eject 0
```

will eject the first floppy disk. Message prefixes can be omitted, as long
as the message name is unambiguous (except where noted). The following
commands are equivalent:

```text
-m disk.eject 0
-m eject 0
```

A message has at most one parameter. If the parameter contains spaces, it
must be enclosed in double quotes in the monitor, like this:

```text
-m term.screenshot "my screenshot.ppm"
```

## PCE/atarist Messages

```text
emu.cpu.model <model>
	Set the emulated CPU model. Valid models are "68000", "68010" and
	"68020"
```

```text
emu.cpu.speed <speed>
	Set the emulated CPU clock frequency to a multiple of 8 MHz.
```

```text
emu.cpu.speed.step <step>
	Add <step> to the CPU clock frequency multiplier.
```

```text
emu.exit
	Terminate the emulator immediately.
```

```text
emu.midi.file <filename>
	Set a new MIDI output file. If <filename> is the empty string, no
	more MIDI output is saved.
```

```text
emu.pause "0" | "1"
	Resume or pause the emulator.
```

```text
emu.pause.toggle
	Resume or pause the emulator.
```

```text
emu.reset
	Reset the emulated machine.
```

```text
emu.stop
	Fall back to the monitor.
```

```text
emu.viking "0" | "1"
	Switch from or to the viking monochrome display.
```

```text
emu.viking.toggle
	Switch from or to the viking monochrome display.
```

## PCE/ibmpc Messages

```text
emu.exit
	Terminate the emulator immediately.
```

```text
emu.stop
	Fall back to the monitor.
```

```text
emu.pause "0" | "1"
	Resume or pause the emulator.
```

```text
emu.pause.toggle
	Resume or pause the emulator.
```

```text
emu.reset
	Reset the emulated machine.
```

```text
emu.config.save <filename>
	Save the current configuration to <filename>.
```

```text
emu.cpu.model <model>
	Set the emulated CPU model. Valid models are "8086", "8088",
	"80186" and "80188".
```

```text
emu.cpu.speed <factor>
	Set the emulated CPU clock frequency to a multiple of 4.77 MHz.
```

```text
emu.cpu.speed.step <adjustment>
	Add <adjustment> to the CPU clock frequency multiplier.
```

```text
emu.disk.boot <bootdrive>
	Set the boot drive.
```

```text
emu.fdc.accurate "0" | "1"
	Disable or enable the FDC accurate mode.
```

```text
emu.fdc.verbose <level>
	Set the FDC verbosity level.
```

```text
emu.par1.file <filename>
	Set the output file for parallel port 1.
```

```text
emu.par2.file <filename>
	Set the output file for parallel port 2.
```

```text
emu.par3.file <filename>
	Set the output file for parallel port 3.
```

```text
emu.parport.driver <port>":"<driver>
	Attach a parallel port to a new character driver. The driver
	string has the same format as in the config file.
```

```text
emu.parport.file <port>":"<filename>
	This is equivalent to
	emu.parport.driver <port>:stdio:file=<filename>.
```

```text
emu.serport.driver <port>":"<driver>
	Attach a serial port to a new character driver. The driver
	string has the same format as in the config file.
```

```text
emu.serport.file <port>":"<filename>
	This is equivalent to
	emu.serport.driver <port>:stdio:file=<filename>.
```

```text
emu.video.blink <blink-rate>
	Set the cursor blink rate. The number specified is the number
	of screen refreshes for which the cursor is visible/invisible.
```

```text
emu.video.composite "0" | "1" | "auto"
	Set CGA composite mode. A value of "0" turns off composite mode
	in all cases. A value of "1" turns on composite mode in all graphics
	modes. A value of "auto" turns on composite mode in high resolution
	graphics mode if the color burst is enabled.
```

```text
emu.video.composite.cycle
	Cycle through the three possible values for CGA composite mode.
```

## PCE/macplus Messages

```text
emu.cpu.model <model>
	Set the emulated CPU model. Valid models are "68000" and "68010".
```

```text
emu.cpu.speed <speed>
	Set the emulated CPU clock frequency to a multiple of 7.8336MHz.
```

```text
emu.cpu.speed.step <step>
	Add <step> to the CPU clock frequency multiplier.
```

```text
emu.exit
	Terminate the emulator immediately.
```

```text
emu.iwm.ro <drive>
	Set the drive <drive> to read-only (locked). The first
	drive is 1, if <drive> is 0, set all drives to read-only.
```

```text
emu.iwm.rw <drive>
	Set the drive <drive> to read/write (unlocked). The first
	drive is 1, if <drive> is 0, set all drives to read/write.
```

```text
emu.iwm.status
	Print status information on all IWM drives.
```

```text
emu.mac.insert [<drive>]
	Simulate re-inserting a disk that has been dragged to the trash.
```

```text
emu.pause "0" | "1"
	Pause or resume the emulation.
```

```text
emu.pause.toggle
	Pause or resume the emulation.
```

```text
emu.reset
	Reset the emulated machine.
```

```text
emu.ser1.driver <driver>
	Attach the first serial port to a new character driver. The driver
	string has the same format as in the config file.
```

```text
emu.ser1.file <filename>
	This is equivalent to
	emu.ser1.driver "stdio:file=<filename>"
```

```text
emu.ser1.multi <count>
	Set the multi char count for the first serial port.
```

emu.ser2.driver `<driver>`
emu.ser2.file `<filename>`
emu.ser2.multi `<count>`

```text
emu.stop
	Fall back to the monitor.
```

```text
emu.video.blink <rate>
	Set the blink rate for the text mode cursor and blinking characters.
	Higher values cause slower blinking. A value of 0 turns blinking off.
	If <rate> is "off" or "on", turn blinking characters and the text
	mode cursor permanently off or on.
```

```text
emu.video.brightness <brightness>
	Set the video brightness in the range of 0 - 1000.
```

## Cassette messages

```text
cas.commit
	Write the cassette image back to disk.
```

```text
cas.create <filename>
	Create a new cassette and insert it for writing.
```

```text
cas.open <filename>
	Open a cassette image and insert it for writing. Also, remove the
	read image.
```

```text
cas.play
	Press play on the cassette player.
```

```text
cas.load <pos>
	Move the read position to <pos>.
```

```text
cas.read <filename>
	Open a cassette image and insert it for reading.
```

```text
cas.record
	Press play and record on the cassette player.
```

```text
cas.space <duration>
	Add <duration> seconds of silence to the end of the tape.
```

```text
cas.state
	Print the cassette state.
```

```text
cas.stop
	Press stop on the cassette player.
```

```text
cas.truncate <pos>
	Truncate the write tape at position <pos>.
```

```text
cas.write <filename>
	Open a cassette image and insert it for writing.
```

## Disk messages

```text
disk.commit [<id> | "all"]
	Commit changes to drives that have copy-on-write enabled.
```

```text
disk.eject [<id>]
	Eject the disk <id> and set the current disk id to <id>.
```

```text
disk.id <id>
	Set the current disk id to <id>.
```

```text
disk.insert <fname>
	Insert a new disk into the current drive. The disk image type
	is "auto". If a disk already inserted in the current drive it is
	ejected first.
```

```text
disk.ro <id>
	Set the read-only flag for the disk in drive <id>.
```

```text
disk.rw <id>
	Clear the read-only flag for the disk in drive <id>.
```

## Terminal messages

Terminal messages may not be abbreviated.

```text
term.escape <key>
	Set the terminal escape key.
```

```text
term.fullscreen "0" | "1"
	Leave or enter fullscreen mode.
```

```text
term.fullscreen.toggle
	Toggle fullscreen mode.
```

```text
term.grab
	Grab the mouse.
```

```text
term.release
	Release the mouse.
```

```text
term.screenshot [<filename>]
	Write a screen shot to <filename> in PNM format.
```

```text
term.title <title>
	Set the terminal window title.
```
