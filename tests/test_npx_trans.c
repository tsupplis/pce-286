/* Early NPX constants/transcendental tests. GPL-2.0-or-later. */
#include <cpu/e8087/execute.h>
#include <cpu/e8086/e8086.h>
#include <stdio.h>
#include <string.h>
#include "npx_trans_vectors.h"
#include "npx_tangent_vectors.h"
#include <cpu/e8087/arithmetic.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){if(failures++<15)fprintf(stderr,"line %d: %s\n",__LINE__,#x);}}while(0)
static void set(e87_t *n,unsigned logical,e87_real80_t x)
{unsigned p=e87_index(n,logical);n->reg[p]=x;n->tags=(n->tags&~(3u<<(2*p)))|((x.significand?0:1)<<(2*p));}
static int equal(e87_real80_t a,e87_real80_t b)
{return a.significand==b.significand&&a.sign_exponent==b.sign_exponent;}
/* A one-ulp functional tolerance; this deliberately does not certify Intel's
 * original approximation or universal correct rounding. */
static int close80(e87_real80_t a,e87_real80_t b)
{
    if(a.sign_exponent!=b.sign_exponent){
        if((a.sign_exponent^b.sign_exponent)&0x8000)return 0;
        e87_real80_t hi=a,lo=b;if((hi.sign_exponent&0x7fff)<(lo.sign_exponent&0x7fff)){hi=b;lo=a;}
        return hi.sign_exponent==lo.sign_exponent+1 && hi.significand==UINT64_C(0x8000000000000000)&&lo.significand==UINT64_MAX;
    }
    return a.significand>b.significand?a.significand-b.significand<=1:b.significand-a.significand<=1;
}
static void tangent_tests(void)
{
 for(unsigned m=0;m<2;++m)for(unsigned k=0;k<sizeof tangents/sizeof *tangents;++k){
  e87_t n;e87_init(&n,m);n.status=0x4700;n.control=(n.control&~0xc00u)|(tangents[k].rc<<10);
  set(&n,0,tangents[k].angle);CHECK(e87_numeric(&n,0xd9,0xf2,0,0));
  CHECK(e87_top(&n)==7 && e87_tag(&n,0)==0);
  CHECK(close80(n.reg[0],tangents[k].num));CHECK(close80(n.reg[7],tangents[k].den));
  CHECK((n.status&0x4700)==0x4700);CHECK(!!(n.status&0x20)==!!tangents[k].angle.significand);
  e87_real80_t ratio;unsigned flags;static const unsigned rc[]={0,2,3,1};
  CHECK(e87_div80(n.reg[0],n.reg[7],80,rc[tangents[k].rc],&ratio,&flags));
  /* Ratio has rounding of both returned components plus division. */
  e87_real80_t expected=tangents[k].ratio;
  unsigned long long delta=ratio.significand>expected.significand?ratio.significand-expected.significand:expected.significand-ratio.significand;
  CHECK(close80(ratio,expected)||(ratio.sign_exponent==expected.sign_exponent&&delta<=4));
 }
 for(unsigned m=0;m<2;++m)for(unsigned t=0;t<8;++t)for(unsigned pc=0;pc<4;++pc)
 for(unsigned mask=0;mask<2;++mask){
  e87_t n;e87_init(&n,m);n.status=(t<<11)|0x4700;n.control=(n.control&~0x320u)|(pc<<8)|(mask?0x20:0);
  e87_real80_t small={UINT64_C(0x8000000000000000),0x3fbf};set(&n,0,small);
  CHECK(e87_numeric(&n,0xd9,0xf2,0,0));CHECK(e87_top(&n)==((t+7)&7));
  CHECK(equal(n.reg[t],small));CHECK(n.reg[e87_top(&n)].sign_exponent==0x3fff);
  CHECK(!!(n.status&0x80)==!mask);CHECK((n.status&0x4700)==0x4700);
 }
 for(unsigned m=0;m<2;++m)for(unsigned mask=0;mask<2;++mask)for(unsigned empty=0;empty<2;++empty){
  e87_t n;e87_init(&n,m);n.control=(n.control&~1u)|mask;
  if(!empty){n.tags=0;set(&n,0,(e87_real80_t){UINT64_C(0x8000000000000000),0x3ffd});}
  e87_t before=n;CHECK(e87_numeric(&n,0xd9,0xf2,0,0)==!mask);
  if(mask)CHECK(!memcmp(&n,&before,sizeof n));else CHECK((n.status&0x81)==0x81 && e87_top(&n)==0);
 }
}
int main(void)
{
 tangent_tests();
 static const e87_real80_t constants[]={
 {UINT64_C(0x8000000000000000),0x3fff},{UINT64_C(0xd49a784bcd1b8afe),0x4000},
 {UINT64_C(0xb8aa3b295c17f0bc),0x3fff},{UINT64_C(0xc90fdaa22168c235),0x4000},
 {UINT64_C(0x9a209a84fbcff799),0x3ffd},{UINT64_C(0xb17217f7d1cf79ac),0x3ffe},{0,0}};
 for(unsigned m=0;m<2;++m)for(unsigned t=0;t<8;++t)for(unsigned pc=0;pc<4;++pc)
 for(unsigned rc=0;rc<4;++rc)for(unsigned k=0;k<7;++k){
  e87_t n;e87_init(&n,m);n.status=(t<<11)|0x4700;n.control=(n.control&~0xf00u)|(pc<<8)|(rc<<10);
  CHECK(e87_numeric(&n,0xd9,0xe8+k,0,0));CHECK(e87_top(&n)==((t+7)&7));
  CHECK(equal(n.reg[e87_top(&n)],constants[k]));CHECK(e87_tag(&n,0)==(k==6?1:0));
  CHECK((n.status&0x47bf)==0x4700);
 }
 for(unsigned m=0;m<2;++m)for(unsigned mask=0;mask<2;++mask)for(unsigned k=0;k<7;++k){
  e87_t n;e87_init(&n,m);n.tags=0;n.control=(n.control&~1u)|mask;n.status=0x4700;
  CHECK(e87_numeric(&n,0xd9,0xe8+k,0,0));CHECK(n.status&1);CHECK(e87_top(&n)==(mask?7:0));
  CHECK((n.status&0x4700)==0x4700);if(mask)CHECK(n.reg[7].sign_exponent==0xffff&&e87_tag(&n,0)==2);
 }
 for(unsigned m=0;m<2;++m)for(unsigned k=0;k<sizeof vectors/sizeof *vectors;++k){
  const struct vector *v=&vectors[k];e87_t n;e87_init(&n,m);n.status=0x4700;
  n.control=(n.control&~0xc00u)|(v->rc<<10);set(&n,0,v->x);set(&n,1,v->y);
  CHECK(e87_numeric(&n,0xd9,v->op,0,0));if(!close80(n.reg[e87_top(&n)],v->result))fprintf(stderr,"vector %u op %x rc %u got %04x:%016llx want %04x:%016llx\n",k,v->op,v->rc,n.reg[e87_top(&n)].sign_exponent,(unsigned long long)n.reg[e87_top(&n)].significand,v->result.sign_exponent,(unsigned long long)v->result.significand);CHECK(close80(n.reg[e87_top(&n)],v->result));
  CHECK(e87_top(&n)==(v->op==0xf0?0:1));CHECK((n.status&0x4700)==0x4700);
 }
 const e87_real80_t bad[]={{1,0},{UINT64_C(0x8000000000000000),0x7fff},
 {UINT64_C(0xc000000000000000),0x7fff},{1,0x3fff}};
 const unsigned ops[]={0xf0,0xf1,0xf3,0xf9};
 for(unsigned m=0;m<2;++m)for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k){
  e87_t n;e87_init(&n,m);set(&n,0,bad[k]);set(&n,1,constants[0]);e87_t before=n;
  CHECK(!e87_numeric(&n,0xd9,ops[j],0,0));CHECK(!memcmp(&n,&before,sizeof n));
 }
 /* Pending exceptions block constants as well as transcendental operations. */
 for(unsigned m=0;m<2;++m){e87_t n;e87_init(&n,m);n.control&=~0x81u;e87_raise_exception(&n,1);e87_t before=n;
 CHECK(!e87_numeric(&n,0xd9,0xeb,0,0));CHECK(!memcmp(&n,&before,sizeof n));}

 /* 87TEST atan(1) compatibility boundary, both models and all TOPs. */
 for(unsigned m=0;m<2;++m)for(unsigned t=0;t<8;++t){
  e87_t n;e87_init(&n,m);n.status=t<<11;
  set(&n,0,(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff});
  set(&n,1,(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff});
  CHECK(e87_numeric(&n,0xd9,0xf3,0,0));CHECK(e87_top(&n)==((t+1)&7));
  CHECK(close80(n.reg[e87_top(&n)],(e87_real80_t){UINT64_C(0xc90fdaa22168c235),0x3ffe}));
 }
 /* Early ranges: rejects must preserve every byte, including pointers. */
 const struct {unsigned op;e87_real80_t x,y;} rejected[]={
 {0xf0,{UINT64_C(0x8000000000000001),0x3ffe},{0,0}},
 {0xf0,{UINT64_C(0x8000000000000000),0xbffe},{0,0}},
 {0xf1,{0,0},{UINT64_C(0x8000000000000000),0x3fff}},
 {0xf3,{UINT64_C(0x8000000000000000),0x3fff},{UINT64_C(0x8000000000000001),0x3fff}},
 {0xf9,{UINT64_C(0x95f619980c4336f8),0x3ffd},{UINT64_C(0x8000000000000000),0x3fff}},
 {0xf2,{UINT64_C(0xc90fdaa22168c235),0x3ffe},{0,0}}};
 for(unsigned m=0;m<2;++m)for(unsigned k=0;k<sizeof rejected/sizeof *rejected;++k){
  e87_t n;e87_init(&n,m);set(&n,0,rejected[k].x);set(&n,1,rejected[k].y);e87_t before=n;
  CHECK(!e87_numeric(&n,0xd9,rejected[k].op,0,0));CHECK(!memcmp(&n,&before,sizeof n));
 }
 /* PE is after-operation; full significand precision is independent of PC. */
 for(unsigned m=0;m<2;++m)for(unsigned pc=0;pc<4;++pc)for(unsigned mask=0;mask<2;++mask)
 for(unsigned t=0;t<8;++t)for(unsigned k=0;k<4;++k){
  e87_t n;e87_init(&n,m);n.status=(t<<11)|0x4700;n.control=(n.control&~0x320u)|(pc<<8)|(mask?0x20:0);
  set(&n,0,(e87_real80_t){UINT64_C(0x8000000000000000),0x3ffd});
  set(&n,1,(e87_real80_t){UINT64_C(0x8000000000000000),0x3ffc});
  /* FYL2X uses a non-power input so PE is unavoidable. */
  if(k==1)set(&n,0,(e87_real80_t){UINT64_C(0xc000000000000000),0x3fff});
  e87_t full=n;full.control=(full.control&~0x300u)|0x300;
  CHECK(e87_numeric(&full,0xd9,ops[k],0,0));
  CHECK(e87_numeric(&n,0xd9,ops[k],0,0));CHECK(n.status&0x20);
  CHECK(equal(n.reg[e87_top(&n)],full.reg[e87_top(&full)]));
  CHECK(e87_top(&n)==((t+(k!=0))&7));CHECK((n.status&0x4700)==0x4700);
  CHECK(!!(n.status&0x80)==!mask);
 }

 /* Intel 287 table 1-10 preserves F2XM1 negative zero. */
 for(unsigned m=0;m<2;++m)for(unsigned rc=0;rc<4;++rc){
  e87_t n;e87_init(&n,m);n.control=(n.control&~0xc00u)|(rc<<10);
  set(&n,0,(e87_real80_t){0,0x8000});CHECK(e87_numeric(&n,0xd9,0xf0,0,0));
  CHECK(n.reg[0].sign_exponent==0x8000 && !n.reg[0].significand && !(n.status&0x3f));
 }
 /* min_normal * ln(2); the quadratic term is far below a destination ulp.
  * High-precision mathematical result: 0x58b90bfbe8e7bcd5. */
 for(unsigned m=0;m<2;++m)for(unsigned rc=0;rc<4;++rc)for(unsigned pm=0;pm<2;++pm)
 for(unsigned pc=0;pc<4;++pc){
  e87_t n;e87_init(&n,m);n.status=0x4700;
  n.control=(n.control&~0xf20u)|(rc<<10)|(pc<<8)|(pm?0x20:0);
  set(&n,0,(e87_real80_t){UINT64_C(0x8000000000000000),1});
  CHECK(e87_numeric(&n,0xd9,0xf0,0,0));
  CHECK(n.reg[0].significand==UINT64_C(0x58b90bfbe8e7bcd5)+(rc==0||rc==2));
  CHECK(n.reg[0].sign_exponent==0 && e87_tag(&n,0)==2);
  CHECK((n.status&0x4730)==0x4730);CHECK(!!(n.status&0x80)==!pm);
  e87_init(&n,m);n.control&=~0x10u;set(&n,0,(e87_real80_t){UINT64_C(0x8000000000000000),1});
  n.status=0x4700;n.control=(n.control&~0xf30u)|(rc<<10)|(pc<<8)|(pm?0x20:0);
  CHECK(e87_numeric(&n,0xd9,0xf0,0,0));
  CHECK(n.reg[0].significand==UINT64_C(0xb17217f7d1cf79ab));
  CHECK(n.reg[0].sign_exponent==0x6000 && e87_tag(&n,0)==0);
  CHECK((n.status&0x47bf)==0x4790); /* UE/ES, no rounding-induced PE. */
  CHECK(e87_top(&n)==0);
 }
 /* Independent Decimal(precision=120) oracle: floor(2*input_sig*ln(2)).
  * For these min-exponent inputs, the quadratic term cannot affect a bit.
  * These are mathematical approximation tests, not physical-chip captures. */
 const uint64_t tiny[][2]={
  {UINT64_C(0x8000000000000001),UINT64_C(0xb17217f7d1cf79ad)},
  {UINT64_C(0x9000000000000000),UINT64_C(0xc7a05af6cc0968e1)},
  {UINT64_C(0xb000000000000000),UINT64_C(0xf3fce0f4c07d474c)}};
 for(unsigned m=0;m<2;++m)for(unsigned rc=0;rc<4;++rc)
 for(unsigned pc=0;pc<4;++pc)for(unsigned t=0;t<8;++t)
 for(unsigned k=0;k<3;++k){
  e87_t n;e87_init(&n,m);n.status=(t<<11)|0x4700;
  n.control=(n.control&~0xf10u)|(rc<<10)|(pc<<8);
  set(&n,0,(e87_real80_t){tiny[k][0],1});
  CHECK(e87_numeric(&n,0xd9,0xf0,0,0));
  CHECK(n.reg[t].significand==tiny[k][1] && n.reg[t].sign_exponent==0x6000);
  CHECK(e87_top(&n)==t && e87_tag(&n,0)==0);
  CHECK((n.status&0x47bf)==0x4790);
 }
 /* FPATAN(min_normal/max_normal): positive, smaller than any denormal.
  * Masked RC-up returns the smallest denormal; other modes return +0.
  * Unmasked result uses the wrapped exponent and truncated significand.
  * The tiny cubic correction cannot change the retained bits for this ratio. */
 for(unsigned m=0;m<2;++m)for(unsigned rc=0;rc<4;++rc)
 for(unsigned pc=0;pc<4;++pc)for(unsigned t=0;t<8;++t)
 for(unsigned masked=0;masked<2;++masked){
  e87_t n;e87_init(&n,m);n.status=(t<<11)|0x4700;
  n.control=(n.control&~0xf10u)|(rc<<10)|(pc<<8)|(masked?0x10:0);
  set(&n,0,(e87_real80_t){UINT64_MAX,0x7ffe});
  set(&n,1,(e87_real80_t){UINT64_C(0x8000000000000000),1});
  CHECK(e87_numeric(&n,0xd9,0xf3,0,0));
  unsigned dest=(t+1)&7;
  CHECK(e87_top(&n)==dest && ((n.tags>>(2*t))&3)==3);
  CHECK(n.reg[dest].significand==(masked?(rc==2):UINT64_C(0x8000000000000000)));
  CHECK(n.reg[dest].sign_exponent==(masked?0:0x2001));
  CHECK((n.status&0x47bf)==(masked?0x4730:0x4790));
 }
 /* FPATAN(min_normal/3): nonzero gradual underflow. At this scale,
  * atan's cubic term cannot cross the /3 rounding boundary. */
 for(unsigned m=0;m<2;++m)for(unsigned rc=0;rc<4;++rc)
 for(unsigned pc=0;pc<4;++pc)for(unsigned masked=0;masked<2;++masked)
 for(unsigned pm=0;pm<2;++pm){
  e87_t n;e87_init(&n,m);n.status=0x4700;
  n.control=(n.control&~0xf30u)|(rc<<10)|(pc<<8)|(masked?0x10:0)|(pm?0x20:0);
  set(&n,0,(e87_real80_t){UINT64_C(0xc000000000000000),0x4000});
  set(&n,1,(e87_real80_t){UINT64_C(0x8000000000000000),1});
  CHECK(e87_numeric(&n,0xd9,0xf3,0,0));
  CHECK(n.reg[1].significand==(masked?UINT64_C(0x2aaaaaaaaaaaaaaa)+(rc==0||rc==2):UINT64_C(0xaaaaaaaaaaaaaaaa)));
  CHECK(n.reg[1].sign_exponent==(masked?0:0x5fff));
  CHECK(e87_top(&n)==1 && e87_tag(&n,0)==(masked?2:0));
  CHECK((n.status&0x473f)==(masked?0x4730:0x4710));
  CHECK(!!(n.status&0x80)==(!masked||!pm));
 }
 printf("NPX constants/transcendentals: %u checks, %u failures\n",checks,failures);return failures!=0;
}
