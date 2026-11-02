# Character drivers

The syntax of a character driver specification is:

```text
<driver>:<option1>=<value1>:<option2>=<value2>...
```

White space around option names and values is ignored. If a value contains
a colon, it must be escaped by repeating it.

If a file name is prefixed by an '@' character, the file is opened for
appending instead of truncated.

```text
These options can be used with any character driver:
	cap=[@]<filename>
		Copy the output to the character driver to <filename>.
```

```text
	log=[@]<filename>
		Log input and output to <filename>.
```

The following is a list of character drivers and their options:

```text
mouse:
	A serial mouse. If more than one mouse is configured, all mice
	will report the same movements and button presses.
```

```text
	protocol=<proto>
		Set the mouse protocol. Valid values are:
```

```text
		microsoft
			A Microsoft two button serial mouse.
```

```text
		msys
			A Mouse Systems three button serial mouse.
```

```text
		The default is "ms".
```

```text
	xmul=<int>
	ydiv=<int>
		The horizontal mouse speed is multiplied by (xmul / xdiv).
```

```text
	ymul=<int>
	ydiv=<int>
		The vertical mouse speed is multiplied by (ymul / ydiv).
```

```text
null:
	Output is discarded and no input is ever provided.
```

```text
posix:
	The "read" and "write" options are used only if "file" is not
	set.
```

```text
	file=<filename>
		Set the input and output file name.
```

```text
	file=-
		Read input from stdin, write output to stdout.
```

```text
	file=--
		Read input from stdin, write output to stderr.
```

```text
	read=<filename>
		Set the input file name.
```

```text
	read=-
		Read input from stdin.
```

```text
	write=<filename>
		Set the output file name.
```

```text
	write=-
		Write output to stdout.
```

```text
	write=--
		Write output to stderr.
```

```text
ppp:
	For this driver to work, tun/tap support must be enabled.
```

```text
	if=<name>
		Set the tun/tap interface name, for example "tun0".
```

```text
	host-ip=<ip-addr>
		Set the host side IP address.
```

```text
	guest-ip=<ip-addr>
		Set the guest side IP address.
```

```text
pty:
	symlink=<filename>
		Create a symlink pointing to the new pseudo terminal device.
```

```text
slip:
	For this driver to work, tun/tap support must be enabled.
```

```text
	if=<name>
		Set the tun/tap interface name, for example "tun0".
```

```text
stdio:
	Output is written to a file. Input is read from a file.
```

```text
	append=[0|1]
		If true, open the output file for appending instead of
		truncating it.
```

```text
	file=<filename>
		Set the output file name.
```

```text
	file=-
		Use standard output as output.
```

```text
	flush=[0|1]
		If true, flush the output file whenever data is written.
```

```text
	read=<filename>
		Set the input file name.
```

```text
	write=<filename>
		Set the output file name. This option is synonymous to
		the "file" options.
```

```text
tcp:
	connect=[0|1]
		If true, initiate a connection to the host and port
		specified in the host and port options. Otherwise
		wait for connections to the port specified in the
		port option.
```

```text
	host=<hostname>
		Set the host name to connect to if connect is true.
```

```text
	port=<port>
		Set the TCP port number.
```

```text
	telnet=[0|1]
		If true, will handle option negotiation for telnet
		binary and character mode. Useful when the serial
		line is a tty, to provide proper terminal emulation.
```

```text
	telnetinit=[0|1]
		If this and "telnet" are true, initiate telnet option
		negotiations immediately after accepting a connection.
		Common telnet clients need this when connecting to
		a port other than 23. Defaults to true.
```

```text
	usectl=[0|1]
		If true, modem status lines are properly handled in
		both directions. Specifically, DSR is always asserted,
		CTS and CD are only asserted when a client is
		connected to the TCP port, and flashing DTR causes the
		TCP connection to close. This is useful when the
		serial line is a tty, to close the connection when
		logged out and, conversely, to log out when the
		connection is closed manually. Defaults to true.
```

```text
tios:
	file=<filename>
		The character device to connect to
```

```text
wincom:
	Windows COM port support. This driver is only available under
	Windows.
```

```text
	port=<port>
		Set the windows COM port to use, for example "COM1" or
		"COM2".
```
