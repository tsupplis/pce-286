/* Early NPX constants/transcendental tests. GPL-2.0-or-later. */
#include <cpu/e8087/execute.h>
#include <cpu/e8086/e8086.h>
#include <stdio.h>
#include <string.h>
#include "npx_trans_vectors.h"
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
int main(void)
{
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

 /* Early ranges: rejects must preserve every byte, including pointers. */
 const struct {unsigned op;e87_real80_t x,y;} rejected[]={
 {0xf0,{UINT64_C(0x8000000000000001),0x3ffe},{0,0}},
 {0xf0,{UINT64_C(0x8000000000000000),0xbffe},{0,0}},
 {0xf1,{0,0},{UINT64_C(0x8000000000000000),0x3fff}},
 {0xf3,{UINT64_C(0x8000000000000000),0x3fff},{UINT64_C(0x8000000000000000),0x3fff}},
 {0xf9,{UINT64_C(0x95f619980c4336f8),0x3ffd},{UINT64_C(0x8000000000000000),0x3fff}},
 /* Valid input, but a result requiring historical underflow handling. */
 {0xf0,{UINT64_C(0x8000000000000000),1},{0,0}},
 {0xf2,{UINT64_C(0x8000000000000000),0x3ffd},{0,0}}};
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
  CHECK(e87_numeric(&n,0xd9,ops[k],0,0));CHECK(n.status&0x20);
  CHECK(e87_top(&n)==((t+(k!=0))&7));CHECK((n.status&0x4700)==0x4700);
  CHECK(!!(n.status&0x80)==!mask);
 }
 printf("NPX constants/transcendentals: %u checks, %u failures\n",checks,failures);return failures!=0;
}
