/* PCE NPX arithmetic adapter. GPL-2.0-or-later. */
#include "arithmetic.h"
#include "softfloat/platform.h"
#include "softfloat/softfloat.h"
int e87_add80(e87_real80_t a,e87_real80_t b,unsigned precision,
             unsigned rounding,e87_real80_t *result,unsigned *flags)
{
    static const unsigned modes[]={softfloat_round_near_even,softfloat_round_minMag,
                                  softfloat_round_min,softfloat_round_max};
    extFloat80_t x,y,z;
    uint_fast8_t old_mode,old_flags,old_tiny,old_precision;
    if ((precision!=32 && precision!=64 && precision!=80) || rounding>3 ||
        result==0 || flags==0) return 0;
    old_mode=softfloat_roundingMode;old_flags=softfloat_exceptionFlags;
    old_tiny=softfloat_detectTininess;old_precision=extF80_roundingPrecision;
    softfloat_roundingMode=modes[rounding];softfloat_exceptionFlags=0;
    softfloat_detectTininess=softfloat_tininess_beforeRounding;
    extF80_roundingPrecision=precision;
    x.signif=a.significand;x.signExp=a.sign_exponent;
    y.signif=b.significand;y.signExp=b.sign_exponent;
    z=extF80_add(x,y);
    result->significand=z.signif;result->sign_exponent=z.signExp;
    *flags=softfloat_exceptionFlags;
    softfloat_roundingMode=old_mode;softfloat_exceptionFlags=old_flags;
    softfloat_detectTininess=old_tiny;extF80_roundingPrecision=old_precision;
    return 1;
}

/* Integer work area: 64 significand bits plus 63 guard bits and one carry.
 * The guard span also covers cancellation against a differently scaled
 * unnormal. Right shifts retain a sticky bit; no host floating point is used. */
typedef unsigned __int128 e87_wide_t;
static e87_wide_t align_unnormal(uint64_t sig, unsigned shift)
{
    e87_wide_t v = (e87_wide_t)sig << 63;
    if (!shift) return v;
    if (shift >= 128) return v != 0;
    return (v >> shift) | ((v & (((e87_wide_t)1 << shift)-1)) != 0);
}
/* Intel 287 PRM table 1-11. This covers a NORMAL true result only.
 * The published unnormal rows are contradictory and remain unsupported. */
static int masked_overflow(unsigned sign,unsigned rounding,unsigned width,
                           unsigned control,e87_real80_t *result,unsigned *status)
{
    unsigned to_infinity=rounding<2 || (rounding==2 ? sign : !sign);
    e87_real80_t value;
    if(to_infinity) {
        value=(e87_real80_t){UINT64_C(0x8000000000000000),(sign<<15)|0x7fff};
    } else {
        /* Table 1-11 explicitly ends the significand in ...10, not ...11.
         * Restrict finite saturation to the documented full temporary format;
         * reduced-PC finite saturation awaits a separate hardware check. */
        if(width!=64) return 0;
        value=(e87_real80_t){UINT64_MAX-1,(sign<<15)|0x7ffe};
    }
    *result=value;
    /* Early NPX suppresses OE when its masked response remains finite. */
    *status=0x20|(to_infinity?8:0)|((control&0x20)?0:0x80);
    return 1;
}
static int add_early(e87_real80_t a,e87_real80_t b,unsigned precision,
                     unsigned rounding,e87_real80_t *result,unsigned *flags,
                     int range_only,unsigned control)
{
    unsigned width = precision == 32 ? 24 : precision == 64 ? 53 : 64;
    unsigned ea=a.sign_exponent&0x7fff, eb=b.sign_exponent&0x7fff;
    unsigned sa=a.sign_exponent>>15, sb=b.sign_exponent>>15, sign, normal;
    int exponent;
    e87_wide_t x,y,z,one=(e87_wide_t)1;
    if (!result || !flags || rounding>3 ||
        (precision!=32 && precision!=64 && precision!=80) || ea==0x7fff || eb==0x7fff) return 0;
    /* A denormal's equivalent unnormal has biased exponent 1. */
    if (!ea) ea=1;
    if (!eb) eb=1;
    /* A zero payload must not force a nonzero operand out of the work area. */
    if (!a.significand && b.significand) ea=eb;
    if (!b.significand && a.significand) eb=ea;
    exponent=ea>eb?ea:eb;
    x=align_unnormal(a.significand,exponent-ea);
    y=align_unnormal(b.significand,exponent-eb);
    normal = x>y ? a.significand>>63 : y>x ? b.significand>>63 :
             (a.significand|b.significand)>>63;
    if(sa==sb) { z=x+y;sign=sa; }
    else { z=x>=y?x-y:y-x;sign=x>=y?sa:sb; }
    if(!z) {
        sign=sa==sb?sa:rounding==2;
        e87_real80_t zero={0,(sign<<15)|(normal?0:exponent)};
        if (range_only) return 0;
        *result=zero;*flags=0;return 1;
    }
    if(z&(one<<127)) { z=(z>>1)|(z&1);++exponent; }
    if(normal) while(!(z&(one<<126))) {z<<=1;--exponent;}
    if(exponent<1 || exponent>=0x7fff) {
        if (!range_only) return 0;
        unsigned exception=exponent<1?0x10:0x08;
        e87_real80_t value;
        if (!(control & exception)) {
            /* Early 287 register response: adjusted exponent, no rounding/PE. */
            exponent += exception==0x10 ? 24576 : -24576;
            if(exponent<1 || exponent>=0x7fff) return 0;
            value=(e87_real80_t){(uint64_t)(z>>63),(sign<<15)|exponent};
            *result=value;*flags=exception|0x80;return 1;
        }
        if(exception==0x08) {
            if (!(z & (one<<126))) return 0;
            return masked_overflow(sign,rounding,width,control,result,flags);
        }
        /* Masked underflow is rounded to the temporary-real 64-bit format,
         * irrespective of PC. Preserve a sticky bit during gradual underflow. */
        unsigned shift=1-exponent;
        z=shift>=128 ? z!=0 : (z>>shift)|((z&((one<<shift)-1))!=0);
        e87_wide_t rem=z&((one<<63)-1),half=one<<62,kept=z>>63;
        unsigned up=rounding==0 ? rem>half || (rem==half && (kept&1)) :
                    rounding==2 ? sign && rem : rounding==3 ? !sign && rem : 0;
        kept+=up;
        value=(e87_real80_t){(uint64_t)kept,(sign<<15)|((kept&(one<<63))?1:0)};
        unsigned status=(rounding==1 || rem)?0x10:0;
        if(rem) status|=0x20|((control&0x20)?0:0x80);
        if(up) status|=0x200;
        *result=value;*flags=status;return 1;
    }
    unsigned cut=63+64-width;
    e87_wide_t rem=z&((one<<cut)-1),half=one<<(cut-1),kept=z>>cut;
    unsigned increment=rounding==0 ? rem>half || (rem==half && (kept&1)) :
                       rounding==2 ? sign && rem : rounding==3 ? !sign && rem : 0;
    kept+=increment;
    if(kept==(one<<width)) {kept>>=1;++exponent;}
    if(exponent>=0x7fff) {
        /* Masked carry overflow has the same delivered value as masked true
         * overflow. Unmasked carry ordering is not inferred from the later x87. */
        if (!range_only || !(control&8)) return 0;
        if (!masked_overflow(sign,rounding,width,control,result,flags)) return 0;
        *flags |= 0x200; /* This path actually incremented the significand. */
        return 1;
    }
    if(range_only) return 0;
    e87_real80_t value={(uint64_t)(kept<<(64-width)),(sign<<15)|exponent};
    *result=value;*flags=rem?E87_ARITH_INEXACT:0;return 1;
}

int e87_add80_unnormal(e87_real80_t a,e87_real80_t b,unsigned precision,
                      unsigned rounding,e87_real80_t *result,unsigned *flags)
{
    return add_early(a,b,precision,rounding,result,flags,0,0);
}
int e87_add80_range(e87_real80_t a,e87_real80_t b,unsigned precision,
                   unsigned rounding,unsigned control,e87_real80_t *result,unsigned *status)
{
    return add_early(a,b,precision,rounding,result,status,1,control);
}

/* Exact integer product or quotient/remainder, rounded once at selected PC.
 * Early NPX exceptional results are deliberately outside this finite adapter. */
static int product80(e87_real80_t a,e87_real80_t b,unsigned precision,
                     unsigned rounding,e87_real80_t *result,unsigned *flags,int divide)
{
    unsigned ea=a.sign_exponent&0x7fff, eb=b.sign_exponent&0x7fff;
    unsigned sign=(a.sign_exponent^b.sign_exponent)&0x8000;
    unsigned width=precision==32?24:precision==64?53:64, shift;
    e87_wide_t q, rem, denominator, product;
    int exponent;
    if(!result || !flags || (precision!=32 && precision!=64 && precision!=80) || rounding>3) return 0;
    if((ea==0 ? a.significand!=0 : ea==0x7fff || !(a.significand>>63)) ||
       (eb==0 ? b.significand!=0 : eb==0x7fff || !(b.significand>>63))) return 0;
    if(divide && !b.significand) return 0;
    if(!a.significand || (!divide && !b.significand)) {
        *result=(e87_real80_t){0,sign};*flags=0;return 1;
    }
    if(divide) {
        unsigned smaller=a.significand<b.significand;
        exponent=(int)ea-(int)eb+0x3fff-smaller;
        product=(e87_wide_t)a.significand<<(width-1+smaller);
        denominator=b.significand;q=product/denominator;rem=product%denominator;
    } else {
        product=(e87_wide_t)a.significand*b.significand;
        unsigned carry=(unsigned)(product>>127);
        exponent=(int)ea+(int)eb-0x3fff+carry;
        shift=127+carry-width;denominator=(e87_wide_t)1<<shift;
        q=product>>shift;rem=product&(denominator-1);
    }
    if(exponent<=0 || exponent>=0x7fff) return 0;
    unsigned increment=rem && ((rounding==0 &&
        (rem>denominator/2 || (rem*2==denominator && (q&1)))) ||
        (rounding==2 && sign) || (rounding==3 && !sign));
    q+=increment;
    if(q==((e87_wide_t)1<<width)) {q>>=1;++exponent;}
    if(exponent>=0x7fff) return 0;
    *result=(e87_real80_t){(uint64_t)q<<(64-width),sign|exponent};
    *flags=rem?E87_ARITH_INEXACT:0;
    return 1;
}
int e87_mul80(e87_real80_t a,e87_real80_t b,unsigned p,unsigned r,e87_real80_t *v,unsigned *f)
{ return product80(a,b,p,r,v,f,0); }
int e87_div80(e87_real80_t a,e87_real80_t b,unsigned p,unsigned r,e87_real80_t *v,unsigned *f)
{ return product80(a,b,p,r,v,f,1); }

/* Shared early-NPX range and precision response for product/quotient/scale.
 * z has 64 significand bits and 63 guard/sticky bits. */
static int finish_work(e87_wide_t z,int exponent,unsigned sign,unsigned width,
                       unsigned rounding,unsigned control,e87_real80_t *result,unsigned *flags)
{
    e87_wide_t one=(e87_wide_t)1;
    if(exponent<1 || exponent>=0x7fff) {

        unsigned exception=exponent<1?0x10:0x08;
        e87_real80_t value;
        if (!(control & exception)) {
            /* Early 287 register response: adjusted exponent, no rounding/PE. */
            exponent += exception==0x10 ? 24576 : -24576;
            if(exponent<1 || exponent>=0x7fff) return 0;
            value=(e87_real80_t){(uint64_t)(z>>63),(sign<<15)|exponent};
            *result=value;*flags=exception|0x80;return 1;
        }
        if(exception==0x08) {
            if (!(z & (one<<126))) return 0;
            return masked_overflow(sign,rounding,width,control,result,flags);
        }
        /* Masked underflow is rounded to the temporary-real 64-bit format,
         * irrespective of PC. Preserve a sticky bit during gradual underflow. */
        unsigned shift=1-exponent;
        z=shift>=128 ? z!=0 : (z>>shift)|((z&((one<<shift)-1))!=0);
        e87_wide_t rem=z&((one<<63)-1),half=one<<62,kept=z>>63;
        unsigned up=rounding==0 ? rem>half || (rem==half && (kept&1)) :
                    rounding==2 ? sign && rem : rounding==3 ? !sign && rem : 0;
        kept+=up;
        value=(e87_real80_t){(uint64_t)kept,(sign<<15)|((kept&(one<<63))?1:0)};
        unsigned status=(rounding==1 || rem)?0x10:0;
        if(rem) status|=0x20|((control&0x20)?0:0x80);
        if(up) status|=0x200;
        *result=value;*flags=status;return 1;
    }
    unsigned cut=63+64-width;
    e87_wide_t rem=z&((one<<cut)-1),half=one<<(cut-1),kept=z>>cut;
    unsigned increment=rounding==0 ? rem>half || (rem==half && (kept&1)) :
                       rounding==2 ? sign && rem : rounding==3 ? !sign && rem : 0;
    kept+=increment;
    if(kept==(one<<width)) {kept>>=1;++exponent;}
    if(exponent>=0x7fff) {
        /* Masked carry overflow has the same delivered value as masked true
         * overflow. Unmasked carry ordering is not inferred from the later x87. */
        if (!(control&8)) return 0;
        if (!masked_overflow(sign,rounding,width,control,result,flags)) return 0;
        *flags |= 0x200; /* This path actually incremented the significand. */
        return 1;
    }

    e87_real80_t value={(uint64_t)(kept<<(64-width)),(sign<<15)|exponent};
    *result=value;*flags=(rem?0x20:0)|(increment?0x200:0);return 1;
}
int e87_product80(e87_real80_t a,e87_real80_t b,int divide,unsigned precision,
                  unsigned rounding,unsigned control,e87_real80_t *result,unsigned *flags)
{
    int ea=a.sign_exponent&0x7fff,eb=b.sign_exponent&0x7fff;
    unsigned sign=(a.sign_exponent^b.sign_exponent)>>15;
    unsigned width=precision==32?24:precision==64?53:64;
    e87_wide_t z, numerator, rem, tail;int exponent;
    if(!result || !flags || rounding>3 || (precision!=32 && precision!=64 && precision!=80) ||
       ea==0x7fff || eb==0x7fff || (divide && !(b.significand>>63))) return 0;
    if(!ea) ea=1;
    if(!eb) eb=1;
    if(divide) {
        exponent=ea-eb+0x3fff;
        unsigned shift=63;
        if((a.significand>>63) && a.significand<b.significand) {++shift;--exponent;}
        numerator=(e87_wide_t)a.significand<<shift;
        z=(numerator/b.significand)<<63;rem=numerator%b.significand;
        tail=(rem<<63)/b.significand;
        z|=tail|(((rem<<63)%b.significand)!=0);
    } else {
        exponent=ea+eb-0x3fff;
        z=(e87_wide_t)a.significand*b.significand;
        if(z>>127) {z=(z>>1)|(z&1);++exponent;}
    }
    if(!z) {
        /* True zero gives true zero; pseudozero retains its unnormal scale. */
        unsigned pseudo=(a.significand==0 && (a.sign_exponent&0x7fff)) ||
                        (b.significand==0 && (b.sign_exponent&0x7fff));
        if(pseudo && (exponent<1 || exponent>=0x7fff)) return 0;
        *result=(e87_real80_t){0,(sign<<15)|(pseudo?exponent:0)};*flags=0;return 1;
    }
    return finish_work(z,exponent,sign,width,rounding,control,result,flags);
}

/* Exact integer square root of a 128-bit radicand. The remainder is retained
 * for rounding at the requested precision, avoiding double rounding. */
int e87_sqrt80(e87_real80_t a,unsigned precision,unsigned rounding,e87_real80_t *r,unsigned *flags)
{
    unsigned width=precision==32?24:precision==64?53:64;
    int e=(a.sign_exponent&0x7fff)-0x3fff;
    if(!r || !flags || rounding>3 || (precision!=32 && precision!=64 && precision!=80) ||
       a.sign_exponent>>15 || !(a.significand>>63) || (a.sign_exponent&0x7fff)==0x7fff) return 0;
    unsigned odd=(e%2)!=0;
    e87_wide_t original=(e87_wide_t)a.significand<<(63+odd),rem=original,root=0,bit=(e87_wide_t)1<<126;
    while(bit>rem) bit>>=2;
    while(bit) {
        if(rem>=root+bit) {rem-=root+bit;root=(root>>1)+bit;}
        else root>>=1;
        bit>>=2;
    }
    unsigned cut=64-width;
    e87_wide_t q=root>>cut,low=root&(((e87_wide_t)1<<cut)-1);
    unsigned inexact=rem || low,up=0;
    if(rounding==0) {
        if(!cut) up=rem>root;
        else {
            e87_wide_t mid=(q<<cut)+((e87_wide_t)1<<(cut-1));
            up=original>mid*mid || (original==mid*mid && (q&1));
        }
    } else if(rounding==3) up=inexact;
    q+=up;e=(e-(int)odd)/2+0x3fff;
    if(q==((e87_wide_t)1<<width)) {q>>=1;++e;}
    *r=(e87_real80_t){(uint64_t)q<<cut,e};*flags=(inexact?0x20:0)|(up?0x200:0);return 1;
}
int e87_scale80(e87_real80_t a,int scale,unsigned rounding,unsigned control,e87_real80_t *r,unsigned *flags)
{
    if(!r || !flags || rounding>3 || scale < -32768 || scale>32767) return 0;
    if(!a.significand) {*r=a;*flags=0;return 1;}
    int e=a.sign_exponent&0x7fff;if(!e)e=1;
    return finish_work((e87_wide_t)a.significand<<63,e+scale,a.sign_exponent>>15,64,rounding,control,r,flags);
}
