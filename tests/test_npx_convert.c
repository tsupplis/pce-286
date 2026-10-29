/* Early 8087/287 comparisons and conversions. GPL-2.0-or-later. */
#include <cpu/e8087/execute.h>
#include <stdio.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do {++checks;if(!(x)){++failures;fprintf(stderr,"line %d: %s\n",__LINE__,#x);}} while(0)
static uint64_t get(const unsigned char *p,unsigned size)
{uint64_t v=0;for(unsigned k=0;k<size;++k)v|=(uint64_t)p[k]<<(8*k);return v;}
static void put(unsigned char *p,unsigned size,uint64_t v)
{for(unsigned k=0;k<size;++k)p[k]=v>>(8*k);}
static e87_real80_t whole(int value)
{
    unsigned sign=value<0,e=0;uint64_t mag=sign?-(int64_t)value:value,x=mag;
    if(!x)return(e87_real80_t){0,0};
    while(x>>1){x>>=1;++e;}return(e87_real80_t){mag<<(63-e),(sign<<15)|(0x3fff+e)};
}
static int equal(e87_real80_t a,e87_real80_t b)
{return a.significand==b.significand && a.sign_exponent==b.sign_exponent;}
static void set(e87_t *n,unsigned logical,e87_real80_t v,unsigned tag)
{unsigned i=e87_index(n,logical);n->reg[i]=v;n->tags=(n->tags&~(3u<<(2*i)))|(tag<<(2*i));}
static void compare_tests(void)
{
    const int values[]={-3,-1,0,1,3};
    for(unsigned m=0;m<2;++m)for(unsigned t=0;t<8;++t)for(unsigned p=0;p<3;++p)
    for(unsigned a=0;a<5;++a)for(unsigned b=0;b<5;++b) {
        e87_t n;e87_init(&n,m);n.status=(t<<11)|0x4700;
        set(&n,0,whole(values[a]),a==2?1:0);set(&n,1,whole(values[b]),b==2?1:0);
        e87_real80_t x=n.reg[t],y=n.reg[(t+1)&7];
        CHECK(e87_numeric(&n,p==2?0xde:0xd8,p==2?0xd9:p?0xd9:0xd1,0,0));
        unsigned cc=a==b?0x4000:a<b?0x100:0;
        CHECK((n.status&0x4700)==(cc|0x200));CHECK(e87_top(&n)==((t+p)&7));
        CHECK(equal(n.reg[t],x)&&equal(n.reg[(t+1)&7],y));CHECK(!(n.status&0x3f));
    }
    const unsigned ops[]={0xd8,0xdc,0xda,0xde},sizes[]={4,8,4,2};
    for(unsigned m=0;m<2;++m)for(unsigned f=0;f<4;++f)for(unsigned p=0;p<2;++p) {
        e87_t n;e87_init(&n,m);set(&n,0,whole(1),0);
        unsigned char in[8]={0};put(in,sizes[f],f==0?0x40000000:f==1?UINT64_C(0x4000000000000000):2);
        CHECK(e87_cpu_memory(&n,ops[f],p?0x18:0x10,1,0)==((sizes[f]<<2)|1));
        CHECK(e87_numeric(&n,ops[f],p?0x18:0x10,in,0));CHECK((n.status&0x4500)==0x100 && e87_top(&n)==p);
    }
    const e87_real80_t specimens[]={ {UINT64_C(0x4000000000000000),0x3fff},
      {UINT64_C(0xc000000000000001),0x7fff},{UINT64_C(0x8000000000000000),0x3fff},
      {UINT64_C(0x8000000000000000),0x7fff},{0,0},{1,0},{0,0x3fff} };
    const unsigned classes[]={0,0x100,0x400,0x500,0x4000,0x4400,0};
    for(unsigned m=0;m<2;++m)for(unsigned k=0;k<7;++k)for(unsigned sign=0;sign<2;++sign)for(unsigned empty=0;empty<2;++empty){
        e87_t n;e87_init(&n,m);e87_real80_t x=specimens[k];x.sign_exponent|=sign<<15;
        set(&n,0,x,empty?3:k==1||k==3||k==5?2:k==4?1:0);
        n.control&=~0x3fu;CHECK(e87_numeric(&n,0xd9,0xe5,0,0));
        CHECK((n.status&0x4700)==((empty?0x4100:classes[k])|(sign<<9)));CHECK(!(n.status&0xbf));
    }
    for(unsigned m=0;m<2;++m)for(unsigned affine=0;affine<2;++affine)for(unsigned mask=0;mask<2;++mask){
        e87_t n;e87_init(&n,m);n.control=(n.control&~0x1001u)|(affine<<12)|mask;
        set(&n,0,specimens[3],2);set(&n,1,whole(1),0);n.status=0x4000;
        CHECK(e87_numeric(&n,0xd8,0xd9,0,0));
        if(affine)CHECK(!(n.status&0x4501)&&e87_top(&n)==1);
        else if(mask)CHECK((n.status&0x4501)==0x4501&&e87_top(&n)==1);
        else CHECK((n.status&0x4581)==0x4081&&e87_top(&n)==0);
    }
    for(unsigned m=0;m<2;++m)for(unsigned masks=0;masks<4;++masks) {
        e87_t n;e87_init(&n,m);n.control=(n.control&~3u)|masks;n.status=0x200;
        unsigned char denormal[4]={1};CHECK(e87_numeric(&n,0xd8,0x18,denormal,0));
        CHECK((n.status&3)==3);
        CHECK(e87_top(&n)==(masks==3?1:0));CHECK((n.status&0x4500)==(masks==3?0x4500:0));
    }
    for(unsigned m=0;m<2;++m)for(unsigned mask=0;mask<2;++mask) {
        e87_t n;e87_init(&n,m);n.control=(n.control&~2u)|(mask?2:0);
        set(&n,0,specimens[0],0);set(&n,1,(e87_real80_t){UINT64_C(0x8000000000000000),0x3ffe},0);
        CHECK(e87_numeric(&n,0xd8,0xd9,0,0));CHECK((n.status&3)==2);
        CHECK(e87_top(&n)==mask && (n.status&0x4500)==(mask?0x4000:0));
        e87_init(&n,m);set(&n,0,(e87_real80_t){0,0x8000},1);
        CHECK(e87_numeric(&n,0xd9,0xe4,0,0));CHECK((n.status&0x4500)==0x4000);
    }
}
static void integer_tests(void)
{
    const uint64_t vectors[]={0,1,2,0x7fff,0x8000,0xffff,0x7fffffff,0x80000000,
        UINT64_C(0xffffffff),UINT64_C(0x7fffffffffffffff),UINT64_C(0x8000000000000000),UINT64_MAX};
    const unsigned ops[]={0xdf,0xdb,0xdf},load[]={0,0,0x28},store[]={0x18,0x18,0x38},sizes[]={2,4,8};
    for(unsigned m=0;m<2;++m)for(unsigned f=0;f<3;++f)for(unsigned t=0;t<8;++t)
    for(unsigned pc=0;pc<4;++pc)for(unsigned k=0;k<sizeof vectors/sizeof *vectors;++k){
        e87_t n;e87_init(&n,m);n.status=t<<11;n.control=(n.control&~0x300u)|(pc<<8);
        unsigned char in[10]={0},out[12];memset(out,0xa5,sizeof out);put(in,sizes[f],vectors[k]);
        CHECK(e87_numeric(&n,ops[f],load[f],in,0));CHECK(e87_top(&n)==((t+7)&7));
        CHECK(e87_numeric(&n,ops[f],store[f],0,out+1));
        CHECK(!memcmp(in,out+1,sizes[f]) && out[0]==0xa5 && out[1+sizes[f]]==0xa5);
        CHECK(e87_top(&n)==t && n.tags==0xffff && !(n.status&0x3f));
    }
    /* Exhaust every signed 16-bit representation through load/store. */
    for(unsigned m=0;m<2;++m)for(unsigned raw=0;raw<65536;++raw){
        e87_t n;e87_init(&n,m);unsigned char in[2],out[2];put(in,2,raw);
        CHECK(e87_numeric(&n,0xdf,0,in,0));CHECK(e87_numeric(&n,0xdf,0x18,0,out));
        CHECK(get(out,2)==raw && n.tags==0xffff && !(n.status&0x3f));
    }
    for(unsigned m=0;m<2;++m)for(unsigned f=0;f<3;++f)for(unsigned rc=0;rc<4;++rc)
    for(unsigned neg=0;neg<2;++neg)for(unsigned odd=0;odd<2;++odd)for(unsigned mask=0;mask<2;++mask){
        e87_t n;e87_init(&n,m);n.control=(n.control&~0xc20u)|(rc<<10)|(mask?0x20:0);
        set(&n,0,(e87_real80_t){odd?UINT64_C(0xe000000000000000):UINT64_C(0xa000000000000000),0x4000|(neg<<15)},0);
        unsigned char out[8];CHECK(e87_numeric(&n,ops[f],store[f],0,out));
        unsigned up=rc==0?odd:rc==1?neg:rc==2?!neg:0;
        uint64_t expected=2+odd+up;if(neg)expected=-expected;
        uint64_t maskbits=sizes[f]==8?UINT64_MAX:(UINT64_C(1)<<(sizes[f]*8))-1;
        CHECK(get(out,sizes[f])==(expected&maskbits));CHECK(e87_top(&n)==1);
        CHECK((n.status&0x2a0)==((up?0x200:0)|(mask?0x20:0xa0)));
    }
    for(unsigned m=0;m<2;++m)for(unsigned f=0;f<3;++f)for(unsigned mask=0;mask<2;++mask){
        e87_t n;e87_init(&n,m);n.control=(n.control&~1u)|mask;
        set(&n,0,(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff+sizes[f]*8-1},0);
        unsigned char out[10],before[10];memset(out,0xa5,10);memcpy(before,out,10);
        CHECK(e87_numeric(&n,ops[f],store[f],0,out)==(mask?1:2));
        CHECK((n.status&0x3f)==1 && e87_top(&n)==mask);
        if(mask)CHECK(get(out,sizes[f])==(UINT64_C(1)<<(sizes[f]*8-1)));
        else CHECK(!memcmp(out,before,10));
    }
}
static void real_tests(void)
{
    for(unsigned m=0;m<2;++m)for(unsigned f=0;f<2;++f)for(unsigned rc=0;rc<4;++rc)
    for(unsigned sign=0;sign<2;++sign)for(unsigned pc=0;pc<4;++pc){
        unsigned size=f?8:4,op=f?0xdd:0xd9,bias=f?1023:127,frac=f?52:23,cut=63-frac;
        e87_t n;e87_init(&n,m);n.control=(n.control&~0xf00u)|(rc<<10)|(pc<<8);
        set(&n,0,(e87_real80_t){UINT64_C(0x8000000000000000)|(UINT64_C(1)<<(cut-1)),0x3fff|(sign<<15)},0);
        unsigned char out[10];memset(out,0xa5,10);CHECK(e87_numeric(&n,op,0x18,0,out));
        unsigned up=rc==1?sign:rc==2?!sign:0;
        CHECK(get(out,size)==(((uint64_t)sign<<(size*8-1))|((uint64_t)bias<<frac)|up));
        CHECK((n.status&0x23f)==(0x20|(up?0x200:0)));CHECK(e87_top(&n)==1);
        CHECK(out[size]==0xa5);
    }
    uint64_t random=UINT64_C(0x808780287);
    for(unsigned m=0;m<2;++m)for(unsigned f=0;f<2;++f)for(unsigned k=0;k<1024;++k){
        random^=random<<13;random^=random>>7;random^=random<<17;
        unsigned size=f?8:4,op=f?0xdd:0xd9,frac=f?52:23,emax=f?2047:255;
        uint64_t raw=f?random:(uint32_t)random;unsigned e=(raw>>frac)&emax;
        if(!e || e==emax)continue;
        unsigned char in[8],out[8];put(in,size,raw);e87_t n;e87_init(&n,m);
        CHECK(e87_numeric(&n,op,0,in,0));CHECK(e87_numeric(&n,op,0x18,0,out));
        CHECK(!memcmp(in,out,size)&&!(n.status&0x3f));
    }
    for(unsigned m=0;m<2;++m)for(unsigned f=0;f<2;++f)for(unsigned under=0;under<2;++under)
    for(unsigned mask=0;mask<2;++mask)for(unsigned rc=0;rc<4;++rc){
        unsigned size=f?8:4,op=f?0xdd:0xd9,frac=f?52:23,bias=f?1023:127,exception=under?0x10:8;
        e87_t n;e87_init(&n,m);n.control=(n.control&~(exception|0xc00u))|(mask?exception:0)|(rc<<10);
        set(&n,0,(e87_real80_t){UINT64_C(0x8000000000000000),under?0x3fff-bias:0x3fff+bias+1},0);
        unsigned char out[10],sentinel[10];memset(out,0xa5,10);memcpy(sentinel,out,10);
        CHECK(e87_numeric(&n,op,0x18,0,out)==(mask?1:2));CHECK(e87_top(&n)==mask);
        if(!mask)CHECK(!memcmp(out,sentinel,10)&&(n.status&0x3f)==exception);
        else if(under)CHECK(get(out,size)==(UINT64_C(1)<<(frac-1)) && (n.status&0x3f)==(rc==3?0x10:0));
        else {uint64_t expected=rc==1?((uint64_t)(2*bias)<<frac)|((UINT64_C(1)<<frac)-2):((uint64_t)(2*bias+1)<<frac);
            CHECK(get(out,size)==expected);CHECK((n.status&0x3f)==(rc==1?0x20:0x28));}
    }
    for(unsigned m=0;m<2;++m)for(unsigned f=0;f<2;++f)for(unsigned mask=0;mask<2;++mask){
        e87_t n;e87_init(&n,m);n.control=(n.control&~2u)|(mask?2:0);
        unsigned char in[8]={1};CHECK(e87_numeric(&n,f?0xdd:0xd9,0,in,0));
        CHECK((n.status&0x3f)==2 && e87_top(&n)==(mask?7:0));
        if(!mask)CHECK(n.tags==0xffff);
    }
}
static void tiny_and_special_stores(void)
{
    for(unsigned m=0;m<2;++m)for(unsigned f=0;f<2;++f)for(unsigned rc=0;rc<4;++rc)for(unsigned sign=0;sign<2;++sign) {
        e87_t n;e87_init(&n,m);n.control=(n.control&~0xc00u)|(rc<<10);
        set(&n,0,(e87_real80_t){UINT64_C(0x8000000000000000),1|(sign<<15)},0);
        unsigned char out[8];unsigned size=f?8:4;
        CHECK(e87_numeric(&n,f?0xdd:0xd9,0x18,0,out));
        unsigned up=rc==1?sign:rc==2?!sign:0;
        CHECK(get(out,size)==(((uint64_t)sign<<(size*8-1))|up));
        CHECK((n.status&0x23f)==(0x30|(up?0x200:0)));
    }
    const e87_real80_t values[]={ {UINT64_C(0xc123456789abcdef),0xffff},
        {UINT64_C(0x8000000000000000),0x7fff},{UINT64_C(0x123456789abcdef),0},
        {UINT64_C(0x4000000000000000),0x3fff} };
    for(unsigned m=0;m<2;++m)for(unsigned f=0;f<2;++f)for(unsigned k=0;k<4;++k)for(unsigned mask=0;mask<2;++mask) {
        e87_t n;e87_init(&n,m);n.control=(n.control&~1u)|mask;set(&n,0,values[k],k==3?0:2);
        unsigned char out[8],sentinel[8];memset(out,0xa5,8);memcpy(sentinel,out,8);
        unsigned size=f?8:4,frac=f?52:23,emax=f?2047:255;
        CHECK(e87_numeric(&n,f?0xdd:0xd9,0x18,0,out)==(k==3&&!mask?2:1));
        if(k==3&&!mask) {CHECK(!memcmp(out,sentinel,8) && e87_top(&n)==0);continue;}
        uint64_t raw=k==3?((UINT64_C(1)<<(size*8-1))|((uint64_t)emax<<frac)|(UINT64_C(1)<<(frac-1))):
            ((uint64_t)(values[k].sign_exponent>>15)<<(size*8-1))|
            ((uint64_t)(k==2?0:emax)<<frac)|((values[k].significand>>(63-frac))&((UINT64_C(1)<<frac)-1));
        CHECK(get(out,size)==raw && (n.status&0x3f)==(k==3?1:0));
    }
}
static void bcd_and_transfers(void)
{
    const uint64_t values[]={0,1,123456789,UINT64_C(999999999999999999)};
    for(unsigned m=0;m<2;++m)for(unsigned k=0;k<4;++k)for(unsigned sign=0;sign<2;++sign){
        unsigned char in[10]={0},out[10];uint64_t v=values[k];
        for(unsigned j=0;j<9;++j){in[j]=v%10;v/=10;in[j]|=(v%10)<<4;v/=10;}in[9]=sign<<7;
        e87_t n;e87_init(&n,m);CHECK(e87_numeric(&n,0xdf,0x20,in,0));
        CHECK(e87_numeric(&n,0xdf,0x30,0,out));CHECK(!memcmp(in,out,10));CHECK(n.tags==0xffff&&!(n.status&0x3f));
    }
    for(unsigned m=0;m<2;++m) {
        e87_t n,before;e87_init(&n,m);unsigned char in[10]={0xfa},out[10],sentinel[10];before=n;
        CHECK(!e87_numeric(&n,0xdf,0x20,in,0));CHECK(!memcmp(&n,&before,sizeof n));
        set(&n,0,(e87_real80_t){UINT64_C(0xb800000000000000),0x4002},0); /* 11.5 */
        before=n;memset(out,0xa5,10);memcpy(sentinel,out,10);
        CHECK(!e87_numeric(&n,0xdf,0x30,0,out));CHECK(!memcmp(&n,&before,sizeof n)&&!memcmp(out,sentinel,10));
        CHECK(e87_numeric(&n,0xd9,0xfc,0,0));CHECK(e87_numeric(&n,0xdf,0x30,0,out));CHECK(out[0]==0x12);
        e87_init(&n,m);CHECK(e87_numeric(&n,0xdf,0x30,0,out));CHECK(out[8]==0xff&&out[9]==0xff&&(n.status&1));
    }
    for(unsigned m=0;m<2;++m)for(unsigned t=0;t<8;++t) {
        e87_t n;e87_init(&n,m);n.status=t<<11;set(&n,0,whole(7),0);set(&n,1,whole(3),0);
        CHECK(e87_numeric(&n,0xd9,0xc0,0,0));CHECK(e87_top(&n)==((t+7)&7));
        CHECK(equal(n.reg[e87_top(&n)],whole(7)));
        CHECK(e87_numeric(&n,0xd9,0xca,0,0));CHECK(equal(n.reg[e87_top(&n)],whole(3)));
        CHECK(e87_numeric(&n,0xdd,0xdb,0,0));CHECK(e87_top(&n)==t && equal(n.reg[(t+2)&7],whole(3)));
    }
}
static void exception_order_and_stack(void)
{
    for(unsigned m=0;m<2;++m)for(unsigned dm=0;dm<2;++dm)for(unsigned im=0;im<2;++im) {
        e87_t n;e87_init(&n,m);n.control=(n.control&~3u)|(dm<<1)|im;
        set(&n,0,(e87_real80_t){UINT64_C(0xc123000000000000),0xffff},2);
        set(&n,1,(e87_real80_t){1,0},2);n.status=0x200;
        e87_real80_t a=n.reg[0],b=n.reg[1];
        CHECK(e87_numeric(&n,0xde,0xd9,0,0));
        CHECK((n.status&3)==(dm?3:2));CHECK(e87_top(&n)==(dm&&im?2:0));
        CHECK((n.status&0x4700)==(dm&&im?0x4700:0x200));
        CHECK(equal(n.reg[0],a)&&equal(n.reg[1],b));
    }
    for(unsigned m=0;m<2;++m)for(unsigned mask=0;mask<2;++mask)for(unsigned f=0;f<3;++f) {
        e87_t n;e87_init(&n,m);n.control=(n.control&~1u)|mask;
        unsigned char out[10],before[10];memset(out,0xa5,10);memcpy(before,out,10);
        CHECK(e87_numeric(&n,f==0?0xd9:f==1?0xdb:0xdf,f==2?0x30:0x18,0,out)==(mask?1:2));
        CHECK((n.status&1)&&e87_top(&n)==mask);
        if(!mask)CHECK(!memcmp(out,before,10)&&n.tags==0xffff);
        e87_init(&n,m);n.control=(n.control&~1u)|mask;n.tags=0; /* full stack */
        set(&n,0,whole(1),0);e87_t saved=n;
        CHECK(e87_numeric(&n,0xd9,0xc0,0,0));CHECK(n.status&1);
        if(mask)CHECK(e87_top(&n)==7 && n.reg[7].sign_exponent==0xffff);
        else CHECK(n.tags==saved.tags&&e87_top(&n)==0&&!memcmp(n.reg,saved.reg,sizeof n.reg));
        e87_init(&n,m);n.control=(n.control&~1u)|mask;set(&n,1,whole(2),0);
        CHECK(e87_numeric(&n,0xd9,0xc9,0,0));CHECK(n.status&1);
        if(mask)CHECK(equal(n.reg[0],whole(2))&&n.reg[1].sign_exponent==0xffff);
        else CHECK(e87_tag(&n,0)==3&&equal(n.reg[1],whole(2)));
    }
}
int main(void)
{
    compare_tests();exception_order_and_stack();integer_tests();real_tests();tiny_and_special_stores();bcd_and_transfers();
    printf("NPX comparisons/conversions: %u checks, %u failures\n",checks,failures);return failures!=0;
}
