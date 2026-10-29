/* PCE early-NPX transcendental numerical approximation. GPL-2.0-or-later.
 * 120-bit software significands, independent of host floating point. This is
 * NOT a reconstruction of Intel microcode or a bit-identical chip oracle.
 * Only normal/zero results are accepted; historical range behavior defers.
 */
#include "transcendental.h"
typedef unsigned __int128 wide;
typedef struct {wide m; int e; unsigned s;} number;
#define BIT119 ((wide)1<<119)
static number norm(number a)
{
    if(!a.m){a.e=0;return a;}
    while(a.m<BIT119){a.m<<=1;--a.e;}
    while(a.m>=BIT119*2){a.m>>=1;++a.e;}
    return a;
}
static number integer(int a)
{return norm((number){a<0?-(wide)a:(wide)a,119,a<0});}
static number unpack(e87_real80_t a)
{return (number){(wide)a.significand<<56,(int)(a.sign_exponent&0x7fff)-16383,a.sign_exponent>>15};}
static number negative(number a){a.s^=1;return a;}
static number add(number a,number b)
{
    if(!a.m)return b;if(!b.m)return a;
    if(a.e<b.e){number t=a;a=b;b=t;}
    int d=a.e-b.e;wide m=d>=120?0:b.m>>d;
    if(a.s==b.s)a.m+=m;
    else if(a.m>=m)a.m-=m;
    else {a.m=m-a.m;a.s=b.s;}
    return norm(a);
}
static number mul(number a,number b)
{
    /* Four 64-bit limbs hold the exact 240-bit product. */
    uint64_t av[2]={(uint64_t)a.m,(uint64_t)(a.m>>64)};
    uint64_t bv[2]={(uint64_t)b.m,(uint64_t)(b.m>>64)},v[4]={0};
    for(unsigned i=0;i<2;++i){wide carry=0;
        for(unsigned j=0;j<2;++j){wide t=(wide)av[i]*bv[j]+v[i+j]+carry;v[i+j]=t;carry=t>>64;}
        v[i+2]=carry;
    }
    wide m=((wide)v[3]<<73)|((wide)v[2]<<9)|(v[1]>>55);
    return norm((number){m,a.e+b.e,a.s^b.s});
}
static number divide(number a,number b)
{
    if(!a.m)return a;
    wide rem=a.m,q=0;
    for(unsigned i=0;i<120;++i){q<<=1;if(rem>=b.m){rem-=b.m;q|=1;}rem<<=1;}
    return norm((number){q,a.e-b.e,a.s^b.s});
}
static int same(number a,number b)
{return a.m==b.m&&a.e==b.e&&a.s==b.s;}
static number ln2(void)
{return (number){((wide)UINT64_C(0xb17217f7d1cf79)<<64)|UINT64_C(0xabc9e3b39803f2f6),-1,0};}
static number pi4(void)
{return (number){((wide)UINT64_C(0xc90fdaa22168c2)<<64)|UINT64_C(0x34c4c6628b80dc1c),-1,0};}
/* log(1+x)=2*(z+z^3/3+...), z=x/(2+x). Retains tiny x. */
static number log1p(number x)
{
    number z=divide(x,add(integer(2),x)),zz=mul(z,z),term=z,sum=z;
    for(unsigned k=3;k<401;k+=2){term=mul(term,zz);number next=add(sum,divide(term,integer(k)));
        if(same(sum,next))break;sum=next;}
    ++sum.e;return sum;
}
static number log2n(number x)
{
    int exponent=x.e;x.e=0;
    /* Reduce to [sqrt(1/2),sqrt(2)] to avoid cancellation near one. */
    if(x.m>((wide)UINT64_C(0xb504f333f9de64)<<64|UINT64_C(0x84597d89b3754abe))){--x.e;++exponent;}
    return add(integer(exponent),divide(log1p(add(x,negative(integer(1)))),ln2()));
}
static number expm1(number x)
{
    number term=x,sum=x;
    for(unsigned k=2;k<201;++k){term=divide(mul(term,x),integer(k));number next=add(sum,term);
        if(same(sum,next))break;sum=next;}
    return sum;
}
static number atan_small(number x)
{
    number xx=mul(x,x),term=x,sum=x;
    for(unsigned k=3;k<401;k+=2){term=negative(mul(term,xx));number next=add(sum,divide(term,integer(k)));
        if(same(sum,next))break;sum=next;}
    return sum;
}
static number atan_ratio(number x)
{
    number half=integer(1);--half.e;
    if(x.e<half.e || (x.e==half.e&&x.m<=half.m))return atan_small(x);
    return add(pi4(),atan_small(divide(add(x,negative(integer(1))),add(x,integer(1)))));
}
static int pack(number a,unsigned rc,e87_real80_t *r)
{
    if(!a.m){*r=(e87_real80_t){0,a.s<<15};return 1;}
    if(a.e < -16382 || a.e>16383)return 0;
    uint64_t sig=a.m>>56;wide rem=a.m&(((wide)1<<56)-1),half=(wide)1<<55;
    unsigned up=rc==0?(rem>half||(rem==half&&(sig&1))):rc==1?a.s&&rem:rc==2?!a.s&&rem:0;
    if(up && ++sig==0){sig=UINT64_C(0x8000000000000000);++a.e;}
    if(a.e>16383)return 0;
    *r=(e87_real80_t){sig,(a.s<<15)|(a.e+16383)};return 1;
}
int e87_transcendental(unsigned mr,e87_real80_t x,e87_real80_t y,unsigned rc,e87_real80_t *r)
{
    number a=unpack(x),b=unpack(y),v;
    switch(mr){
    case 0xf0:v=expm1(mul(a,ln2()));break;
    case 0xf1:v=mul(b,log2n(a));break;
    case 0xf3:v=atan_ratio(divide(b,a));break;
    case 0xf9:v=mul(b,divide(log1p(a),ln2()));break;
    default:return 0;
    }
    return pack(v,rc,r);
}
