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
