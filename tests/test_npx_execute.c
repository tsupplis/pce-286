/* Known-value instruction sequences and transactional rejection. GPL-2.0-or-later. */
#include <cpu/e8087/execute.h>
#include <stdio.h>
#include <string.h>
static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("line %d: %s\n", __LINE__, #x); } } while (0)
static void rejected(e87_t *n, unsigned op, unsigned mr, unsigned char *in)
{
    e87_t before = *n;
    unsigned char out[10], sentinel[10];
    memset(out, 0xa5, 10); memcpy(sentinel, out, 10);
    CHECK(!e87_numeric(n, op, mr, in, out));
    CHECK(!memcmp(n, &before, sizeof(*n)));
    CHECK(!memcmp(out, sentinel, 10));
}
static e87_real80_t whole(int value)
{
    unsigned sign=value<0,exponent=0;uint64_t v=sign?-(int64_t)value:value,copy=v;
    if(!v) return (e87_real80_t){0,0};
    while(copy>>1) {copy>>=1;++exponent;}
    return (e87_real80_t){v<<(63-exponent),(sign<<15)|(0x3fff+exponent)};
}
static void same_value(e87_real80_t a,e87_real80_t b)
{ CHECK(a.significand==b.significand && a.sign_exponent==b.sign_exponent); }
static void memory_arithmetic_checks(void)
{
    const unsigned ops[]={0xd8,0xdc,0xda,0xde},groups[]={0,1,4,5,6,7};
    const unsigned sizes[]={4,8,4,2};
    for(unsigned m=0;m<2;++m) for(unsigned f=0;f<4;++f) for(unsigned g=0;g<6;++g)
    for(unsigned neg=0;neg<2;++neg) for(unsigned t=0;t<8;++t) {
        e87_t n;e87_init(&n,m);n.status=t<<11;n.tags=0xffff&~(3u<<(2*t));n.reg[t]=whole(8);
        uint64_t raw=f==0?0x40000000:f==1?UINT64_C(0x4000000000000000):2;
        if(neg) raw=f<2?raw|(UINT64_C(1)<<(sizes[f]*8-1)):((UINT64_C(1)<<(sizes[f]*8))-2);
        unsigned char b[8];for(unsigned k=0;k<sizes[f];++k)b[k]=raw>>(8*k);
        e87_t before=n;
        CHECK(e87_cpu_memory(&n,ops[f],groups[g]<<3,1,0)==((sizes[f]<<2)|1));
        CHECK(e87_cpu_memory(&n,ops[f],groups[g]<<3,0,b));
        int y=neg?-2:2,result=g==0?8+y:g==1?8*y:g==2?8-y:g==3?y-8:g==4?8/y:0;
        same_value(n.reg[t],g==5?(e87_real80_t){UINT64_C(0x8000000000000000),0x3ffd|(neg<<15)}:whole(result));
        CHECK(n.tags==before.tags && e87_top(&n)==t && !(n.status&0x3f));
        for(unsigned k=0;k<8;++k) if(k!=t) same_value(n.reg[k],before.reg[k]);
    }
    for(unsigned m=0;m<2;++m) for(unsigned f=0;f<2;++f) for(unsigned mask=0;mask<2;++mask) {
        e87_t n;e87_init(&n,m);n.tags=0xfffc;n.reg[0]=whole(1);n.control=(n.control&~2u)|(mask?2:0);
        unsigned char b[8]={1};e87_real80_t before=n.reg[0];
        CHECK(e87_numeric(&n,ops[f],0x08,b,0));
        CHECK((n.status&0x3f)==2);
        if(mask) {CHECK(n.reg[0].significand==(UINT64_C(1)<<(f?11:40)));CHECK(n.reg[0].sign_exponent==(unsigned)(0x3fff-(f?1022:126)));}
        else same_value(n.reg[0],before);
    }
    for(unsigned m=0;m<2;++m) for(unsigned f=2;f<4;++f) {
        e87_t n;e87_init(&n,m);n.tags=0xfffc;n.reg[0]=whole(1);unsigned char b[4]={0};b[sizes[f]-1]=0x80;
        CHECK(e87_numeric(&n,ops[f],8,b,0));
        CHECK(n.reg[0].significand==UINT64_C(0x8000000000000000));
        CHECK(n.reg[0].sign_exponent==(0xbfff+sizes[f]*8-1));
    }
}
static void other_arithmetic_checks(void)
{
    for(unsigned m=0;m<2;++m) for(unsigned t=0;t<8;++t) {
        e87_t n;e87_init(&n,m);n.tags=0xffff&~(3u<<(2*t));n.status=t<<11;n.reg[t]=whole(-8);
        CHECK(e87_numeric(&n,0xd9,0xe1,0,0));same_value(n.reg[t],whole(8));
        CHECK(e87_numeric(&n,0xd9,0xe0,0,0));same_value(n.reg[t],whole(-8));
        CHECK(e87_numeric(&n,0xd9,0xf4,0,0));
        CHECK(e87_top(&n)==((t+7)&7));same_value(n.reg[t],whole(3));same_value(n.reg[(t+7)&7],whole(-1));
    }
    for(unsigned m=0;m<2;++m) for(unsigned sign=0;sign<2;++sign) {
        e87_t n;e87_init(&n,m);n.tags=0xfffd;n.reg[0]=(e87_real80_t){0,sign<<15};
        CHECK(e87_numeric(&n,0xd9,0xf4,0,0));
        same_value(n.reg[0],(e87_real80_t){0,sign<<15});same_value(n.reg[7],n.reg[0]);CHECK(!(n.status&0x3f));
    }
    for(unsigned m=0;m<2;++m) for(unsigned rc=0;rc<4;++rc) for(unsigned sign=0;sign<2;++sign)
    for(unsigned odd=0;odd<2;++odd) for(unsigned mask=0;mask<2;++mask) {
        e87_t n;e87_init(&n,m);n.tags=0xfffc;n.control=(n.control&~0xc20u)|(rc<<10)|(mask?0x20:0);
        n.reg[0]=(e87_real80_t){odd?UINT64_C(0xe000000000000000):UINT64_C(0xa000000000000000),0x4000|(sign<<15)};
        unsigned up=rc==0?odd:rc==1?sign:rc==2?!sign:0;
        CHECK(e87_numeric(&n,0xd9,0xfc,0,0));same_value(n.reg[0],whole((sign?-1:1)*(int)(2+odd+up)));
        CHECK((n.status&0xa0)==(mask?0x20:0xa0));CHECK(!!(n.status&0x200)==up);
    }
    for(unsigned m=0;m<2;++m) for(int scale=-15;scale<16;++scale) {
        e87_t n;e87_init(&n,m);n.tags=0xfff0;n.reg[0]=whole(3);n.reg[1]=whole(scale);
        CHECK(e87_numeric(&n,0xd9,0xfd,0,0));CHECK(n.reg[0].significand==UINT64_C(0xc000000000000000));
        CHECK(n.reg[0].sign_exponent==0x4000+scale);same_value(n.reg[1],whole(scale));CHECK(!(n.status&0x3f));
    }
    for(unsigned m=0;m<2;++m) for(unsigned q=0;q<32;++q) for(unsigned sign=0;sign<2;++sign) {
        e87_t n;e87_init(&n,m);n.tags=0xfff0;n.reg[0]=whole((sign?-1:1)*(int)(q*3+1));n.reg[1]=whole(3);n.status=0x4700;
        CHECK(e87_numeric(&n,0xd9,0xf8,0,0));same_value(n.reg[0],whole(sign?-1:1));
        unsigned cc=((q&1)?0x200:0)|(q<2?0x4000:(q&2)?0x4000:0)|(q<4?0x100:(q&4)?0x100:0);
        CHECK((n.status&0x4700)==cc);CHECK(!(n.status&0x3f));
    }
    for(unsigned m=0;m<2;++m) {
        e87_t n;e87_init(&n,m);n.tags=0xfff0;n.reg[0]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff+200};n.reg[1]=whole(3);
        unsigned loops=0;do {CHECK(e87_numeric(&n,0xd9,0xf8,0,0));++loops;} while((n.status&0x400)&&loops<8);
        CHECK(loops>1 && loops<8);same_value(n.reg[0],whole(1));CHECK(!(n.status&0x3f));
        n.reg[1]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3ffe};rejected(&n,0xd9,0xfd,0);
    }
    const unsigned unary[]={0xe0,0xe1,0xf4,0xfa,0xfc},binary[]={0xf8,0xfd};
    for(unsigned m=0;m<2;++m) for(unsigned mask=0;mask<2;++mask) for(unsigned k=0;k<7;++k) {
        e87_t n;e87_init(&n,m);n.control=(n.control&~1u)|mask;
        CHECK(e87_numeric(&n,0xd9,k<5?unary[k]:binary[k-5],0,0));
        CHECK((n.status&0x81)==(mask?1:0x81));
        if(!mask) CHECK(n.tags==0xffff && e87_top(&n)==0);
        else CHECK(n.reg[k==2?7:0].sign_exponent==0xffff);
    }
}

static void square_root_vectors(void)
{
 static const struct {uint64_t sig,out;unsigned exp,oe,pc,rc,flags;} v[]={
 {UINT64_C(0x8332dd768e5592bf),UINT64_C(0xb744670000000000),0x29a2,0x34d0,0x0,0,0x220},
 {UINT64_C(0x8332dd768e5592bf),UINT64_C(0xb744660000000000),0x29a2,0x34d0,0x0,1,0x20},
 {UINT64_C(0x8332dd768e5592bf),UINT64_C(0xb744670000000000),0x29a2,0x34d0,0x0,2,0x220},
 {UINT64_C(0x8332dd768e5592bf),UINT64_C(0xb744660000000000),0x29a2,0x34d0,0x0,3,0x20},
 {UINT64_C(0x8332dd768e5592bf),UINT64_C(0xb74466f333928800),0x29a2,0x34d0,0x200,0,0x220},
 {UINT64_C(0x8332dd768e5592bf),UINT64_C(0xb74466f333928000),0x29a2,0x34d0,0x200,1,0x20},
 {UINT64_C(0x8332dd768e5592bf),UINT64_C(0xb74466f333928800),0x29a2,0x34d0,0x200,2,0x220},
 {UINT64_C(0x8332dd768e5592bf),UINT64_C(0xb74466f333928000),0x29a2,0x34d0,0x200,3,0x20},
 {UINT64_C(0x8332dd768e5592bf),UINT64_C(0xb74466f333928409),0x29a2,0x34d0,0x300,0,0x20},
 {UINT64_C(0x8332dd768e5592bf),UINT64_C(0xb74466f333928409),0x29a2,0x34d0,0x300,1,0x20},
 {UINT64_C(0x8332dd768e5592bf),UINT64_C(0xb74466f33392840a),0x29a2,0x34d0,0x300,2,0x220},
 {UINT64_C(0x8332dd768e5592bf),UINT64_C(0xb74466f333928409),0x29a2,0x34d0,0x300,3,0x20},
 {UINT64_C(0x8f36f1954380786c),UINT64_C(0x8764ce0000000000),0x7af3,0x5d79,0x0,0,0x220},
 {UINT64_C(0x8f36f1954380786c),UINT64_C(0x8764cd0000000000),0x7af3,0x5d79,0x0,1,0x20},
 {UINT64_C(0x8f36f1954380786c),UINT64_C(0x8764ce0000000000),0x7af3,0x5d79,0x0,2,0x220},
 {UINT64_C(0x8f36f1954380786c),UINT64_C(0x8764cd0000000000),0x7af3,0x5d79,0x0,3,0x20},
 {UINT64_C(0x8f36f1954380786c),UINT64_C(0x8764cdd764787800),0x7af3,0x5d79,0x200,0,0x20},
 {UINT64_C(0x8f36f1954380786c),UINT64_C(0x8764cdd764787800),0x7af3,0x5d79,0x200,1,0x20},
 {UINT64_C(0x8f36f1954380786c),UINT64_C(0x8764cdd764788000),0x7af3,0x5d79,0x200,2,0x220},
 {UINT64_C(0x8f36f1954380786c),UINT64_C(0x8764cdd764787800),0x7af3,0x5d79,0x200,3,0x20},
 {UINT64_C(0x8f36f1954380786c),UINT64_C(0x8764cdd764787b36),0x7af3,0x5d79,0x300,0,0x220},
 {UINT64_C(0x8f36f1954380786c),UINT64_C(0x8764cdd764787b35),0x7af3,0x5d79,0x300,1,0x20},
 {UINT64_C(0x8f36f1954380786c),UINT64_C(0x8764cdd764787b36),0x7af3,0x5d79,0x300,2,0x220},
 {UINT64_C(0x8f36f1954380786c),UINT64_C(0x8764cdd764787b35),0x7af3,0x5d79,0x300,3,0x20},
 {UINT64_C(0xb562ac372aa13efc),UINT64_C(0xd77cb00000000000),0x5090,0x4847,0x0,0,0x220},
 {UINT64_C(0xb562ac372aa13efc),UINT64_C(0xd77caf0000000000),0x5090,0x4847,0x0,1,0x20},
 {UINT64_C(0xb562ac372aa13efc),UINT64_C(0xd77cb00000000000),0x5090,0x4847,0x0,2,0x220},
 {UINT64_C(0xb562ac372aa13efc),UINT64_C(0xd77caf0000000000),0x5090,0x4847,0x0,3,0x20},
 {UINT64_C(0xb562ac372aa13efc),UINT64_C(0xd77cafeabb25a000),0x5090,0x4847,0x200,0,0x220},
 {UINT64_C(0xb562ac372aa13efc),UINT64_C(0xd77cafeabb259800),0x5090,0x4847,0x200,1,0x20},
 {UINT64_C(0xb562ac372aa13efc),UINT64_C(0xd77cafeabb25a000),0x5090,0x4847,0x200,2,0x220},
 {UINT64_C(0xb562ac372aa13efc),UINT64_C(0xd77cafeabb259800),0x5090,0x4847,0x200,3,0x20},
 {UINT64_C(0xb562ac372aa13efc),UINT64_C(0xd77cafeabb259e09),0x5090,0x4847,0x300,0,0x220},
 {UINT64_C(0xb562ac372aa13efc),UINT64_C(0xd77cafeabb259e08),0x5090,0x4847,0x300,1,0x20},
 {UINT64_C(0xb562ac372aa13efc),UINT64_C(0xd77cafeabb259e09),0x5090,0x4847,0x300,2,0x220},
 {UINT64_C(0xb562ac372aa13efc),UINT64_C(0xd77cafeabb259e08),0x5090,0x4847,0x300,3,0x20},
 {UINT64_C(0xc1c6ea01908bd114),UINT64_C(0x9d7dbb0000000000),0x1e47,0x2f23,0x0,0,0x220},
 {UINT64_C(0xc1c6ea01908bd114),UINT64_C(0x9d7dba0000000000),0x1e47,0x2f23,0x0,1,0x20},
 {UINT64_C(0xc1c6ea01908bd114),UINT64_C(0x9d7dbb0000000000),0x1e47,0x2f23,0x0,2,0x220},
 {UINT64_C(0xc1c6ea01908bd114),UINT64_C(0x9d7dba0000000000),0x1e47,0x2f23,0x0,3,0x20},
 {UINT64_C(0xc1c6ea01908bd114),UINT64_C(0x9d7dbae821869000),0x1e47,0x2f23,0x200,0,0x220},
 {UINT64_C(0xc1c6ea01908bd114),UINT64_C(0x9d7dbae821868800),0x1e47,0x2f23,0x200,1,0x20},
 {UINT64_C(0xc1c6ea01908bd114),UINT64_C(0x9d7dbae821869000),0x1e47,0x2f23,0x200,2,0x220},
 {UINT64_C(0xc1c6ea01908bd114),UINT64_C(0x9d7dbae821868800),0x1e47,0x2f23,0x200,3,0x20},
 {UINT64_C(0xc1c6ea01908bd114),UINT64_C(0x9d7dbae821868c2e),0x1e47,0x2f23,0x300,0,0x20},
 {UINT64_C(0xc1c6ea01908bd114),UINT64_C(0x9d7dbae821868c2e),0x1e47,0x2f23,0x300,1,0x20},
 {UINT64_C(0xc1c6ea01908bd114),UINT64_C(0x9d7dbae821868c2f),0x1e47,0x2f23,0x300,2,0x220},
 {UINT64_C(0xc1c6ea01908bd114),UINT64_C(0x9d7dbae821868c2e),0x1e47,0x2f23,0x300,3,0x20},
 {UINT64_C(0x92be4511069f9b79),UINT64_C(0x890d340000000000),0x6b33,0x5599,0x0,0,0x20},
 {UINT64_C(0x92be4511069f9b79),UINT64_C(0x890d340000000000),0x6b33,0x5599,0x0,1,0x20},
 {UINT64_C(0x92be4511069f9b79),UINT64_C(0x890d350000000000),0x6b33,0x5599,0x0,2,0x220},
 {UINT64_C(0x92be4511069f9b79),UINT64_C(0x890d340000000000),0x6b33,0x5599,0x0,3,0x20},
 {UINT64_C(0x92be4511069f9b79),UINT64_C(0x890d342ee0273800),0x6b33,0x5599,0x200,0,0x220},
 {UINT64_C(0x92be4511069f9b79),UINT64_C(0x890d342ee0273000),0x6b33,0x5599,0x200,1,0x20},
 {UINT64_C(0x92be4511069f9b79),UINT64_C(0x890d342ee0273800),0x6b33,0x5599,0x200,2,0x220},
 {UINT64_C(0x92be4511069f9b79),UINT64_C(0x890d342ee0273000),0x6b33,0x5599,0x200,3,0x20},
 {UINT64_C(0x92be4511069f9b79),UINT64_C(0x890d342ee027346c),0x6b33,0x5599,0x300,0,0x20},
 {UINT64_C(0x92be4511069f9b79),UINT64_C(0x890d342ee027346c),0x6b33,0x5599,0x300,1,0x20},
 {UINT64_C(0x92be4511069f9b79),UINT64_C(0x890d342ee027346d),0x6b33,0x5599,0x300,2,0x220},
 {UINT64_C(0x92be4511069f9b79),UINT64_C(0x890d342ee027346c),0x6b33,0x5599,0x300,3,0x20},
 {UINT64_C(0xf884122cd182290d),UINT64_C(0xb25a8b0000000000),0x2c81,0x3640,0x0,0,0x220},
 {UINT64_C(0xf884122cd182290d),UINT64_C(0xb25a8a0000000000),0x2c81,0x3640,0x0,1,0x20},
 {UINT64_C(0xf884122cd182290d),UINT64_C(0xb25a8b0000000000),0x2c81,0x3640,0x0,2,0x220},
 {UINT64_C(0xf884122cd182290d),UINT64_C(0xb25a8a0000000000),0x2c81,0x3640,0x0,3,0x20},
 {UINT64_C(0xf884122cd182290d),UINT64_C(0xb25a8ad53653a800),0x2c81,0x3640,0x200,0,0x220},
 {UINT64_C(0xf884122cd182290d),UINT64_C(0xb25a8ad53653a000),0x2c81,0x3640,0x200,1,0x20},
 {UINT64_C(0xf884122cd182290d),UINT64_C(0xb25a8ad53653a800),0x2c81,0x3640,0x200,2,0x220},
 {UINT64_C(0xf884122cd182290d),UINT64_C(0xb25a8ad53653a000),0x2c81,0x3640,0x200,3,0x20},
 {UINT64_C(0xf884122cd182290d),UINT64_C(0xb25a8ad53653a515),0x2c81,0x3640,0x300,0,0x220},
 {UINT64_C(0xf884122cd182290d),UINT64_C(0xb25a8ad53653a514),0x2c81,0x3640,0x300,1,0x20},
 {UINT64_C(0xf884122cd182290d),UINT64_C(0xb25a8ad53653a515),0x2c81,0x3640,0x300,2,0x220},
 {UINT64_C(0xf884122cd182290d),UINT64_C(0xb25a8ad53653a514),0x2c81,0x3640,0x300,3,0x20},
 {UINT64_C(0x97fcd16e492db054),UINT64_C(0x8b7aa40000000000),0x3415,0x3a0a,0x0,0,0x220},
 {UINT64_C(0x97fcd16e492db054),UINT64_C(0x8b7aa30000000000),0x3415,0x3a0a,0x0,1,0x20},
 {UINT64_C(0x97fcd16e492db054),UINT64_C(0x8b7aa40000000000),0x3415,0x3a0a,0x0,2,0x220},
 {UINT64_C(0x97fcd16e492db054),UINT64_C(0x8b7aa30000000000),0x3415,0x3a0a,0x0,3,0x20},
 {UINT64_C(0x97fcd16e492db054),UINT64_C(0x8b7aa3e12f033800),0x3415,0x3a0a,0x200,0,0x20},
 {UINT64_C(0x97fcd16e492db054),UINT64_C(0x8b7aa3e12f033800),0x3415,0x3a0a,0x200,1,0x20},
 {UINT64_C(0x97fcd16e492db054),UINT64_C(0x8b7aa3e12f034000),0x3415,0x3a0a,0x200,2,0x220},
 {UINT64_C(0x97fcd16e492db054),UINT64_C(0x8b7aa3e12f033800),0x3415,0x3a0a,0x200,3,0x20},
 {UINT64_C(0x97fcd16e492db054),UINT64_C(0x8b7aa3e12f033a26),0x3415,0x3a0a,0x300,0,0x220},
 {UINT64_C(0x97fcd16e492db054),UINT64_C(0x8b7aa3e12f033a25),0x3415,0x3a0a,0x300,1,0x20},
 {UINT64_C(0x97fcd16e492db054),UINT64_C(0x8b7aa3e12f033a26),0x3415,0x3a0a,0x300,2,0x220},
 {UINT64_C(0x97fcd16e492db054),UINT64_C(0x8b7aa3e12f033a25),0x3415,0x3a0a,0x300,3,0x20},
 {UINT64_C(0x8da9b179b99c6aa4),UINT64_C(0x86a8840000000000),0x064f,0x2327,0x0,0,0x220},
 {UINT64_C(0x8da9b179b99c6aa4),UINT64_C(0x86a8830000000000),0x064f,0x2327,0x0,1,0x20},
 {UINT64_C(0x8da9b179b99c6aa4),UINT64_C(0x86a8840000000000),0x064f,0x2327,0x0,2,0x220},
 {UINT64_C(0x8da9b179b99c6aa4),UINT64_C(0x86a8830000000000),0x064f,0x2327,0x0,3,0x20},
 {UINT64_C(0x8da9b179b99c6aa4),UINT64_C(0x86a883a420305000),0x064f,0x2327,0x200,0,0x220},
 {UINT64_C(0x8da9b179b99c6aa4),UINT64_C(0x86a883a420304800),0x064f,0x2327,0x200,1,0x20},
 {UINT64_C(0x8da9b179b99c6aa4),UINT64_C(0x86a883a420305000),0x064f,0x2327,0x200,2,0x220},
 {UINT64_C(0x8da9b179b99c6aa4),UINT64_C(0x86a883a420304800),0x064f,0x2327,0x200,3,0x20},
 {UINT64_C(0x8da9b179b99c6aa4),UINT64_C(0x86a883a420304ffe),0x064f,0x2327,0x300,0,0x20},
 {UINT64_C(0x8da9b179b99c6aa4),UINT64_C(0x86a883a420304ffe),0x064f,0x2327,0x300,1,0x20},
 {UINT64_C(0x8da9b179b99c6aa4),UINT64_C(0x86a883a420304fff),0x064f,0x2327,0x300,2,0x220},
 {UINT64_C(0x8da9b179b99c6aa4),UINT64_C(0x86a883a420304ffe),0x064f,0x2327,0x300,3,0x20},
 {UINT64_C(0xc4fe7b3a99c7142e),UINT64_C(0x9ecb070000000000),0x628d,0x5146,0x0,0,0x220},
 {UINT64_C(0xc4fe7b3a99c7142e),UINT64_C(0x9ecb060000000000),0x628d,0x5146,0x0,1,0x20},
 {UINT64_C(0xc4fe7b3a99c7142e),UINT64_C(0x9ecb070000000000),0x628d,0x5146,0x0,2,0x220},
 {UINT64_C(0xc4fe7b3a99c7142e),UINT64_C(0x9ecb060000000000),0x628d,0x5146,0x0,3,0x20},
 {UINT64_C(0xc4fe7b3a99c7142e),UINT64_C(0x9ecb06f74af18800),0x628d,0x5146,0x200,0,0x20},
 {UINT64_C(0xc4fe7b3a99c7142e),UINT64_C(0x9ecb06f74af18800),0x628d,0x5146,0x200,1,0x20},
 {UINT64_C(0xc4fe7b3a99c7142e),UINT64_C(0x9ecb06f74af19000),0x628d,0x5146,0x200,2,0x220},
 {UINT64_C(0xc4fe7b3a99c7142e),UINT64_C(0x9ecb06f74af18800),0x628d,0x5146,0x200,3,0x20},
 {UINT64_C(0xc4fe7b3a99c7142e),UINT64_C(0x9ecb06f74af18bd8),0x628d,0x5146,0x300,0,0x220},
 {UINT64_C(0xc4fe7b3a99c7142e),UINT64_C(0x9ecb06f74af18bd7),0x628d,0x5146,0x300,1,0x20},
 {UINT64_C(0xc4fe7b3a99c7142e),UINT64_C(0x9ecb06f74af18bd8),0x628d,0x5146,0x300,2,0x220},
 {UINT64_C(0xc4fe7b3a99c7142e),UINT64_C(0x9ecb06f74af18bd7),0x628d,0x5146,0x300,3,0x20},
 {UINT64_C(0xf2bbe8235451e697),UINT64_C(0xf9475e0000000000),0x5b9c,0x4dcd,0x0,0,0x220},
 {UINT64_C(0xf2bbe8235451e697),UINT64_C(0xf9475d0000000000),0x5b9c,0x4dcd,0x0,1,0x20},
 {UINT64_C(0xf2bbe8235451e697),UINT64_C(0xf9475e0000000000),0x5b9c,0x4dcd,0x0,2,0x220},
 {UINT64_C(0xf2bbe8235451e697),UINT64_C(0xf9475d0000000000),0x5b9c,0x4dcd,0x0,3,0x20},
 {UINT64_C(0xf2bbe8235451e697),UINT64_C(0xf9475daee5cb8000),0x5b9c,0x4dcd,0x200,0,0x20},
 {UINT64_C(0xf2bbe8235451e697),UINT64_C(0xf9475daee5cb8000),0x5b9c,0x4dcd,0x200,1,0x20},
 {UINT64_C(0xf2bbe8235451e697),UINT64_C(0xf9475daee5cb8800),0x5b9c,0x4dcd,0x200,2,0x220},
 {UINT64_C(0xf2bbe8235451e697),UINT64_C(0xf9475daee5cb8000),0x5b9c,0x4dcd,0x200,3,0x20},
 {UINT64_C(0xf2bbe8235451e697),UINT64_C(0xf9475daee5cb8337),0x5b9c,0x4dcd,0x300,0,0x20},
 {UINT64_C(0xf2bbe8235451e697),UINT64_C(0xf9475daee5cb8337),0x5b9c,0x4dcd,0x300,1,0x20},
 {UINT64_C(0xf2bbe8235451e697),UINT64_C(0xf9475daee5cb8338),0x5b9c,0x4dcd,0x300,2,0x220},
 {UINT64_C(0xf2bbe8235451e697),UINT64_C(0xf9475daee5cb8337),0x5b9c,0x4dcd,0x300,3,0x20},
 {UINT64_C(0xbf3eb33dcb52d34c),UINT64_C(0x9c75730000000000),0x5e71,0x4f38,0x0,0,0x220},
 {UINT64_C(0xbf3eb33dcb52d34c),UINT64_C(0x9c75720000000000),0x5e71,0x4f38,0x0,1,0x20},
 {UINT64_C(0xbf3eb33dcb52d34c),UINT64_C(0x9c75730000000000),0x5e71,0x4f38,0x0,2,0x220},
 {UINT64_C(0xbf3eb33dcb52d34c),UINT64_C(0x9c75720000000000),0x5e71,0x4f38,0x0,3,0x20},
 {UINT64_C(0xbf3eb33dcb52d34c),UINT64_C(0x9c7572a81e103800),0x5e71,0x4f38,0x200,0,0x20},
 {UINT64_C(0xbf3eb33dcb52d34c),UINT64_C(0x9c7572a81e103800),0x5e71,0x4f38,0x200,1,0x20},
 {UINT64_C(0xbf3eb33dcb52d34c),UINT64_C(0x9c7572a81e104000),0x5e71,0x4f38,0x200,2,0x220},
 {UINT64_C(0xbf3eb33dcb52d34c),UINT64_C(0x9c7572a81e103800),0x5e71,0x4f38,0x200,3,0x20},
 {UINT64_C(0xbf3eb33dcb52d34c),UINT64_C(0x9c7572a81e103924),0x5e71,0x4f38,0x300,0,0x220},
 {UINT64_C(0xbf3eb33dcb52d34c),UINT64_C(0x9c7572a81e103923),0x5e71,0x4f38,0x300,1,0x20},
 {UINT64_C(0xbf3eb33dcb52d34c),UINT64_C(0x9c7572a81e103924),0x5e71,0x4f38,0x300,2,0x220},
 {UINT64_C(0xbf3eb33dcb52d34c),UINT64_C(0x9c7572a81e103923),0x5e71,0x4f38,0x300,3,0x20},
 {UINT64_C(0xd4c21b4431270ce0),UINT64_C(0xa5063f0000000000),0x1939,0x2c9c,0x0,0,0x20},
 {UINT64_C(0xd4c21b4431270ce0),UINT64_C(0xa5063f0000000000),0x1939,0x2c9c,0x0,1,0x20},
 {UINT64_C(0xd4c21b4431270ce0),UINT64_C(0xa506400000000000),0x1939,0x2c9c,0x0,2,0x220},
 {UINT64_C(0xd4c21b4431270ce0),UINT64_C(0xa5063f0000000000),0x1939,0x2c9c,0x0,3,0x20},
 {UINT64_C(0xd4c21b4431270ce0),UINT64_C(0xa5063f35954ce000),0x1939,0x2c9c,0x200,0,0x20},
 {UINT64_C(0xd4c21b4431270ce0),UINT64_C(0xa5063f35954ce000),0x1939,0x2c9c,0x200,1,0x20},
 {UINT64_C(0xd4c21b4431270ce0),UINT64_C(0xa5063f35954ce800),0x1939,0x2c9c,0x200,2,0x220},
 {UINT64_C(0xd4c21b4431270ce0),UINT64_C(0xa5063f35954ce000),0x1939,0x2c9c,0x200,3,0x20},
 {UINT64_C(0xd4c21b4431270ce0),UINT64_C(0xa5063f35954ce2a0),0x1939,0x2c9c,0x300,0,0x20},
 {UINT64_C(0xd4c21b4431270ce0),UINT64_C(0xa5063f35954ce2a0),0x1939,0x2c9c,0x300,1,0x20},
 {UINT64_C(0xd4c21b4431270ce0),UINT64_C(0xa5063f35954ce2a1),0x1939,0x2c9c,0x300,2,0x220},
 {UINT64_C(0xd4c21b4431270ce0),UINT64_C(0xa5063f35954ce2a0),0x1939,0x2c9c,0x300,3,0x20},
 {UINT64_C(0xd6002b638c38bed3),UINT64_C(0xea0f680000000000),0x3250,0x3927,0x0,0,0x20},
 {UINT64_C(0xd6002b638c38bed3),UINT64_C(0xea0f680000000000),0x3250,0x3927,0x0,1,0x20},
 {UINT64_C(0xd6002b638c38bed3),UINT64_C(0xea0f690000000000),0x3250,0x3927,0x0,2,0x220},
 {UINT64_C(0xd6002b638c38bed3),UINT64_C(0xea0f680000000000),0x3250,0x3927,0x0,3,0x20},
 {UINT64_C(0xd6002b638c38bed3),UINT64_C(0xea0f682f232be800),0x3250,0x3927,0x200,0,0x20},
 {UINT64_C(0xd6002b638c38bed3),UINT64_C(0xea0f682f232be800),0x3250,0x3927,0x200,1,0x20},
 {UINT64_C(0xd6002b638c38bed3),UINT64_C(0xea0f682f232bf000),0x3250,0x3927,0x200,2,0x220},
 {UINT64_C(0xd6002b638c38bed3),UINT64_C(0xea0f682f232be800),0x3250,0x3927,0x200,3,0x20},
 {UINT64_C(0xd6002b638c38bed3),UINT64_C(0xea0f682f232beb72),0x3250,0x3927,0x300,0,0x220},
 {UINT64_C(0xd6002b638c38bed3),UINT64_C(0xea0f682f232beb71),0x3250,0x3927,0x300,1,0x20},
 {UINT64_C(0xd6002b638c38bed3),UINT64_C(0xea0f682f232beb72),0x3250,0x3927,0x300,2,0x220},
 {UINT64_C(0xd6002b638c38bed3),UINT64_C(0xea0f682f232beb71),0x3250,0x3927,0x300,3,0x20},
 {UINT64_C(0xfa2a135e2026c730),UINT64_C(0xb2f1b90000000000),0x67a9,0x53d4,0x0,0,0x20},
 {UINT64_C(0xfa2a135e2026c730),UINT64_C(0xb2f1b90000000000),0x67a9,0x53d4,0x0,1,0x20},
 {UINT64_C(0xfa2a135e2026c730),UINT64_C(0xb2f1ba0000000000),0x67a9,0x53d4,0x0,2,0x220},
 {UINT64_C(0xfa2a135e2026c730),UINT64_C(0xb2f1b90000000000),0x67a9,0x53d4,0x0,3,0x20},
 {UINT64_C(0xfa2a135e2026c730),UINT64_C(0xb2f1b92058aa3000),0x67a9,0x53d4,0x200,0,0x20},
 {UINT64_C(0xfa2a135e2026c730),UINT64_C(0xb2f1b92058aa3000),0x67a9,0x53d4,0x200,1,0x20},
 {UINT64_C(0xfa2a135e2026c730),UINT64_C(0xb2f1b92058aa3800),0x67a9,0x53d4,0x200,2,0x220},
 {UINT64_C(0xfa2a135e2026c730),UINT64_C(0xb2f1b92058aa3000),0x67a9,0x53d4,0x200,3,0x20},
 {UINT64_C(0xfa2a135e2026c730),UINT64_C(0xb2f1b92058aa32fd),0x67a9,0x53d4,0x300,0,0x220},
 {UINT64_C(0xfa2a135e2026c730),UINT64_C(0xb2f1b92058aa32fc),0x67a9,0x53d4,0x300,1,0x20},
 {UINT64_C(0xfa2a135e2026c730),UINT64_C(0xb2f1b92058aa32fd),0x67a9,0x53d4,0x300,2,0x220},
 {UINT64_C(0xfa2a135e2026c730),UINT64_C(0xb2f1b92058aa32fc),0x67a9,0x53d4,0x300,3,0x20},
 {UINT64_C(0xc39cc50cc52a1f94),UINT64_C(0xdfc7450000000000),0x59d6,0x4cea,0x0,0,0x220},
 {UINT64_C(0xc39cc50cc52a1f94),UINT64_C(0xdfc7440000000000),0x59d6,0x4cea,0x0,1,0x20},
 {UINT64_C(0xc39cc50cc52a1f94),UINT64_C(0xdfc7450000000000),0x59d6,0x4cea,0x0,2,0x220},
 {UINT64_C(0xc39cc50cc52a1f94),UINT64_C(0xdfc7440000000000),0x59d6,0x4cea,0x0,3,0x20},
 {UINT64_C(0xc39cc50cc52a1f94),UINT64_C(0xdfc744d833f58800),0x59d6,0x4cea,0x200,0,0x20},
 {UINT64_C(0xc39cc50cc52a1f94),UINT64_C(0xdfc744d833f58800),0x59d6,0x4cea,0x200,1,0x20},
 {UINT64_C(0xc39cc50cc52a1f94),UINT64_C(0xdfc744d833f59000),0x59d6,0x4cea,0x200,2,0x220},
 {UINT64_C(0xc39cc50cc52a1f94),UINT64_C(0xdfc744d833f58800),0x59d6,0x4cea,0x200,3,0x20},
 {UINT64_C(0xc39cc50cc52a1f94),UINT64_C(0xdfc744d833f5898e),0x59d6,0x4cea,0x300,0,0x20},
 {UINT64_C(0xc39cc50cc52a1f94),UINT64_C(0xdfc744d833f5898e),0x59d6,0x4cea,0x300,1,0x20},
 {UINT64_C(0xc39cc50cc52a1f94),UINT64_C(0xdfc744d833f5898f),0x59d6,0x4cea,0x300,2,0x220},
 {UINT64_C(0xc39cc50cc52a1f94),UINT64_C(0xdfc744d833f5898e),0x59d6,0x4cea,0x300,3,0x20},
 {UINT64_C(0xd142834a7029206f),UINT64_C(0xe773fb0000000000),0x252c,0x3295,0x0,0,0x220},
 {UINT64_C(0xd142834a7029206f),UINT64_C(0xe773fa0000000000),0x252c,0x3295,0x0,1,0x20},
 {UINT64_C(0xd142834a7029206f),UINT64_C(0xe773fb0000000000),0x252c,0x3295,0x0,2,0x220},
 {UINT64_C(0xd142834a7029206f),UINT64_C(0xe773fa0000000000),0x252c,0x3295,0x0,3,0x20},
 {UINT64_C(0xd142834a7029206f),UINT64_C(0xe773fadf5aad4000),0x252c,0x3295,0x200,0,0x220},
 {UINT64_C(0xd142834a7029206f),UINT64_C(0xe773fadf5aad3800),0x252c,0x3295,0x200,1,0x20},
 {UINT64_C(0xd142834a7029206f),UINT64_C(0xe773fadf5aad4000),0x252c,0x3295,0x200,2,0x220},
 {UINT64_C(0xd142834a7029206f),UINT64_C(0xe773fadf5aad3800),0x252c,0x3295,0x200,3,0x20},
 {UINT64_C(0xd142834a7029206f),UINT64_C(0xe773fadf5aad3f9e),0x252c,0x3295,0x300,0,0x220},
 {UINT64_C(0xd142834a7029206f),UINT64_C(0xe773fadf5aad3f9d),0x252c,0x3295,0x300,1,0x20},
 {UINT64_C(0xd142834a7029206f),UINT64_C(0xe773fadf5aad3f9e),0x252c,0x3295,0x300,2,0x220},
 {UINT64_C(0xd142834a7029206f),UINT64_C(0xe773fadf5aad3f9d),0x252c,0x3295,0x300,3,0x20},
 {UINT64_C(0xc1390d86cce1276c),UINT64_C(0xde684c0000000000),0x3df4,0x3ef9,0x0,0,0x220},
 {UINT64_C(0xc1390d86cce1276c),UINT64_C(0xde684b0000000000),0x3df4,0x3ef9,0x0,1,0x20},
 {UINT64_C(0xc1390d86cce1276c),UINT64_C(0xde684c0000000000),0x3df4,0x3ef9,0x0,2,0x220},
 {UINT64_C(0xc1390d86cce1276c),UINT64_C(0xde684b0000000000),0x3df4,0x3ef9,0x0,3,0x20},
 {UINT64_C(0xc1390d86cce1276c),UINT64_C(0xde684b8d731da800),0x3df4,0x3ef9,0x200,0,0x20},
 {UINT64_C(0xc1390d86cce1276c),UINT64_C(0xde684b8d731da800),0x3df4,0x3ef9,0x200,1,0x20},
 {UINT64_C(0xc1390d86cce1276c),UINT64_C(0xde684b8d731db000),0x3df4,0x3ef9,0x200,2,0x220},
 {UINT64_C(0xc1390d86cce1276c),UINT64_C(0xde684b8d731da800),0x3df4,0x3ef9,0x200,3,0x20},
 {UINT64_C(0xc1390d86cce1276c),UINT64_C(0xde684b8d731da833),0x3df4,0x3ef9,0x300,0,0x220},
 {UINT64_C(0xc1390d86cce1276c),UINT64_C(0xde684b8d731da832),0x3df4,0x3ef9,0x300,1,0x20},
 {UINT64_C(0xc1390d86cce1276c),UINT64_C(0xde684b8d731da833),0x3df4,0x3ef9,0x300,2,0x220},
 {UINT64_C(0xc1390d86cce1276c),UINT64_C(0xde684b8d731da832),0x3df4,0x3ef9,0x300,3,0x20},
 {UINT64_C(0x9458c9591a399497),UINT64_C(0xc2e0580000000000),0x68f6,0x547a,0x0,0,0x220},
 {UINT64_C(0x9458c9591a399497),UINT64_C(0xc2e0570000000000),0x68f6,0x547a,0x0,1,0x20},
 {UINT64_C(0x9458c9591a399497),UINT64_C(0xc2e0580000000000),0x68f6,0x547a,0x0,2,0x220},
 {UINT64_C(0x9458c9591a399497),UINT64_C(0xc2e0570000000000),0x68f6,0x547a,0x0,3,0x20},
 {UINT64_C(0x9458c9591a399497),UINT64_C(0xc2e057963d955800),0x68f6,0x547a,0x200,0,0x220},
 {UINT64_C(0x9458c9591a399497),UINT64_C(0xc2e057963d955000),0x68f6,0x547a,0x200,1,0x20},
 {UINT64_C(0x9458c9591a399497),UINT64_C(0xc2e057963d955800),0x68f6,0x547a,0x200,2,0x220},
 {UINT64_C(0x9458c9591a399497),UINT64_C(0xc2e057963d955000),0x68f6,0x547a,0x200,3,0x20},
 {UINT64_C(0x9458c9591a399497),UINT64_C(0xc2e057963d9557e4),0x68f6,0x547a,0x300,0,0x220},
 {UINT64_C(0x9458c9591a399497),UINT64_C(0xc2e057963d9557e3),0x68f6,0x547a,0x300,1,0x20},
 {UINT64_C(0x9458c9591a399497),UINT64_C(0xc2e057963d9557e4),0x68f6,0x547a,0x300,2,0x220},
 {UINT64_C(0x9458c9591a399497),UINT64_C(0xc2e057963d9557e3),0x68f6,0x547a,0x300,3,0x20},
 {UINT64_C(0xab6423e91ac10b69),UINT64_C(0x941d740000000000),0x4641,0x4320,0x0,0,0x20},
 {UINT64_C(0xab6423e91ac10b69),UINT64_C(0x941d740000000000),0x4641,0x4320,0x0,1,0x20},
 {UINT64_C(0xab6423e91ac10b69),UINT64_C(0x941d750000000000),0x4641,0x4320,0x0,2,0x220},
 {UINT64_C(0xab6423e91ac10b69),UINT64_C(0x941d740000000000),0x4641,0x4320,0x0,3,0x20},
 {UINT64_C(0xab6423e91ac10b69),UINT64_C(0x941d7461b5e85000),0x4641,0x4320,0x200,0,0x20},
 {UINT64_C(0xab6423e91ac10b69),UINT64_C(0x941d7461b5e85000),0x4641,0x4320,0x200,1,0x20},
 {UINT64_C(0xab6423e91ac10b69),UINT64_C(0x941d7461b5e85800),0x4641,0x4320,0x200,2,0x220},
 {UINT64_C(0xab6423e91ac10b69),UINT64_C(0x941d7461b5e85000),0x4641,0x4320,0x200,3,0x20},
 {UINT64_C(0xab6423e91ac10b69),UINT64_C(0x941d7461b5e8505e),0x4641,0x4320,0x300,0,0x220},
 {UINT64_C(0xab6423e91ac10b69),UINT64_C(0x941d7461b5e8505d),0x4641,0x4320,0x300,1,0x20},
 {UINT64_C(0xab6423e91ac10b69),UINT64_C(0x941d7461b5e8505e),0x4641,0x4320,0x300,2,0x220},
 {UINT64_C(0xab6423e91ac10b69),UINT64_C(0x941d7461b5e8505d),0x4641,0x4320,0x300,3,0x20},
 {UINT64_C(0xefd0e73ef9c704eb),UINT64_C(0xf7c6a20000000000),0x4506,0x4282,0x0,0,0x20},
 {UINT64_C(0xefd0e73ef9c704eb),UINT64_C(0xf7c6a20000000000),0x4506,0x4282,0x0,1,0x20},
 {UINT64_C(0xefd0e73ef9c704eb),UINT64_C(0xf7c6a30000000000),0x4506,0x4282,0x0,2,0x220},
 {UINT64_C(0xefd0e73ef9c704eb),UINT64_C(0xf7c6a20000000000),0x4506,0x4282,0x0,3,0x20},
 {UINT64_C(0xefd0e73ef9c704eb),UINT64_C(0xf7c6a2442e5bb000),0x4506,0x4282,0x200,0,0x20},
 {UINT64_C(0xefd0e73ef9c704eb),UINT64_C(0xf7c6a2442e5bb000),0x4506,0x4282,0x200,1,0x20},
 {UINT64_C(0xefd0e73ef9c704eb),UINT64_C(0xf7c6a2442e5bb800),0x4506,0x4282,0x200,2,0x220},
 {UINT64_C(0xefd0e73ef9c704eb),UINT64_C(0xf7c6a2442e5bb000),0x4506,0x4282,0x200,3,0x20},
 {UINT64_C(0xefd0e73ef9c704eb),UINT64_C(0xf7c6a2442e5bb265),0x4506,0x4282,0x300,0,0x220},
 {UINT64_C(0xefd0e73ef9c704eb),UINT64_C(0xf7c6a2442e5bb264),0x4506,0x4282,0x300,1,0x20},
 {UINT64_C(0xefd0e73ef9c704eb),UINT64_C(0xf7c6a2442e5bb265),0x4506,0x4282,0x300,2,0x220},
 {UINT64_C(0xefd0e73ef9c704eb),UINT64_C(0xf7c6a2442e5bb264),0x4506,0x4282,0x300,3,0x20},
 {UINT64_C(0x855627e660121cc5),UINT64_C(0xb8c11b0000000000),0x300e,0x3806,0x0,0,0x220},
 {UINT64_C(0x855627e660121cc5),UINT64_C(0xb8c11a0000000000),0x300e,0x3806,0x0,1,0x20},
 {UINT64_C(0x855627e660121cc5),UINT64_C(0xb8c11b0000000000),0x300e,0x3806,0x0,2,0x220},
 {UINT64_C(0x855627e660121cc5),UINT64_C(0xb8c11a0000000000),0x300e,0x3806,0x0,3,0x20},
 {UINT64_C(0x855627e660121cc5),UINT64_C(0xb8c11a99ea983000),0x300e,0x3806,0x200,0,0x220},
 {UINT64_C(0x855627e660121cc5),UINT64_C(0xb8c11a99ea982800),0x300e,0x3806,0x200,1,0x20},
 {UINT64_C(0x855627e660121cc5),UINT64_C(0xb8c11a99ea983000),0x300e,0x3806,0x200,2,0x220},
 {UINT64_C(0x855627e660121cc5),UINT64_C(0xb8c11a99ea982800),0x300e,0x3806,0x200,3,0x20},
 {UINT64_C(0x855627e660121cc5),UINT64_C(0xb8c11a99ea982eb0),0x300e,0x3806,0x300,0,0x20},
 {UINT64_C(0x855627e660121cc5),UINT64_C(0xb8c11a99ea982eb0),0x300e,0x3806,0x300,1,0x20},
 {UINT64_C(0x855627e660121cc5),UINT64_C(0xb8c11a99ea982eb1),0x300e,0x3806,0x300,2,0x220},
 {UINT64_C(0x855627e660121cc5),UINT64_C(0xb8c11a99ea982eb0),0x300e,0x3806,0x300,3,0x20},
 {UINT64_C(0xc1e28ae84f167acb),UINT64_C(0x9d88f50000000000),0x6699,0x534c,0x0,0,0x220},
 {UINT64_C(0xc1e28ae84f167acb),UINT64_C(0x9d88f40000000000),0x6699,0x534c,0x0,1,0x20},
 {UINT64_C(0xc1e28ae84f167acb),UINT64_C(0x9d88f50000000000),0x6699,0x534c,0x0,2,0x220},
 {UINT64_C(0xc1e28ae84f167acb),UINT64_C(0x9d88f40000000000),0x6699,0x534c,0x0,3,0x20},
 {UINT64_C(0xc1e28ae84f167acb),UINT64_C(0x9d88f4bc803d4000),0x6699,0x534c,0x200,0,0x220},
 {UINT64_C(0xc1e28ae84f167acb),UINT64_C(0x9d88f4bc803d3800),0x6699,0x534c,0x200,1,0x20},
 {UINT64_C(0xc1e28ae84f167acb),UINT64_C(0x9d88f4bc803d4000),0x6699,0x534c,0x200,2,0x220},
 {UINT64_C(0xc1e28ae84f167acb),UINT64_C(0x9d88f4bc803d3800),0x6699,0x534c,0x200,3,0x20},
 {UINT64_C(0xc1e28ae84f167acb),UINT64_C(0x9d88f4bc803d3d35),0x6699,0x534c,0x300,0,0x20},
 {UINT64_C(0xc1e28ae84f167acb),UINT64_C(0x9d88f4bc803d3d35),0x6699,0x534c,0x300,1,0x20},
 {UINT64_C(0xc1e28ae84f167acb),UINT64_C(0x9d88f4bc803d3d36),0x6699,0x534c,0x300,2,0x220},
 {UINT64_C(0xc1e28ae84f167acb),UINT64_C(0x9d88f4bc803d3d35),0x6699,0x534c,0x300,3,0x20},
 {UINT64_C(0xcfde1d58ab3f80e8),UINT64_C(0xe6ae8e0000000000),0x2afc,0x357d,0x0,0,0x220},
 {UINT64_C(0xcfde1d58ab3f80e8),UINT64_C(0xe6ae8d0000000000),0x2afc,0x357d,0x0,1,0x20},
 {UINT64_C(0xcfde1d58ab3f80e8),UINT64_C(0xe6ae8e0000000000),0x2afc,0x357d,0x0,2,0x220},
 {UINT64_C(0xcfde1d58ab3f80e8),UINT64_C(0xe6ae8d0000000000),0x2afc,0x357d,0x0,3,0x20},
 {UINT64_C(0xcfde1d58ab3f80e8),UINT64_C(0xe6ae8d89fe8e9000),0x2afc,0x357d,0x200,0,0x220},
 {UINT64_C(0xcfde1d58ab3f80e8),UINT64_C(0xe6ae8d89fe8e8800),0x2afc,0x357d,0x200,1,0x20},
 {UINT64_C(0xcfde1d58ab3f80e8),UINT64_C(0xe6ae8d89fe8e9000),0x2afc,0x357d,0x200,2,0x220},
 {UINT64_C(0xcfde1d58ab3f80e8),UINT64_C(0xe6ae8d89fe8e8800),0x2afc,0x357d,0x200,3,0x20},
 {UINT64_C(0xcfde1d58ab3f80e8),UINT64_C(0xe6ae8d89fe8e8fc2),0x2afc,0x357d,0x300,0,0x20},
 {UINT64_C(0xcfde1d58ab3f80e8),UINT64_C(0xe6ae8d89fe8e8fc2),0x2afc,0x357d,0x300,1,0x20},
 {UINT64_C(0xcfde1d58ab3f80e8),UINT64_C(0xe6ae8d89fe8e8fc3),0x2afc,0x357d,0x300,2,0x220},
 {UINT64_C(0xcfde1d58ab3f80e8),UINT64_C(0xe6ae8d89fe8e8fc2),0x2afc,0x357d,0x300,3,0x20},
 {UINT64_C(0xd5e0df5e1c08a851),UINT64_C(0xe9fe4a0000000000),0x327a,0x393c,0x0,0,0x20},
 {UINT64_C(0xd5e0df5e1c08a851),UINT64_C(0xe9fe4a0000000000),0x327a,0x393c,0x0,1,0x20},
 {UINT64_C(0xd5e0df5e1c08a851),UINT64_C(0xe9fe4b0000000000),0x327a,0x393c,0x0,2,0x220},
 {UINT64_C(0xd5e0df5e1c08a851),UINT64_C(0xe9fe4a0000000000),0x327a,0x393c,0x0,3,0x20},
 {UINT64_C(0xd5e0df5e1c08a851),UINT64_C(0xe9fe4a0a7e3d9000),0x327a,0x393c,0x200,0,0x20},
 {UINT64_C(0xd5e0df5e1c08a851),UINT64_C(0xe9fe4a0a7e3d9000),0x327a,0x393c,0x200,1,0x20},
 {UINT64_C(0xd5e0df5e1c08a851),UINT64_C(0xe9fe4a0a7e3d9800),0x327a,0x393c,0x200,2,0x220},
 {UINT64_C(0xd5e0df5e1c08a851),UINT64_C(0xe9fe4a0a7e3d9000),0x327a,0x393c,0x200,3,0x20},
 {UINT64_C(0xd5e0df5e1c08a851),UINT64_C(0xe9fe4a0a7e3d9286),0x327a,0x393c,0x300,0,0x20},
 {UINT64_C(0xd5e0df5e1c08a851),UINT64_C(0xe9fe4a0a7e3d9286),0x327a,0x393c,0x300,1,0x20},
 {UINT64_C(0xd5e0df5e1c08a851),UINT64_C(0xe9fe4a0a7e3d9287),0x327a,0x393c,0x300,2,0x220},
 {UINT64_C(0xd5e0df5e1c08a851),UINT64_C(0xe9fe4a0a7e3d9286),0x327a,0x393c,0x300,3,0x20},
 {UINT64_C(0xa7454689f0726e23),UINT64_C(0x9252e10000000000),0x5e71,0x4f38,0x0,0,0x220},
 {UINT64_C(0xa7454689f0726e23),UINT64_C(0x9252e00000000000),0x5e71,0x4f38,0x0,1,0x20},
 {UINT64_C(0xa7454689f0726e23),UINT64_C(0x9252e10000000000),0x5e71,0x4f38,0x0,2,0x220},
 {UINT64_C(0xa7454689f0726e23),UINT64_C(0x9252e00000000000),0x5e71,0x4f38,0x0,3,0x20},
 {UINT64_C(0xa7454689f0726e23),UINT64_C(0x9252e0d28faf5800),0x5e71,0x4f38,0x200,0,0x20},
 {UINT64_C(0xa7454689f0726e23),UINT64_C(0x9252e0d28faf5800),0x5e71,0x4f38,0x200,1,0x20},
 {UINT64_C(0xa7454689f0726e23),UINT64_C(0x9252e0d28faf6000),0x5e71,0x4f38,0x200,2,0x220},
 {UINT64_C(0xa7454689f0726e23),UINT64_C(0x9252e0d28faf5800),0x5e71,0x4f38,0x200,3,0x20},
 {UINT64_C(0xa7454689f0726e23),UINT64_C(0x9252e0d28faf5983),0x5e71,0x4f38,0x300,0,0x220},
 {UINT64_C(0xa7454689f0726e23),UINT64_C(0x9252e0d28faf5982),0x5e71,0x4f38,0x300,1,0x20},
 {UINT64_C(0xa7454689f0726e23),UINT64_C(0x9252e0d28faf5983),0x5e71,0x4f38,0x300,2,0x220},
 {UINT64_C(0xa7454689f0726e23),UINT64_C(0x9252e0d28faf5982),0x5e71,0x4f38,0x300,3,0x20},
 };
 for(unsigned m=0;m<2;++m) for(unsigned k=0;k<sizeof(v)/sizeof(v[0]);++k) {
  e87_t n;e87_init(&n,m);n.tags=0xfffc;n.control=(n.control&~0xf00)|v[k].pc|(v[k].rc<<10);
  n.reg[0]=(e87_real80_t){v[k].sig,v[k].exp};
  CHECK(e87_numeric(&n,0xd9,0xfa,0,0));
  CHECK(n.reg[0].significand==v[k].out && n.reg[0].sign_exponent==v[k].oe);
  CHECK((n.status&0x23f)==v[k].flags);
 }
 for(unsigned m=0;m<2;++m) for(int k=1;k<50;++k) {
  e87_t n;e87_init(&n,m);n.tags=0xfffc;n.reg[0]=whole(k*k);
  CHECK(e87_numeric(&n,0xd9,0xfa,0,0));same_value(n.reg[0],whole(k));CHECK(!(n.status&0x3f));
 }
}

static void arithmetic_exception_checks(void)
{
    const e87_real80_t one={UINT64_C(0x8000000000000000),0x3fff};
    const e87_real80_t special[]={ {0,0}, {UINT64_C(0x8000000000000000),0x7fff},
      {UINT64_C(0xc123000000000000),0xffff}, {UINT64_C(0x4000000000000000),0},
      {UINT64_C(0x4000000000000000),0x3fff} };
    for(unsigned m=0;m<2;++m) for(unsigned mask=0;mask<2;++mask) for(unsigned k=0;k<5;++k) {
        e87_t n;e87_init(&n,m);n.tags=0xfff0;n.reg[0]=one;n.reg[1]=special[k];
        n.tags|=(k==0?1:k<4?2:0)<<2;
        unsigned exception=k==0?4:k==1?0:1;
        if(exception) n.control=(n.control&~exception)|(mask?exception:0);
        CHECK(e87_numeric(&n,0xd8,0xf1,0,0));
        CHECK((n.status&0x3f)==(exception|(k==3?2:0)));
        if(exception && !mask) {same_value(n.reg[0],one);CHECK(n.status&0x80);}
        else if(k==1) {CHECK(!n.reg[0].significand && !n.reg[0].sign_exponent);}
        else if(k==2) same_value(n.reg[0],special[k]);
        else CHECK(n.reg[0].sign_exponent==(k==0?0x7fff:0xffff));
    }
    for(unsigned m=0;m<2;++m) for(unsigned mask=0;mask<2;++mask) for(unsigned k=0;k<4;++k) {
        e87_t n;e87_init(&n,m);n.tags=0xfffc;
        n.reg[0]=k==0?whole(-1):k==1?special[3]:k==2?special[4]:special[1];
        if(k==1 || k==3)n.tags|=2;
        n.control=(n.control&~1u)|mask;e87_real80_t before=n.reg[0];
        CHECK(e87_numeric(&n,0xd9,0xfa,0,0));CHECK((n.status&0x3f)==(k==1?3:1));
        if(!mask)same_value(n.reg[0],before);else CHECK(n.reg[0].sign_exponent==0xffff);
    }
    for(unsigned m=0;m<2;++m) for(unsigned k=0;k<2;++k) {
        e87_t n;e87_init(&n,m);n.tags=0xfff0;n.control&=~(k?0x10u:8u);
        n.reg[0]=(e87_real80_t){one.significand,k?1:0x7ffe};n.reg[1]=whole(k?0:4);
        if(k)n.reg[1]=(e87_real80_t){one.significand,0x3ffd};
        CHECK(e87_numeric(&n,0xd8,0xc9,0,0));
        CHECK((n.status&0xbf)==(k?0x90:0x88));CHECK(n.reg[0].significand==one.significand);
        CHECK(n.reg[0].sign_exponent==(k?24575:8192));
    }
}
static void multiplication_division(void)
{
    const unsigned ops[]={0xd8,0xdc,0xde}, groups[]={0xc8,0xf0,0xf8}, pcs[]={0,0x200,0x300};
    for(unsigned model=0;model<2;++model) for(unsigned form=0;form<3;++form)
    for(unsigned kind=0;kind<3;++kind) for(unsigned sign=0;sign<4;++sign)
    for(unsigned t=0;t<8;++t) for(unsigned pc=0;pc<3;++pc) for(unsigned rc=0;rc<4;++rc) {
        e87_t n;e87_init(&n,model);unsigned i=(t+1)&7,d=form?i:t;
        n.status=t<<11;n.tags=0xffff&~((3u<<(2*t))|(3u<<(2*i)));
        n.control=(n.control&~0xf00)|pcs[pc]|(rc<<10);
        n.reg[t]=(e87_real80_t){UINT64_C(0x8000000000000000),0x4001|((sign&1)<<15)};
        n.reg[i]=(e87_real80_t){UINT64_C(0x8000000000000000),0x4000|((sign>>1)<<15)};
        CHECK(e87_numeric(&n,ops[form],groups[kind]|1,0,0));
        CHECK(n.reg[d].significand==UINT64_C(0x8000000000000000));
        CHECK(n.reg[d].sign_exponent==((kind==0?0x4002:kind==1?0x4000:0x3ffe)|(((sign^(sign>>1))&1)<<15)));
        CHECK(e87_top(&n)==((t+(form==2))&7) && !(n.status&0xbf));
        CHECK(((n.tags>>(2*d))&3)==0);
    }
    const unsigned widths[]={24,53,64};
    for(unsigned model=0;model<2;++model) for(unsigned p=0;p<3;++p)
    for(unsigned rc=0;rc<4;++rc) for(unsigned neg=0;neg<2;++neg) for(unsigned mask=0;mask<2;++mask) {
        e87_t n;e87_init(&n,model);n.tags=0xfff0;
        n.control=(n.control&~0xf20u)|pcs[p]|(rc<<10)|(mask?0x20:0);
        n.reg[0]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff|(neg<<15)};
        n.reg[1]=(e87_real80_t){UINT64_C(0xc000000000000000),0x4000};
        unsigned __int128 numerator=(unsigned __int128)1<<(widths[p]+1);
        uint64_t floor=numerator/3;unsigned rem=numerator%3;
        unsigned up=rc==0?rem==2:rc==1?neg:rc==2?!neg:0;
        CHECK(e87_numeric(&n,0xd8,0xf1,0,0));
        CHECK(n.reg[0].significand==((floor+up)<<(64-widths[p])));
        CHECK(n.reg[0].sign_exponent==(0x3ffd|(neg<<15)));
        CHECK((n.status&0xa0)==(mask?0x20:0xa0) && !!(n.status&0x200)==up);
    }
    for(unsigned model=0;model<2;++model) for(unsigned k=0;k<5;++k) {
        e87_t n;e87_init(&n,model);n.tags=0xfff0;
        n.reg[0]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff};n.reg[1]=n.reg[0];
        if(k==0) {n.reg[1]=(e87_real80_t){0,0};n.tags=0xfff4;}
        if(k==1) n.reg[0]=n.reg[1]=(e87_real80_t){UINT64_C(0x8000000000000000),0x7ffe};
        if(k==2) n.reg[0]=n.reg[1]=(e87_real80_t){UINT64_C(0x8000000000000000),1};
        if(k==3) {n.reg[1]=(e87_real80_t){1,0};n.tags=0xfff8;}
        if(k==4) {n.reg[1]=(e87_real80_t){UINT64_C(0xc123456789abcdef),0x7fff};n.tags=0xfff8;}
        CHECK(e87_numeric(&n,0xd8,k==0?0xf1:0xc9,0,0));
        const unsigned flags[]={4,0x28,0x30,2,1};
        CHECK((n.status&0x3f)==flags[k]);
        if(k<2) CHECK(n.reg[0].sign_exponent==0x7fff && n.reg[0].significand==UINT64_C(0x8000000000000000));
        if(k==2) CHECK(!n.reg[0].significand && !n.reg[0].sign_exponent);
        if(k==3) CHECK(n.reg[0].significand==1 && n.reg[0].sign_exponent==1);
        if(k==4) CHECK(n.reg[0].significand==UINT64_C(0xc123456789abcdef) && n.reg[0].sign_exponent==0x7fff);
    }
}
static void product_rounding_and_zero(void)
{
    const unsigned widths[]={24,53,64},pcs[]={0,0x200,0x300};
    for(unsigned model=0;model<2;++model) for(unsigned p=0;p<3;++p)
    for(unsigned rc=0;rc<4;++rc) for(unsigned neg=0;neg<2;++neg) {
        e87_t n;e87_init(&n,model);n.tags=0xfff0;n.control=(n.control&~0xf00)|pcs[p]|(rc<<10);
        n.reg[0]=(e87_real80_t){UINT64_C(0x8000000000000000)+(UINT64_C(1)<<(64-widths[p])),0x3fff|(neg<<15)};
        n.reg[1]=(e87_real80_t){UINT64_C(0xc000000000000000),0x3fff};
        unsigned up=rc==0 || rc==(neg?1:2);
        CHECK(e87_numeric(&n,0xd8,0xc9,0,0));
        CHECK(n.reg[0].significand==UINT64_C(0xc000000000000000)+((uint64_t)(up?2:1)<<(64-widths[p])));
        CHECK(n.reg[0].sign_exponent==(0x3fff|(neg<<15)) && (n.status&0x20));
        CHECK(!!(n.status&0x200)==up);
    }
    for(unsigned model=0;model<2;++model) for(unsigned neg=0;neg<2;++neg) {
        e87_t n;e87_init(&n,model);n.tags=0xfff0;
        n.reg[0]=n.reg[1]=(e87_real80_t){UINT64_C(0xc000000000000000),0x3fff};
        CHECK(e87_numeric(&n,0xd8,0xc9,0,0));
        CHECK(n.reg[0].significand==UINT64_C(0x9000000000000000) && n.reg[0].sign_exponent==0x4000);
        for(unsigned op=0;op<2;++op) {
            n.reg[0]=(e87_real80_t){0,neg<<15};n.tags=0xfff1;
            CHECK(e87_numeric(&n,0xd8,op?0xf1:0xc9,0,0));
            CHECK(n.reg[0].significand==0 && n.reg[0].sign_exponent==(neg<<15) && e87_tag(&n,0)==1);
        }
    }
}
static void subtraction_checks(void)
{
    const unsigned ops[]={0xd8,0xdc,0xde}, pcs[]={0,0x200,0x300};
    for(unsigned model=0;model<2;++model) for(unsigned form=0;form<3;++form)
    for(unsigned reverse=0;reverse<2;++reverse) for(unsigned sign=0;sign<2;++sign)
    for(unsigned initial=0;initial<8;++initial) for(unsigned pc=0;pc<3;++pc)
    for(unsigned rc=0;rc<4;++rc) {
        e87_t n; e87_init(&n,model);n.status=initial<<11;
        n.control=(n.control&~0xf00)|pcs[pc]|(rc<<10);
        unsigned a=initial,b=(a+1)&7,dest=form?b:a;
        n.tags=0xffff&~((3u<<(2*a))|(3u<<(2*b)));
        n.reg[a]=(e87_real80_t){UINT64_C(0xc000000000000000),0x4000|(sign<<15)}; /* +/-3 */
        n.reg[b]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff|(sign<<15)}; /* +/-1 */
        CHECK(e87_numeric(&n,ops[form],(reverse?0xe8:0xe0)|1,0,0));
        CHECK(n.reg[dest].significand==UINT64_C(0x8000000000000000));
        CHECK(n.reg[dest].sign_exponent==(0x4000|((sign^reverse)<<15)));
        CHECK(e87_top(&n)==((initial+(form==2))&7));
        CHECK(!(n.status&0xbf));
        CHECK(((n.tags>>(2*dest))&3)==0);
        if(form==2) CHECK(((n.tags>>(2*a))&3)==3);
    }
    for(unsigned model=0;model<2;++model) for(unsigned rev=0;rev<2;++rev)
    for(unsigned rc=0;rc<4;++rc) {
        e87_t n;e87_init(&n,model);n.tags=0xfffc;n.control=(n.control&~0xc00)|(rc<<10);
        n.reg[0]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff};
        CHECK(e87_numeric(&n,0xd8,rev?0xe8:0xe0,0,0));
        CHECK(n.reg[0].significand==0 && n.reg[0].sign_exponent==(rc==1?0x8000:0));
        CHECK(e87_tag(&n,0)==1);
    }
    for(unsigned model=0;model<2;++model) for(unsigned rev=0;rev<2;++rev)
    for(unsigned mask=0;mask<2;++mask) {
        e87_t n,before;e87_init(&n,model);n.control=(n.control&~1u)|mask|0x1000;n.tags=0xfffa;
        n.reg[0]=n.reg[1]=(e87_real80_t){UINT64_C(0x8000000000000000),0x7fff};before=n;
        CHECK(e87_numeric(&n,0xd8,rev?0xe9:0xe1,0,0));
        CHECK((n.status&0x81)==(mask?1:0x81));
        if(!mask) CHECK(!memcmp(n.reg,before.reg,sizeof n.reg));
        else CHECK(n.reg[0].sign_exponent==0xffff && n.reg[0].significand==UINT64_C(0xc000000000000000));
        e87_fninit(&n);n.tags=0xfff2;
        n.reg[0]=(e87_real80_t){UINT64_C(0xc123456789abcdef),0xffff};
        n.reg[1]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff};before=n;
        CHECK(e87_numeric(&n,0xd8,rev?0xe9:0xe1,0,0));
        CHECK(n.reg[0].sign_exponent==before.reg[0].sign_exponent && n.reg[0].significand==before.reg[0].significand);
    }
}
static void subtraction_rounding(void)
{
    const unsigned widths[]={24,53,64}, pcs[]={0,0x200,0x300};
    for(unsigned model=0;model<2;++model) for(unsigned p=0;p<3;++p)
    for(unsigned rc=0;rc<4;++rc) for(unsigned reverse=0;reverse<2;++reverse)
    for(unsigned masked=0;masked<2;++masked) {
        e87_t n;e87_init(&n,model);n.tags=0xfff0;
        n.control=(n.control&~0xf20u)|pcs[p]|(rc<<10)|(masked?0x20:0);
        n.reg[0]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff};
        n.reg[1]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff-widths[p]-1};
        CHECK(e87_numeric(&n,0xd8,reverse?0xe9:0xe1,0,0));
        unsigned up=rc==0 || rc==(reverse?1:2);
        CHECK(n.reg[0].significand==(up?UINT64_C(0x8000000000000000):UINT64_MAX<<(64-widths[p])));
        CHECK(n.reg[0].sign_exponent==((up?0x3fff:0x3ffe)|(reverse<<15)));
        CHECK((n.status&0xa0)==(masked?0x20:0xa0));
        CHECK(!!(n.status&0x200)==up);
    }
}
static void precision_checks(void)
{
    const unsigned widths[3]={24,53,64}, pc[3]={0,2,3}, ops[3]={0xd8,0xdc,0xde};
    unsigned model, mask, k, rc, sign, form;
    for(model=0;model<2;++model) for(mask=0;mask<2;++mask)
    for(k=0;k<3;++k) for(rc=0;rc<4;++rc) for(sign=0;sign<2;++sign)
    for(form=0;form<3;++form) {
        e87_t n, before; e87_init(&n,model);
        n.control=(n.control & ~0xf20u) | (pc[k]<<8) | (rc<<10) | (mask?0x20:0);
        n.tags=0xfff0;n.status=0x4500; /* C3/C2/C0 must survive */
        n.reg[0]=(e87_real80_t){UINT64_C(0x8000000000000000),(sign<<15)|0x3fff};
        n.reg[1]=(e87_real80_t){UINT64_C(0x8000000000000000),(sign<<15)|(0x3fff-widths[k]-1)};
        before=n;
        int ok=e87_numeric(&n,ops[form],0xc1,0,0);
        CHECK(ok);
        CHECK(!(n.status&0x8000)); /* Initial 8087 IEM is set. */
        CHECK(e87_error_signal(&n)==(model==E87_80287 && !mask));
        unsigned increment=(rc==(sign?1:2));
        unsigned dest=form?1:0;
        uint64_t want=UINT64_C(0x8000000000000000) +
            (increment ? UINT64_C(1)<<(64-widths[k]) : 0);
        CHECK(n.reg[dest].significand==want);
        CHECK(n.reg[dest].sign_exponent==((sign<<15)|0x3fff));
        CHECK((n.status&0xa0)==(mask?0x20:0xa0));
        CHECK(!!(n.status&0x200)==increment);
        CHECK((n.status&0x4500)==0x4500);
        CHECK(e87_top(&n)==(form==2?1:0));
        CHECK(n.tags==(form==2?0xfff3:0xfff0));
        CHECK(e87_pending_error(&n)==!mask);
        if(!mask) {
            if(model==E87_8087) e87_control(&n,0xdb,0xe0,0);
            rejected(&n,0xd8,0xc0,0);
        }
        before=n;
        CHECK(e87_control(&n,0xdb,0xe2,0)); /* no-wait clear */
        CHECK(!(n.status&0x80bf) && !e87_pending_error(&n));
        CHECK((n.status&0x7f00)==(before.status&0x7f00));
        CHECK(n.tags==before.tags && n.control==before.control);
        CHECK(!memcmp(n.reg,before.reg,sizeof n.reg));
    }
    /* Exact half-ULP ties round toward an even retained significand. */
    for(k=0;k<3;++k) for(unsigned odd=0;odd<2;++odd) {
        e87_t n;e87_init(&n,E87_80287);
        uint64_t ulp=UINT64_C(1)<<(64-widths[k]);
        n.control=(n.control&~0x300u)|(pc[k]<<8);n.tags=0xfff0;
        n.reg[0]=(e87_real80_t){UINT64_C(0x8000000000000000)+odd*ulp,0x3fff};
        n.reg[1]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff-widths[k]};
        CHECK(e87_numeric(&n,0xd8,0xc1,0,0));
        CHECK(n.reg[0].significand==UINT64_C(0x8000000000000000)+2*odd*ulp);
        CHECK(!!(n.status&0x200)==odd && (n.status&0x20));
        /* An exact subsequent operation clears C1, but keeps sticky PE. */
        n.reg[1]=(e87_real80_t){0,0};n.tags=0xfff4;
        CHECK(e87_numeric(&n,0xd8,0xc1,0,0));
        CHECK((n.status&0x220)==0x20);
    }
}
static void stack_checks(void)
{
    const unsigned ops[5]={0xdb,0xdb,0xd8,0xdc,0xde};
    const unsigned mr[5]={0x2e,0x3e,0xc1,0xc1,0xc1};
    unsigned char one[10]={0,0,0,0,0,0,0,0x80,0xff,0x3f};
    unsigned char out[12], expected[12];
    for(unsigned model=0;model<2;++model) for(unsigned mask=0;mask<2;++mask)
    for(unsigned iem=0;iem<2;++iem) for(unsigned top=0;top<8;++top) for(unsigned form=0;form<5;++form) {
        e87_t n,before;e87_init(&n,model);
        n.status=(top<<11)|0x4700|0x20;
        n.control=(n.control&~1u)|mask;
        if(model==E87_8087) n.control=(n.control&~0x80u)|(iem<<7);
        n.tags=form==0?0:0xffff;
        for(unsigned i=0;i<8;++i) n.reg[i]=(e87_real80_t){UINT64_C(0x8000000000000000)+i,0x3fff};
        before=n;memset(out,0xa5,sizeof out);memcpy(expected,out,sizeof out);
        int result=e87_numeric(&n,ops[form],mr[form],one,out+1);
        CHECK(result==(!mask && form==1?2:1));
        CHECK(!!(n.status&0x8000)==(!mask && model==E87_8087 && !iem));
        CHECK(e87_error_signal(&n)==(!mask && (model==E87_80287 || !iem)));
        CHECK((n.status&0xc1)==(mask?1:0x81));
        CHECK((n.status&0x4720)==0x4720);
        if(!mask) {
            CHECK(n.tags==before.tags && e87_top(&n)==top);
            CHECK(!memcmp(n.reg,before.reg,sizeof n.reg));
            CHECK(!memcmp(out,expected,sizeof out));
            CHECK(e87_control(&n,0xdb,0xe2,0));
            CHECK(!e87_error_signal(&n) && !(n.status&0x80bf));
            CHECK(n.tags==before.tags && !memcmp(n.reg,before.reg,sizeof n.reg));
            continue;
        }
        unsigned dest=form==0?(top+7)%8:form>=3?(top+1)%8:top;
        unsigned next=form==0?(top+7)%8:(form==1 || form==4)?(top+1)%8:top;
        CHECK(e87_top(&n)==next);
        if(form==1) {
            memset(expected+1,0,10);expected[8]=0xc0;expected[9]=expected[10]=0xff;
            CHECK(!memcmp(out,expected,sizeof out));
            CHECK(n.tags==0xffff);
        } else {
            CHECK(n.reg[dest].significand==UINT64_C(0xc000000000000000));
            CHECK(n.reg[dest].sign_exponent==0xffff);
            CHECK(((n.tags>>(dest*2))&3)==2);
            for(unsigned i=0;i<8;++i) if(i!=dest)
                CHECK(!memcmp(&n.reg[i],&before.reg[i],sizeof n.reg[i]));
            if(form==4) CHECK(((n.tags>>(top*2))&3)==3);
        }
    }
}
static void nan_checks(void)
{
    for(unsigned model=0;model<2;++model) {
    const unsigned ops[3]={0xd8,0xdc,0xde};
    for(unsigned top=0;top<8;++top) for(unsigned mask=0;mask<2;++mask)
    for(unsigned form=0;form<3;++form) for(unsigned which=0;which<4;++which)
    for(unsigned sign=0;sign<2;++sign) {
        e87_t n,before;e87_init(&n,model);
        unsigned i=(top+1)&7,dest=form?i:top;
        n.status=(top<<11)|0x4700;n.control=(n.control&~1u)|mask;
        n.tags=0xffff & ~(3u<<(2*top)) & ~(3u<<(2*i));
        n.reg[top]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff};
        n.reg[i]=n.reg[top];
        e87_real80_t a={UINT64_C(0x8000000000000011),0x7fff|(sign<<15)};
        e87_real80_t b={UINT64_C(0xc000000000000022),0x7fff|((sign^1)<<15)};
        if(which!=1) { n.reg[top]=a;n.tags|=2u<<(2*top); }
        if(which!=0) { n.reg[i]=b;n.tags|=2u<<(2*i); }
        if(which==3) { n.reg[top]=b;n.reg[i]=a; }
        before=n;
        CHECK(e87_numeric(&n,ops[form],0xc1,0,0)==1);
        CHECK((n.status&0xc1)==(mask?1:0x81));
        CHECK((n.status&0x4700)==0x4700);
        if(!mask) {
            CHECK(n.tags==before.tags && e87_top(&n)==top);
            CHECK(!memcmp(n.reg,before.reg,sizeof n.reg));continue;
        }
        e87_real80_t want=which==0?a:b;
        CHECK(n.reg[dest].significand==want.significand);
        CHECK(n.reg[dest].sign_exponent==want.sign_exponent);
        CHECK(((n.tags>>(2*dest))&3)==2);
        CHECK(e87_top(&n)==(form==2?i:top));
        unsigned char out[10];
        if(form!=1) {
            CHECK(e87_numeric(&n,0xdb,0x3e,0,out)==1);
            e87_real80_t stored;e87_real80_load(&stored,out);
            CHECK(stored.significand==want.significand && stored.sign_exponent==want.sign_exponent);
        }
    }

    }
}
static void infinity_checks(void)
{
    for(unsigned model=0;model<2;++model) {
    const unsigned ops[3]={0xd8,0xdc,0xde};
    for(unsigned top=0;top<8;++top) for(unsigned affine=0;affine<2;++affine)
    for(unsigned mask=0;mask<2;++mask) for(unsigned form=0;form<3;++form)
    for(unsigned signs=0;signs<4;++signs) for(unsigned which=0;which<3;++which) {
        e87_t n,before;e87_init(&n,model);
        unsigned i=(top+1)&7,dest=form?i:top;
        n.status=top<<11;n.control=(n.control&~0x1001u)|(affine<<12)|mask;
        n.tags=0xffff & ~(3u<<(2*top)) & ~(3u<<(2*i));
        n.reg[top]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff|((signs&1)<<15)};
        n.reg[i]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff|((signs>>1)<<15)};
        if(which!=1) { n.reg[top].sign_exponent|=0x7fff;n.tags|=2u<<(2*top); }
        if(which!=0) { n.reg[i].sign_exponent|=0x7fff;n.tags|=2u<<(2*i); }
        before=n;int invalid=which==2 && (!affine || signs==1 || signs==2);
        CHECK(e87_numeric(&n,ops[form],0xc1,0,0)==1);
        CHECK((n.status&0xc1)==(invalid?(mask?1:0x81):0));
        if(invalid && !mask) {
            CHECK(n.tags==before.tags && e87_top(&n)==top);
            CHECK(!memcmp(n.reg,before.reg,sizeof n.reg));continue;
        }
        CHECK(e87_top(&n)==(form==2?i:top));
        CHECK(((n.tags>>(2*dest))&3)==2);
        CHECK(n.reg[dest].significand==(invalid?UINT64_C(0xc000000000000000):UINT64_C(0x8000000000000000)));
        CHECK(n.reg[dest].sign_exponent==(invalid?0xffff:before.reg[which==1?i:top].sign_exponent));
        if(form==2) CHECK(((n.tags>>(2*top))&3)==3);
    }
    for(unsigned sign=0;sign<2;++sign) {
        e87_t n;e87_init(&n,model);
        unsigned char in[10]={0,0,0,0,0,0,0,0x80,0xff,0x7f},out[10];
        in[9]|=sign<<7;
        CHECK(e87_numeric(&n,0xdb,0x2e,in,0)==1 && e87_tag(&n,0)==2);
        CHECK(e87_numeric(&n,0xdb,0x3e,0,out)==1 && n.tags==0xffff);
        CHECK(!memcmp(in,out,10) && !(n.status&0xff));
        /* NaN takes precedence over infinity in either operand position. */
        for(unsigned swap=0;swap<2;++swap) {
            e87_fninit(&n);n.tags=0xfffa;
            n.reg[swap]=(e87_real80_t){UINT64_C(0xc000000000000123),0xffff};
            n.reg[swap^1]=(e87_real80_t){UINT64_C(0x8000000000000000),0x7fff|(sign<<15)};
            CHECK(e87_numeric(&n,0xd8,0xc1,0,0)==1);
            CHECK(n.reg[0].significand==UINT64_C(0xc000000000000123) && (n.status&1));
        }
    }
    }
}
static void denormal_checks(void)
{
    for(unsigned model=0;model<2;++model) {
    const e87_real80_t values[]={
        {1,0},{UINT64_C(0x7fffffffffffffff),0},
        {1,1},{UINT64_C(0x4000000000000000),0x3fff},{0,0x4000}
    };
    for(unsigned top=0;top<8;++top) for(unsigned sign=0;sign<2;++sign)
    for(unsigned k=0;k<5;++k) for(unsigned mask=0;mask<2;++mask) {
        e87_t n;e87_init(&n,model);n.status=top<<11;
        n.control=(n.control&~2u)|(mask<<1);
        e87_real80_t v=values[k];v.sign_exponent|=sign<<15;
        unsigned char in[10],out[10];e87_real80_store(in,&v);
        CHECK(e87_numeric(&n,0xdb,0x2e,in,0)==1);
        CHECK(e87_tag(&n,0)==(k<2?2:0));
        CHECK(!(n.status&0xff)); /* m80 is a transfer, not conversion. */
        CHECK(e87_numeric(&n,0xdb,0x3e,0,out)==1);
        CHECK(!memcmp(in,out,10) && n.tags==0xffff && e87_top(&n)==top);
    }
    const unsigned ops[3]={0xd8,0xdc,0xde};
    for(unsigned top=0;top<8;++top) for(unsigned form=0;form<3;++form)
    for(unsigned which=0;which<3;++which) for(unsigned mask=0;mask<2;++mask) {
        e87_t n,before;e87_init(&n,model);unsigned i=(top+1)&7;
        n.status=(top<<11)|0x4700;n.control=(n.control&~2u)|(mask<<1);
        n.tags=0xffff & ~(3u<<(top*2)) & ~(3u<<(i*2));
        n.reg[top]=n.reg[i]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff};
        if(which!=1) {n.reg[top]=values[0];n.tags|=2u<<(top*2);}
        if(which!=0) {n.reg[i]=values[1];n.tags|=2u<<(i*2);}
        before=n;
        if(mask) {
            CHECK(e87_numeric(&n,ops[form],0xc1,0,0)==1);
            CHECK((n.status&0x82)==2);
            CHECK(e87_top(&n)==(form==2?i:top));
            continue;
        }
        CHECK(e87_numeric(&n,ops[form],0xc1,0,0)==1);
        CHECK(n.status==(before.status|0x82));
        CHECK(n.tags==before.tags && !memcmp(n.reg,before.reg,sizeof n.reg));
        CHECK(e87_control(&n,0xdb,0xe2,0)==1 && !(n.status&0xff));
    }
    }
}
static void unnormal_checks(void)
{
    for(unsigned model=0;model<2;++model) {
    /* Expected raw representations, not host floating-point results. */
    const struct {uint64_t a,b,r;unsigned ea,eb,er;} cases[]={
        {0x4000000000000000ULL,0x2000000000000000ULL,0x6000000000000000ULL,0x4000,0x4000,0x4000},
        {0x4000000000000000ULL,0x2000000000000000ULL,0x2000000000000000ULL,0x4000,0xc000,0x4000},
        {0x4000000000000000ULL,0x4000000000000000ULL,0x8000000000000000ULL,0x4000,0x4000,0x4000},
        {1,1,2,0,0,1},
        {0x7fffffffffffffffULL,1,0x8000000000000000ULL,0,0,1},
        {0x4000000000000000ULL,0x8000000000000000ULL,0x2000000000000000ULL,0x4000,0xbffe,0x4000},
        {0x8000000000000000ULL,0x2000000000000000ULL,0xc000000000000000ULL,0x4000,0xc000,0x3fff},
        {0x4000000000000000ULL,0x4000000000000000ULL,0,0x4000,0xc000,0x4000},
        {0,0x8000000000000000ULL,0x8000000000000000ULL,0x7000,0x3fff,0x3fff}
    };
    for(unsigned k=0;k<sizeof cases/sizeof cases[0];++k) for(unsigned swap=0;swap<2;++swap) {
        e87_t n;e87_init(&n,model);n.tags=0xfff0;
        n.reg[swap]=(e87_real80_t){cases[k].a,cases[k].ea};
        n.reg[swap^1]=(e87_real80_t){cases[k].b,cases[k].eb};
        if(!(cases[k].ea&0x7fff) && cases[k].a)n.tags|=2u<<(swap*2);
        if(!(cases[k].eb&0x7fff) && cases[k].b)n.tags|=2u<<((swap^1)*2);
        CHECK(e87_numeric(&n,0xde,0xc1,0,0)==1);
        CHECK(n.reg[1].significand==cases[k].r);
        CHECK(n.reg[1].sign_exponent==cases[k].er);
        CHECK(e87_top(&n)==1 && e87_tag(&n,0)==0);
    }
    const unsigned widths[3]={24,53,64},pcs[3]={0,2,3};
    for(unsigned k=0;k<3;++k) for(unsigned rc=0;rc<4;++rc)
    for(unsigned sign=0;sign<2;++sign) for(unsigned mask=0;mask<2;++mask) {
        e87_t n;e87_init(&n,model);n.tags=0xfff0;
        n.control=(n.control&~0xf20u)|(pcs[k]<<8)|(rc<<10)|(mask<<5);
        n.reg[0]=(e87_real80_t){0x4000000000000000ULL,0x4000|(sign<<15)};
        n.reg[1]=(e87_real80_t){0x8000000000000000ULL,(0x4000-widths[k]-1)|(sign<<15)};
        CHECK(e87_numeric(&n,0xde,0xc1,0,0)==1);
        unsigned up=rc==(sign?1:2);
        CHECK(n.reg[1].significand==0x4000000000000000ULL+(up?(UINT64_C(1)<<(64-widths[k])):0));
        CHECK(n.reg[1].sign_exponent==(0x4000|(sign<<15)));
        CHECK((n.status&0xa0)==(mask?0x20:0xa0));
        CHECK(!!(n.status&0x200)==up);
    }
    }
}
static void range_checks(void)
{
    for(unsigned model=0;model<2;++model) {
    const unsigned ops[3]={0xd8,0xdc,0xde},pcs[3]={0,2,3};
    for(unsigned kind=0;kind<2;++kind) for(unsigned top=0;top<8;++top)
    for(unsigned form=0;form<3;++form) for(unsigned mask=0;mask<2;++mask)
    for(unsigned rc=0;rc<4;++rc) for(unsigned sign=0;sign<2;++sign)
    for(unsigned pc=0;pc<3;++pc) {
        e87_t n,before;e87_init(&n,model);unsigned i=(top+1)&7,dest=form?i:top;
        unsigned exception=kind?0x10:8;
        n.control=(n.control&~(0xf00u|exception))|(pcs[pc]<<8)|(rc<<10)|(mask?exception:0);
        n.status=(top<<11)|0x4500;n.tags=0xffff & ~(3u<<(top*2)) & ~(3u<<(i*2));
        if(!kind) {
            n.reg[top]=n.reg[i]=(e87_real80_t){UINT64_MAX,0x7ffe|(sign<<15)};
        } else {
            n.reg[top]=(e87_real80_t){0x8000000000000001ULL,1|(sign<<15)};
            n.reg[i]=(e87_real80_t){0x8000000000000000ULL,1|((sign^1)<<15)};
        }
        before=n;
        unsigned saturated=!kind && mask && (rc==(sign?2:1));
        if(saturated && pc!=2) {rejected(&n,ops[form],0xc1,0);continue;}
        CHECK(e87_numeric(&n,ops[form],0xc1,0,0)==1);
        unsigned want_status=!mask?(exception|0x80):!kind?0x28:rc==3?0x10:0;
        if(saturated) want_status=0x20;
        CHECK((n.status&0xff)==want_status);
        CHECK((n.status&0x4500)==0x4500);
        CHECK(e87_top(&n)==(form==2?i:top));
        unsigned exponent=!kind?(mask?0x7fff:0x1fff):(mask?0:0x5fc2);
        uint64_t sig=!kind?(mask?0x8000000000000000ULL:UINT64_MAX):(mask?1:0x8000000000000000ULL);
        if(saturated) {exponent=0x7ffe;sig=UINT64_MAX-1;}
        CHECK(n.reg[dest].sign_exponent==(exponent|(sign<<15)));
        CHECK(n.reg[dest].significand==sig);
        CHECK(((n.tags>>(dest*2))&3)==(mask && !saturated?2:0));
        if(form==2) CHECK(((n.tags>>(top*2))&3)==3);
        for(unsigned k=0;k<8;++k) if(k!=dest) CHECK(!memcmp(&n.reg[k],&before.reg[k],sizeof n.reg[k]));
    }
    /* Masked range overflow can still signal unmasked precision. */
    e87_t n;e87_init(&n,model);n.control&=~0x20;n.tags=0xfff0;
    n.reg[0]=n.reg[1]=(e87_real80_t){UINT64_MAX,0x7ffe};
    CHECK(e87_numeric(&n,0xde,0xc1,0,0)==1);
    CHECK((n.status&0xff)==0xa8 && e87_top(&n)==1);
    }
}
static void rounding_overflow_checks(void)
{
    for(unsigned model=0;model<2;++model) {
    const unsigned widths[3]={24,53,64},pcs[3]={0,2,3};
    for(unsigned k=0;k<3;++k) for(unsigned sign=0;sign<2;++sign)
    for(unsigned pm=0;pm<2;++pm) for(unsigned om=0;om<2;++om)
    for(unsigned rc=0;rc<3;++rc) {
        e87_t n;e87_init(&n,model);n.tags=0xfff0;
        /* Halfway above the largest significand representable at PC. */
        uint64_t largest=UINT64_MAX<<(64-widths[k]);
        n.reg[0]=(e87_real80_t){largest,0x7ffe|(sign<<15)};
        n.reg[1]=(e87_real80_t){UINT64_C(0x8000000000000000),(0x7ffe - widths[k])|(sign<<15)};
        n.control=(n.control&~0xf28u)|(pcs[k]<<8)|(rc<<10)|(pm<<5)|(om<<3);
        unsigned carry=rc==0 || rc==(sign?1:2);
        if(carry && !om) {rejected(&n,0xde,0xc1,0);continue;}
        CHECK(e87_numeric(&n,0xde,0xc1,0,0)==1);
        CHECK(e87_top(&n)==1);
        CHECK(n.reg[1].sign_exponent==((carry?0x7fff:0x7ffe)|(sign<<15)));
        CHECK(n.reg[1].significand==(carry?UINT64_C(0x8000000000000000):largest));
        CHECK((n.status&0x2a8)==((carry?0x208:0)|0x20|(pm?0:0x80)));
    }
    }
}
static void masked_8087_range_checks(void)
{
    const unsigned ops[3]={0xd8,0xdc,0xde},pcs[3]={0,2,3};
    for(unsigned kind=0;kind<2;++kind) for(unsigned top=0;top<8;++top)
    for(unsigned form=0;form<3;++form) for(unsigned rc=0;rc<4;++rc)
    for(unsigned sign=0;sign<2;++sign) for(unsigned pc=0;pc<3;++pc)
    for(unsigned iem=0;iem<2;++iem) {
        e87_t n;e87_init(&n,E87_8087);unsigned i=(top+1)&7,dest=form?i:top;
        n.control=(n.control&~0xf80u)|(pcs[pc]<<8)|(rc<<10)|(iem<<7);
        n.status=top<<11;n.tags=0xffff & ~(3u<<(top*2)) & ~(3u<<(i*2));
        if(!kind) n.reg[top]=n.reg[i]=(e87_real80_t){UINT64_MAX,0x7ffe|(sign<<15)};
        else {
            n.reg[top]=(e87_real80_t){0x8000000000000001ULL,1|(sign<<15)};
            n.reg[i]=(e87_real80_t){0x8000000000000000ULL,1|((sign^1)<<15)};
        }
        unsigned saturated=!kind && rc==(sign?2:1);
        if(saturated && pc!=2) {rejected(&n,ops[form],0xc1,0);continue;}
        CHECK(e87_numeric(&n,ops[form],0xc1,0,0)==1);
        CHECK((n.status&0xff)==(!kind?(saturated?0x20:0x28):(rc==3?0x10:0)));
        CHECK(!e87_pending_error(&n));
        CHECK(e87_top(&n)==(form==2?i:top));
        unsigned exponent=kind?0:saturated?0x7ffe:0x7fff;
        uint64_t sig=kind?1:saturated?UINT64_MAX-1:UINT64_C(0x8000000000000000);
        CHECK(n.reg[dest].sign_exponent==(exponent|(sign<<15)) && n.reg[dest].significand==sig);
        if(form!=1) {
            unsigned char bytes[10];e87_real80_t stored;
            CHECK(e87_numeric(&n,0xdb,0x3e,0,bytes)==1);
            e87_real80_load(&stored,bytes);
            CHECK(stored.significand==sig && stored.sign_exponent==(exponent|(sign<<15)));
        }
    }
    /* Both IEM states: true exponent overflow/underflow and precision on
     * masked overflow all commit a result/pop and latch IR/BUSY. */
    for(unsigned iem=0;iem<2;++iem) for(unsigned kind=0;kind<3;++kind) {
        e87_t n;e87_init(&n,E87_8087);n.tags=0xfff0;
        n.control=(n.control&~0x80u)|(iem<<7);
        n.control &= ~(kind==0?8:kind==1?0x20:0x10);
        if(kind<2) n.reg[0]=n.reg[1]=(e87_real80_t){UINT64_MAX,0x7ffe};
        else {
            n.reg[0]=(e87_real80_t){0x8000000000000001ULL,1};
            n.reg[1]=(e87_real80_t){0x8000000000000000ULL,0x8001};
        }
        CHECK(e87_numeric(&n,0xde,0xc1,0,0));
        CHECK((n.status&0x80bf)==((kind==0?0x88:kind==1?0xa8:0x90)|(iem?0:0x8000)));
        CHECK(e87_top(&n)==1 && e87_error_signal(&n)==!iem);
        CHECK(n.reg[1].sign_exponent==(kind==0?0x1fff:kind==1?0x7fff:0x5fc2));
        CHECK(n.reg[1].significand==(kind==0?UINT64_MAX:UINT64_C(0x8000000000000000)));
    }

}
static void pending_disabled_checks(void)
{
    for(unsigned kind=0;kind<3;++kind) {
        e87_t n;e87_init(&n,E87_8087);n.tags=0xfffc;
        n.reg[0]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff};
        n.control&=~0x20u;e87_raise_exception(&n,0x20);
        CHECK((n.status&0x80a0)==0xa0 && !e87_error_signal(&n));
        CHECK(e87_numeric(&n,0xd8,0xc0,0,0));
        CHECK(n.reg[0].sign_exponent==0x4000 && (n.status&0x80a0)==0xa0);
        e87_control(&n,0xdb,0xe0,0);
        CHECK((n.status&0x8000) && e87_error_signal(&n));
        rejected(&n,0xd8,0xc0,0); /* No invented execution while error-BUSY. */
        e87_control(&n,0xdb,0xe1,0);
        CHECK(!(n.status&0x8000) && !e87_error_signal(&n));
        if(kind==0) {
            unsigned char out[10];e87_real80_t value;
            CHECK(e87_numeric(&n,0xdb,0x3e,0,out));e87_real80_load(&value,out);
            CHECK(value.sign_exponent==0x4000 && n.tags==0xffff);
        } else if(kind==1) {
            n.control&=~1u;
            CHECK(e87_numeric(&n,0xd8,0xc1,0,0)); /* Empty source adds IE. */
            CHECK((n.status&0x80a1)==0xa1 && !e87_error_signal(&n));
        } else {
            CHECK(e87_error_wait(&n));CHECK(e87_error_signal(&n));
            rejected(&n,0xd8,0xc0,0);
            e87_control(&n,0xdb,0xe1,0); /* Handler releases a WAIT override. */
            CHECK(!n.wait_error && !(n.status&0x8000));
            CHECK(e87_numeric(&n,0xd8,0xc0,0,0));
        }
        CHECK(n.status&0x80); /* None of these actions acknowledges IR. */
        e87_control(&n,0xdb,0xe2,0);CHECK(!(n.status&0x80bf));
    }
}
static void pseudo_denormal_checks(void)
{
    const unsigned pcs[]={0,2,3};
    const uint64_t values[]={UINT64_C(0x8000000000000000),
        UINT64_C(0x8000000000000001),UINT64_MAX};
    for(unsigned model=0;model<2;++model) for(unsigned v=0;v<3;++v)
    for(unsigned sign=0;sign<2;++sign) for(unsigned rc=0;rc<4;++rc)
    for(unsigned pc=0;pc<3;++pc) for(unsigned form=0;form<3;++form)
    for(unsigned dm=0;dm<2;++dm) {
        const unsigned ops[]={0xd8,0xdc,0xde};
        e87_t n,normal,before;e87_init(&n,model);
        n.control=(n.control&~0xf02u)|(pcs[pc]<<8)|(rc<<10)|(dm<<1);
        n.tags=0xfff2;
        n.reg[0]=(e87_real80_t){values[v],sign<<15};
        n.reg[1]=(e87_real80_t){UINT64_C(0x8000000000000000),1|(sign<<15)};
        normal=n;normal.reg[0].sign_exponent|=1;normal.tags=0xfff0;before=n;
        CHECK(e87_numeric(&n,ops[form],0xc1,0,0));
        if(!dm) {
            CHECK(n.tags==before.tags && !memcmp(n.reg,before.reg,sizeof n.reg));
            CHECK((n.status&0xff)==0x82 && e87_top(&n)==0);
        } else {
            CHECK(e87_numeric(&normal,ops[form],0xc1,0,0));
            unsigned dest=form?1:0;
            CHECK(((n.tags>>(2*dest))&3)==((normal.tags>>(2*dest))&3));
            CHECK(e87_top(&n)==e87_top(&normal));
            CHECK(form==2 || ((n.tags>>(2*(dest^1)))&3)==((before.tags>>(2*(dest^1)))&3));
            CHECK(n.reg[dest].significand==normal.reg[dest].significand &&
                  n.reg[dest].sign_exponent==normal.reg[dest].sign_exponent);
            CHECK(n.status==(normal.status|2));
            CHECK(!memcmp(&n.reg[dest^1],&before.reg[dest^1],sizeof n.reg[0]));
        }
    }
    /* m80 stores retain the integer bit and exponent, rather than canonicalizing. */
    for(unsigned model=0;model<2;++model) for(unsigned sign=0;sign<2;++sign) {
        e87_t n;e87_init(&n,model);n.control&=~2u;
        e87_real80_t raw={UINT64_MAX,sign<<15};unsigned char in[10],out[10];
        e87_real80_store(in,&raw);
        CHECK(e87_numeric(&n,0xdb,0x2e,in,0));
        CHECK(e87_tag(&n,0)==2 && !(n.status&0xff));
        CHECK(e87_numeric(&n,0xdb,0x3e,0,out));CHECK(!memcmp(in,out,10));
    }
}
static void nan_transfer_checks(void)
{
    const uint64_t payloads[]={0,1,UINT64_C(0x8000000000000001),
        UINT64_C(0xc000000000000000),UINT64_MAX};
    for(unsigned model=0;model<2;++model) for(unsigned k=0;k<5;++k)
    for(unsigned sign=0;sign<2;++sign) for(unsigned mask=0;mask<2;++mask)
    for(unsigned top=0;top<8;++top) {
        e87_t n;e87_init(&n,model);n.control=(n.control&~1u)|mask;n.status=top<<11;
        unsigned char in[10],out[10];
        e87_real80_t v={payloads[k],0x7fff|(sign<<15)};e87_real80_store(in,&v);
        CHECK(e87_numeric(&n,0xdb,0x2e,in,0));
        CHECK(e87_tag(&n,0)==2 && !(n.status&0x80ff));
        CHECK(e87_numeric(&n,0xdb,0x3e,0,out));
        CHECK(!memcmp(in,out,10) && n.tags==0xffff && e87_top(&n)==top);
    }
}
static void mixed_special_checks(void)
{
    const e87_real80_t special[]={
        {UINT64_C(0xc000000000000011),0x7fff},
        {UINT64_C(0x8000000000000000),0x7fff}
    };
    for(unsigned model=0;model<2;++model) for(unsigned swap=0;swap<2;++swap)
    for(unsigned kind=0;kind<2;++kind) for(unsigned dm=0;dm<2;++dm)
    for(unsigned im=0;im<2;++im) for(unsigned empty=0;empty<2;++empty) {
        e87_t n,before;e87_init(&n,model);n.tags=0xfffa;
        n.control=(n.control&~3u)|(dm<<1)|im;
        n.reg[swap]=special[kind];n.reg[swap^1]=(e87_real80_t){1,0};
        if(empty) n.tags|=3u<<(2*swap);
        before=n;
        CHECK(e87_numeric(&n,0xde,0xc1,0,0));
        unsigned exc=empty?1:!dm?2:kind?2:3;
        unsigned stopped=empty?!im:!dm || (!kind && !im);
        CHECK((n.status&0x3f)==exc);
        CHECK(!!(n.status&0x80)==stopped);
        CHECK(!(n.status&0x8000));
        if(stopped) {
            CHECK(n.tags==before.tags && e87_top(&n)==0);
            CHECK(!memcmp(n.reg,before.reg,sizeof n.reg));
        } else {
            CHECK(e87_top(&n)==1 && e87_tag(&n,0)==2);
            CHECK(n.reg[1].significand==(empty?UINT64_C(0xc000000000000000):special[kind].significand));
        }
    }
}
int main(void)
{
    unsigned model, rotation, j;
    unsigned char one[10] = {0,0,0,0,0,0,0,0x80,0xff,0x3f};
    unsigned char out[12], tiny[10], bad[10];
    e87_t n;
    for (model=0; model<2; ++model) for (rotation=0; rotation<8; ++rotation) {
        e87_init(&n, model); n.status = rotation << 11;
        CHECK(e87_numeric(&n, 0xdb, 0x2e, one, 0));
        CHECK(e87_numeric(&n, 0xdb, 0x2e, one, 0));
        CHECK(e87_numeric(&n, 0xde, 0xc1, 0, 0));
        CHECK(e87_top(&n) == ((rotation+7)&7));
        memset(out, 0xa5, sizeof(out));
        CHECK(e87_numeric(&n, 0xdb, 0x3e, 0, out+1));
        CHECK(out[0] == 0xa5 && out[11] == 0xa5);
        CHECK(out[8] == 0x80 && out[9] == 0 && out[10] == 0x40);
        for (j=1;j<8;++j) CHECK(out[j] == 0);
        CHECK(n.tags == 0xffff && e87_top(&n) == rotation);
        for (j=0;j<8;++j) CHECK(e87_numeric(&n, 0xdb, 0x2e, one, 0));
        CHECK(e87_numeric(&n, 0xdb, 0x2e, one, 0)); /* masked full stack */
        CHECK((n.status & 0xc1) == 1 && e87_tag(&n, 0) == 2);
        e87_fninit(&n);
        memcpy(bad, one, 10); bad[8]=0; bad[9]=0;
        CHECK(e87_numeric(&n, 0xdb, 0x2e, bad, 0));
        CHECK(e87_tag(&n,0)==2 && !(n.status&0xff));
        CHECK(e87_numeric(&n, 0xdb, 0x3e, 0, out));
        CHECK(!memcmp(bad,out,10));
        CHECK(e87_numeric(&n, 0xdb, 0x2e, one, 0));
        memcpy(tiny, one, 10); tiny[8]=0xc3; /* 2^-60 */
        CHECK(e87_numeric(&n, 0xdb, 0x2e, tiny, 0));
        n.control = (n.control & ~0x300) | 0x200; /* 53-bit precision */
        CHECK(e87_numeric(&n, 0xde, 0xc1, 0, 0));
        CHECK((n.status & 0xa0) == 0x20);
        CHECK(n.reg[e87_top(&n)].significand == UINT64_C(0x8000000000000000));
        e87_fninit(&n);
        CHECK(e87_numeric(&n, 0xdb, 0x2e, one, 0));
        CHECK(e87_numeric(&n, 0xdb, 0x2e, tiny, 0));
        CHECK(e87_numeric(&n, 0xdc, 0xc1, 0, 0));
        CHECK(n.reg[e87_index(&n, 1)].significand == UINT64_C(0x8000000000000008));
        e87_fninit(&n);
        CHECK(e87_numeric(&n, 0xdb, 0x2e, one, 0));
        CHECK(e87_numeric(&n, 0xd8, 0xc0, 0, 0)); /* ST0 aliases itself */
        CHECK(n.reg[e87_top(&n)].sign_exponent == 0x4000);
        CHECK(e87_top(&n) == 7 && e87_tag(&n, 0) == 0);
        n.control = (n.control & ~0x300) | 0x100;
        rejected(&n, 0xd8, 0xc0, 0); /* reserved precision */
        e87_fninit(&n);
        CHECK(e87_numeric(&n, 0xdb, 0x2e, one, 0));
        memcpy(bad, one, 10); bad[9] |= 0x80; /* -1 */
        CHECK(e87_numeric(&n, 0xdb, 0x2e, bad, 0));
        CHECK(e87_numeric(&n, 0xde, 0xc1, 0, 0));
        CHECK(n.reg[e87_top(&n)].significand == 0);
        CHECK(n.reg[e87_top(&n)].sign_exponent == 0);
        CHECK(e87_tag(&n, 0) == 1);
        rejected(&n, 0xdb, 0x2e, 0); /* missing staged input */
        CHECK(e87_numeric(&n, 0xd8, 0xd0, 0, 0)); /* self comparison */
        CHECK((n.status&0x4500)==0x4000);
        n.status |= 0x80;
        if(model==E87_8087) n.control &= ~0x80u;
        rejected(&n, 0xd8, 0xc0, 0);
    }
    product_rounding_and_zero();
    multiplication_division();
    memory_arithmetic_checks();
    other_arithmetic_checks();
    square_root_vectors();
    arithmetic_exception_checks();
    subtraction_rounding();
    subtraction_checks();
    pending_disabled_checks();
    pseudo_denormal_checks();
    nan_transfer_checks();
    mixed_special_checks();
    precision_checks();
    stack_checks();
    nan_checks();
    infinity_checks();
    denormal_checks();
    unnormal_checks();
    range_checks();
    rounding_overflow_checks();
    masked_8087_range_checks();
    printf("NPX numeric sequence: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
