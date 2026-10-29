/* PCE NPX control-state tests. GPL-2.0-or-later. */
#include <cpu/e8087/e8087.h>
#include <stdio.h>
#include <string.h>
static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; fprintf(stderr,"line %d: %s\n",__LINE__,#x); } } while (0)

static void memory_controls(void)
{
    unsigned mode, top, i;
    for (mode=0; mode<2; ++mode) for (top=0; top<8; ++top) {
        e87_t n, before; unsigned char b[96];
        e87_init(&n, E87_80287); n.protected_mode=mode;
        n.status=top<<11; n.tags=0x1234;
        n.instruction_pointer=mode ? 0x1234 : 0xa1234;
        n.operand_pointer=mode ? 0x5678 : 0xb5678;
        n.instruction_selector=0x28; n.operand_selector=0x30; n.opcode=0x321;
        for(i=0;i<8;++i) { n.reg[i].significand=UINT64_C(0x8123456789abcdef)+i; n.reg[i].sign_exponent=0x4000+i; }
        before=n; memset(b,0xa5,sizeof b);
        CHECK(e87_memory_control(&n,0xdd,0x30,1,0)==94);
        CHECK(!memcmp(&n,&before,sizeof n));
        CHECK(e87_memory_control(&n,0xdd,0x30,0,b)==94);
        CHECK(b[94]==0xa5 && b[95]==0xa5);
        CHECK(b[6]==0x34 && b[7]==0x12 && b[10]==0x78 && b[11]==0x56);
        CHECK(b[8]==(mode?0x28:0x21) && b[9]==(mode?0:0xa3));
        CHECK(b[12]==(mode?0x30:0) && b[13]==(mode?0:0xb0));
        CHECK(b[14]==(unsigned char)(0xef+top));
        CHECK(n.tags==0xffff && n.status==0 && n.protected_mode==mode);
        CHECK(e87_memory_control(&n,0xdd,0x20,0,b)==94);
        CHECK(n.status==before.status && n.tags==before.tags);
        CHECK(!memcmp(n.reg,before.reg,sizeof n.reg));
        CHECK(n.instruction_pointer==before.instruction_pointer && n.operand_pointer==before.operand_pointer);
        CHECK(mode ? n.instruction_selector==0x28 && n.operand_selector==0x30 : n.opcode==0x321);
        before=n;
        CHECK(e87_memory_control(&n,0xd9,0x20,1,0)==14);
        CHECK(e87_memory_control(&n,0xd9,0x20,0,b)==14);
        CHECK(!memcmp(n.reg,before.reg,sizeof n.reg));
        CHECK(n.tags==before.tags && n.status==before.status && n.protected_mode==mode);
        CHECK(e87_memory_control(&n,0xd9,0x38,0,b)==2);
        CHECK(b[0]==0x7f && b[1]==3);
        before=n;
        CHECK(!e87_memory_control(&n,0xdd,0x30,0,0));
        CHECK(!memcmp(&n,&before,sizeof n));
        CHECK(!e87_memory_control(&n,0xd9,0x30,0,b)); /* FNSTENV unresolved */
        CHECK(!memcmp(&n,&before,sizeof n));
        n.status=1; b[0]=0x7e; b[1]=3;
        CHECK(e87_memory_control(&n,0xd9,0x28,0,b)==2);
        CHECK(e87_error_signal(&n));
        CHECK(e87_memory_control(&n,0xdd,0x38,0,b)==2 && (b[0]&0x81)==0x81);
        before=n;
        CHECK(!e87_memory_control(&n,0xdd,0x20,0,b));
        CHECK(!memcmp(&n,&before,sizeof n));
    }
}
static void error_signaling(void)
{
    e87_t n;
    unsigned model, bit, masked, iem;
    for (model = E87_8087; model <= E87_80287; ++model)
    for (bit = 1; bit <= 0x20; bit <<= 1)
    for (masked = 0; masked < 2; ++masked)
    for (iem = 0; iem < 2; ++iem) {
        e87_init(&n, (e87_model_t)model);
        if (!masked) n.control &= ~bit;
        if (!iem) e87_control(&n, 0xdb, 0xe0, NULL);
        n.status = 0x2800;
        e87_raise_exception(&n, bit);
        CHECK((n.status & 0x3f) == bit);
        CHECK(!!(n.status & 0x80) == !masked);
        CHECK(!!(n.status & 0x8000) == (!masked && model == E87_8087 && !iem));
        CHECK(e87_error_signal(&n) == (!masked && (model == E87_80287 || !iem)));
        /* Masking the exception afterward does not acknowledge latched IR/ES. */
        n.control |= bit;
        CHECK(!e87_pending_error(&n));
        CHECK(!!(n.status & 0x80) == !masked);
        e87_control(&n, 0xdb, 0xe0, NULL);
        CHECK(e87_error_signal(&n) == !masked);
        e87_control(&n, 0xdb, 0xe1, NULL);
        CHECK(e87_error_signal(&n) == (!masked && model == E87_80287));
        CHECK(!(n.status & 0x8000));
        CHECK(e87_error_wait(&n) == (!masked && model == E87_8087));
        CHECK(e87_error_signal(&n) == !masked);
        CHECK(e87_control(&n, 0xdb, 0xe2, NULL));
        CHECK(n.status == 0x2800 && !n.wait_error && !e87_error_signal(&n));
        n.control &= ~bit;
        e87_raise_exception(&n, bit);
        e87_error_wait(&n);
        e87_fninit(&n);
        CHECK(!n.wait_error && !e87_error_signal(&n) && n.status == 0);
        e87_raise_exception(&n, 0xffc0);
        CHECK(n.status == 0); /* Reserved/control bits are not exceptions. */
    }
}
int main(void)
{
    e87_t n, before;
    memory_controls();
    error_signaling();
    unsigned model, top, logical;
    uint16_t ax;
    for (model = E87_8087; model <= E87_80287; ++model) {
        CHECK(e87_init(&n, (e87_model_t)model));
        CHECK(n.control == (model == E87_8087 ? 0x03ff : 0x037f));
        CHECK(n.status == 0 && n.tags == 0xffff && n.protected_mode == 0);
        n.reg[5].significand = UINT64_C(0xdeadbeef01234567);
        n.reg[5].sign_exponent = 0x1234;
        n.status = 0xffff; n.tags = 0; n.control = 0;
        n.instruction_pointer = 0xabcdef; n.operand_pointer = 0x123456;
        n.instruction_selector = 0x18; n.operand_selector = 0x20;
        n.opcode = 0x7ff;
        if (model == E87_80287) CHECK(e87_control(&n, 0xdb, 0xe4, NULL));
        CHECK(e87_control(&n, 0xdb, 0xe3, NULL));
        CHECK(n.protected_mode == (model == E87_80287));
        CHECK(n.status == 0 && n.tags == 0xffff);
        CHECK(n.instruction_pointer == 0 && n.operand_pointer == 0 && n.opcode == 0);
        CHECK(n.instruction_selector == 0 && n.operand_selector == 0);
        CHECK(n.reg[5].significand == UINT64_C(0xdeadbeef01234567));
        e87_reset(&n);
        CHECK(n.protected_mode == 0 && n.reg[5].sign_exponent == 0x1234);
        CHECK(e87_control(&n, 0xdb, 0xe0, NULL));
        CHECK(n.control == 0x037f);
        CHECK(e87_control(&n, 0xdb, 0xe1, NULL));
        CHECK(n.control == (model == E87_8087 ? 0x03ff : 0x037f));
        n.status = 0x5a25; ax = 0x1234;
        CHECK(e87_control(&n, 0xdf, 0xe0, &ax) == (model == E87_80287));
        CHECK(ax == (model == E87_80287 ? 0x5a25 : 0x1234));
        before = n;
        CHECK(!e87_control(&n, 0xd9, 0xfb, &ax)); /* Later FSINCOS not handled. */
        CHECK(memcmp(&n, &before, sizeof(n)) == 0);
        for (top = 0; top < 8; ++top) {
            n.status = 0xc7ff | (top << 11); n.tags = 0x1234;
            CHECK(e87_control(&n, 0xd9, 0xf7, NULL));
            CHECK(e87_top(&n) == ((top + 1) & 7));
            CHECK((n.status & 0xc7ff) == 0xc5ff && n.tags == 0x1234);
            CHECK(e87_control(&n, 0xd9, 0xf6, NULL));
            CHECK(e87_top(&n) == top);
            for (logical = 0; logical < 8; ++logical) {
                unsigned physical = (top + logical) & 7;
                n.tags = 0;
                CHECK(e87_control(&n, 0xdd, 0xc0 + logical, NULL));
                CHECK(n.tags == (3u << (2 * physical)));
                CHECK(e87_tag(&n, logical) == 3 && e87_top(&n) == top);
            }
        }
    }
    before = n;
    CHECK(!e87_init(&n, (e87_model_t)99));
    CHECK(memcmp(&n, &before, sizeof(n)) == 0);
    printf("NPX state: %u checks, %u failures\n",checks,failures);
    return failures != 0;
}
