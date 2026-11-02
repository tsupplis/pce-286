# Sound drivers

The syntax of a sound driver specification is:

```text
<driver>:<option1>=<value1>:<option2>=<value2>...
```

White space around option names and values is ignored. If a value contains
a colon, it must be escaped by repeating it.

The following options can be used with all sound drivers:

```text
	wav=<filename>
		Write the sound output to a WAV file.
```

```text
	lowpass=<frequency>
		Apply a low-pass filter. If <frequency> is 0 (the default),
		the low-pass filter is disabled.
```

```text
	wavfilter=[0|1]
		If true then the low-pass filter is applied before the sound
		is written to the WAV file.
```

The following is a list of sound drivers and their options:

```text
null:
	The null sound driver that discards output written to it. It can
	still be written to a WAV file as a side effect.
```

```text
wav:
	This is another name for the null sound driver.
```

```text
oss:
	The OSS sound driver.
```

```text
	dev=<filename>
		Set the OSS device.
```

```text
sdl:
	The SDL sound driver.
```
