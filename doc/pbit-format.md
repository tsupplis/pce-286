# PBIT File Format (Version 0)

All integers are in big-endian format.

## File structure

```text
<file header chunk>
```

[<comment chunk>]

```text
For each track:
	<track header chunk>
	[other chunks]
	[<track data chunk>]
```

```text
<end chunk>
```

## File header chunk

```text
0	4	Magic ('PBIT')
4	4	Size (8)
8	4	Version (0)
12	4	File flags
16	4	CRC
```

## Comment chunk

```text
0	4	Magic ('TEXT')
4	4	Size (n)
8	n	Data
8+n	4	CRC
```

## Track header chunk

```text
0	4	Magic ('TRAK')
4	4	Size (20)
8	4	Cylinder
12	4	Head
16	4	Track length in bits
20	4	Bit clock rate
24	4	Track flags
28	4	CRC
```

## Track data chunk

```text
0	4	Magic ('DATA')
4	4	Size (n)
8	n	Track data
n+8	4	CRC
```

## End chunk

```text
0	4	Magic ('END ')
4	4	Size (0)
8	4	CRC (0x3d64af78)
```

## CRC

```text
	- The algorithm used is big-endian CRC-32C with generator
	  polynomial 0x1edc6f41. The CRC value is initialized to 0.
```

```text
	unsigned long pbit_crc (const unsigned char *src, unsigned cnt)
	{
		unsigned      i, j;
		unsigned long crc;
```

```text
		crc = 0;
```

```text
		for (i = 0; i < cnt; i++) {
			crc ^= (unsigned long) (src[i] & 0xff) << 24;
```

```text
			for (j = 0; j < 8; j++) {
				if (crc & 0x80000000) {
					crc = (crc << 1) ^ 0x1edc6f41;
				}
				else {
					crc = crc << 1;
				}
			}
		}
```

```text
		return (crc & 0xffffffff);
	}
```
