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
        rejected(&n, 0xd8, 0xc8, 0); /* unsupported operation */
        n.status |= 0x80;
        if(model==E87_8087) n.control &= ~0x80u;
        rejected(&n, 0xd8, 0xc0, 0);
    }
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
