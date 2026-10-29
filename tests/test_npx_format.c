/* PCE raw numeric operand tests. GPL-2.0-or-later. */
#include <cpu/e8087/real80.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    /* +1, -0, infinity, and a deliberately noncanonical encoding.
     * Memory transfers must preserve even unsupported numeric encodings. */
    static const struct {
        unsigned char bytes[10];
        uint64_t sig;
        uint16_t se;
    } cases[] = {
        {{0,0,0,0,0,0,0,0x80,0xff,0x3f}, UINT64_C(0x8000000000000000), 0x3fff},
        {{0,0,0,0,0,0,0,0,0,0x80}, 0, 0x8000},
        {{0,0,0,0,0,0,0,0x80,0xff,0x7f}, UINT64_C(0x8000000000000000), 0x7fff},
        {{0xef,0xcd,0xab,0x89,0x67,0x45,0x23,0x01,0x34,0x92}, UINT64_C(0x0123456789abcdef), 0x9234}
    };
    unsigned i;
    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        e87_real80_t value;
        unsigned char output[12];
        memset(output, 0xa5, sizeof(output));
        e87_real80_load(&value, cases[i].bytes);
        if (value.significand != cases[i].sig || value.sign_exponent != cases[i].se)
            return 1;
        value.significand = cases[i].sig;
        value.sign_exponent = cases[i].se;
        e87_real80_store(output + 1, &value);
        if (memcmp(output + 1, cases[i].bytes, 10) || output[0] != 0xa5 || output[11] != 0xa5)
            return 1;
    }
    puts("PASS NPX 80-bit transfer vectors and write boundaries");
    return 0;
}
