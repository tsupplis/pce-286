/* PCE NPX numeric execution foundation. GPL-2.0-or-later. */
#include "execute.h"
#include "arithmetic.h"
#include "transcendental.h"
#include <cpu/e8086/e8086.h> /* Callback event identifiers only. */
static int finite(e87_real80_t x)
{
    unsigned e = x.sign_exponent & 0x7fff;
    return (e == 0 && x.significand == 0) ||
           (e != 0 && e != 0x7fff && (x.significand >> 63));
}
/* Early 287 NaNs include every all-ones exponent except infinity. Their
 * payload is propagated unchanged, not quieted using the later IEEE policy. */
static int nan80(e87_real80_t x)
{
    return (x.sign_exponent & 0x7fff) == 0x7fff &&
           x.significand != UINT64_C(0x8000000000000000);
}
static int infinity80(e87_real80_t x)
{
    return (x.sign_exponent & 0x7fff) == 0x7fff &&
           x.significand == UINT64_C(0x8000000000000000);
}
static int denormal80(e87_real80_t x)
{
    /* Early NPX treats exponent-zero nonzero operands as denormal even when
     * the explicit integer bit is one (287 PRM Appendix C). */
    return !(x.sign_exponent & 0x7fff) && x.significand;
}
static int unnormal80(e87_real80_t x)
{
    unsigned e = x.sign_exponent & 0x7fff;
    return e && e != 0x7fff && !(x.significand >> 63);
}
static void tag(e87_t *n, unsigned i, unsigned value)
{
    n->tags = (n->tags & ~(3u << (2*i))) | (value << (2*i));
}
static void top(e87_t *n, unsigned value)
{
    n->status = (n->status & ~0x3a00u) | ((value & 7) << 11);
}
static const e87_real80_t indefinite = {UINT64_C(0xc000000000000000), 0xffff};
static int is_indefinite(e87_real80_t x)
{
    return x.significand == indefinite.significand && x.sign_exponent == indefinite.sign_exponent;
}
/* 1 masked default, 2 unmasked (before-operation exception).
 * Bit 6 is RESERVED on 8087/287. Do not invent the 387 SF/C1 diagnostics. */
static int stack_invalid(e87_t *n)
{
    e87_raise_exception(n, 1);
    if (!(n->control & 1)) return 2;
    return 1;
}
static void invalid_result(e87_t *n, unsigned dest, unsigned new_top)
{
    unsigned c1 = n->status & 0x200;
    n->reg[dest] = indefinite;
    tag(n, dest, 2);
    top(n, new_top);
    n->status |= c1; /* Undefined condition codes retained on invalid operation. */
}
/* Exact memory decode: retain denormals as equivalent unnormals so the
 * early arithmetic normalization rules still apply after widening. */
static unsigned arithmetic_memory_size(unsigned op,unsigned mr)
{
    unsigned g=(mr>>3)&7;
    if(mr>=0xc0 || g==2 || g==3) return 0;
    return op==0xd8 || op==0xda ? 4 : op==0xdc ? 8 : op==0xde ? 2 : 0;
}
static e87_real80_t integer80(uint64_t magnitude,unsigned sign)
{
    unsigned e=0;
    if(!magnitude) return (e87_real80_t){0,sign<<15};
    uint64_t v=magnitude;
    while(v>>1) {v>>=1;++e;}
    return (e87_real80_t){magnitude<<(63-e),(sign<<15)|(0x3fff+e)};
}
enum { E87_REAL, E87_INTEGER, E87_BCD };
static uint64_t read_le(const unsigned char *p,unsigned size)
{
    uint64_t v=0;for(unsigned k=0;k<size;++k)v|=(uint64_t)p[k]<<(8*k);return v;
}
static void write_le(unsigned char *p,unsigned size,uint64_t v)
{ for(unsigned k=0;k<size;++k)p[k]=v>>(8*k); }
static e87_real80_t decode_operand(const unsigned char *p,unsigned size,unsigned kind,unsigned *de)
{
    uint64_t raw=read_le(p,size);*de=0;
    if(kind==E87_INTEGER) {
        unsigned sign=raw>>(size*8-1);uint64_t mask=size==8?UINT64_MAX:(UINT64_C(1)<<(size*8))-1;
        return integer80(sign?(-raw)&mask:raw,sign);
    }
    unsigned f=size==4?23:52,bias=size==4?127:1023,emax=2*bias+1;
    unsigned sign=raw>>(size*8-1),e=(raw>>f)&emax;
    uint64_t frac=raw&((UINT64_C(1)<<f)-1);
    e87_real80_t v={(frac|(e?UINT64_C(1)<<f:0))<<(63-f),sign<<15};
    if(e==emax)v.sign_exponent|=0x7fff;
    else if(e || frac)v.sign_exponent|=(e?e:1)+0x3fff-bias;
    *de=!e && frac;return v;
}
static int memory_arithmetic(e87_t *n,unsigned op,unsigned mr,const unsigned char *bytes)
{
    unsigned size=arithmetic_memory_size(op,mr),de=0,t=e87_top(n),slot=(t+1)&7;
    if(!size || !bytes) return 0;
    e87_real80_t value=decode_operand(bytes,size,(op==0xda || op==0xde)?E87_INTEGER:E87_REAL,&de);
    /* Use a private scratch register and existing register arithmetic. Restore
     * it before committing; no guest stack slot is consumed or overwritten. */
    e87_t next=*n;
    if(de && e87_tag(n,0)!=3) {
        if(!(n->control&2)) {e87_raise_exception(n,2);return 1;}
        next.status|=2;
    }
    next.reg[slot]=value;
    tag(&next,slot,(nan80(value)||infinity80(value))?2:unnormal80(value)?0:value.significand?0:1);
    if(!e87_numeric(&next,0xd8,0xc1|(mr&0x38),0,0)) return 0;
    next.reg[slot]=n->reg[slot];
    tag(&next,slot,(n->tags>>(2*slot))&3);
    *n=next;return 1;
}
static int other_arithmetic_opcode(unsigned op,unsigned mr)
{
    return op==0xd9 && (mr==0xe0 || mr==0xe1 || mr==0xf4 || mr==0xf8 ||
                       mr==0xfa || mr==0xfc || mr==0xfd);
}
static void result80(e87_t *n,unsigned slot,e87_real80_t v)
{
    n->reg[slot]=v;
    tag(n,slot,(nan80(v)||infinity80(v)||denormal80(v))?2:unnormal80(v)?0:v.significand?0:1);
}
/* Work on a staged state: historical undefined operands must not leave partial
 * flags, stack changes or pointer updates behind. */
static int other_arithmetic(e87_t *n,unsigned mr)
{
    unsigned t=e87_top(n),i=(t+1)&7,push=(t+7)&7,extract=mr==0xf4;
    unsigned binary=mr==0xf8 || mr==0xfd,flags=0,rc=(n->control>>10)&3;
    static const unsigned rounding[]={0,2,3,1},precision[]={32,0,64,80};
    e87_real80_t x=n->reg[t],y=n->reg[i],r=x;
    if(e87_tag(n,0)==3 || (binary && e87_tag(n,1)==3) ||
       (extract && ((n->tags>>(push*2))&3)!=3)) {
        if(stack_invalid(n)==1) {
            invalid_result(n,t,extract?push:t);
            if(extract) invalid_result(n,push,push);
            if(mr==0xf8) n->status&=~0x400u;
        }
        return 1;
    }
    for(unsigned k=0;k<(binary?2u:1u);++k) {
        e87_real80_t v=k?y:x;unsigned vt=e87_tag(n,k);
        if((nan80(v)||infinity80(v)||denormal80(v)) && vt!=2) return 0;
        if(unnormal80(v) && vt!=0) return 0;
    }
    unsigned de=(mr==0xfa || mr==0xf8) && (denormal80(x)||(binary&&denormal80(y)));
    if(de) {e87_raise_exception(n,2);if(!(n->control&2)) return 1;}
    if(nan80(x) || (binary&&nan80(y))) {
        e87_raise_exception(n,1);if(!(n->control&1)) return 1;
        r=nan80(x)?x:y;
        if(binary && nan80(x) && nan80(y) && y.significand>r.significand) r=y;
        result80(n,t,r);
        if(extract) {unsigned c1=n->status&0x200;result80(n,push,r);top(n,push);n->status|=c1;}
        if(mr==0xf8) n->status&=~0x400u;
        return 1;
    }
    unsigned invalid=0;
    if(mr==0xfa) invalid=denormal80(x)||unnormal80(x)||
        ((x.sign_exponent&0x8000)&&x.significand)||
        (infinity80(x)&&!(n->control&0x1000));
    if(mr==0xf8) invalid=infinity80(x)||!y.significand||denormal80(y)||unnormal80(y);
    if(mr==0xfd) invalid=infinity80(y)&&(infinity80(x)||x.significand);
    if(extract) invalid=infinity80(x);
    if(invalid) {
        e87_raise_exception(n,1);if(!(n->control&1)) return 1;
        invalid_result(n,t,extract?push:t);
        if(extract) invalid_result(n,push,push);
        if(mr==0xf8) n->status&=~0x400u;
        return 1;
    }
    if(mr==0xe0 || mr==0xe1) {
        r.sign_exponent=mr==0xe0?r.sign_exponent^0x8000:r.sign_exponent&0x7fff;
    } else if(extract) {
        /* Early NPX extracts raw fields, not the later normalized IEEE result.
         * Denormal extraction has no dependable result in the early manual. */
        if(denormal80(x)) return 0;
        e87_real80_t exponent;
        if(!x.significand && !(x.sign_exponent&0x7fff)) exponent=r=x;
        else {
            int e=(x.sign_exponent&0x7fff)-0x3fff;
            exponent=integer80(e<0?-e:e,e<0);
            r.sign_exponent=(x.sign_exponent&0x8000)|0x3fff;
        }
        result80(n,t,exponent);result80(n,push,r);top(n,push);return 1;
    } else if(mr==0xfa) {
        if(x.significand && !infinity80(x) &&
           !e87_sqrt80(x,precision[(n->control>>8)&3],rounding[rc],&r,&flags)) return 0;
    } else if(mr==0xfc) {
        if(!infinity80(x) && x.significand) {
            int e=(x.sign_exponent&0x7fff);if(!e)e=1;e-=0x3fff;
            uint64_t sig=x.significand;
            while(!(sig>>63)) {sig<<=1;--e;}
            if(e>=63) r=(e87_real80_t){sig,(x.sign_exponent&0x8000)|(e+0x3fff)};
            else {
                unsigned sign=x.sign_exponent>>15,up=0;uint64_t integral=0,rem=sig;
                if(e>=0) {
                    unsigned shift=63-e;integral=sig>>shift;rem=sig&((UINT64_C(1)<<shift)-1);
                    uint64_t half=UINT64_C(1)<<(shift-1);
                    if(rc==0) up=rem>half || (rem==half && (integral&1));
                } else if(rc==0) up=e==-1 && sig>UINT64_C(0x8000000000000000);
                if(rc==1) up=sign && rem;
                if(rc==2) up=!sign && rem;
                r=integer80(integral+up,sign);flags=(rem?0x20:0)|(up?0x200:0);
            }
        } else if(!x.significand) r.sign_exponent&=0x8000;
    } else if(mr==0xfd) {
        if(!infinity80(x) && x.significand) {
            int e=(y.sign_exponent&0x7fff)-0x3fff,scale=0;
            if(y.significand) {
                /* PRM defines only zero or |scale|>=1, within signed word range. */
                if(!(y.significand>>63) || e<0 || e>15) return 0;
                unsigned magnitude=y.significand>>(63-e);
                if(magnitude>32767u+((y.sign_exponent>>15)!=0)) return 0;
                scale=(y.sign_exponent>>15)?-(int)magnitude:(int)magnitude;
            }
            if(!e87_scale80(x,scale,rounding[rc],n->control,&r,&flags)) return 0;
        }
    } else if(mr==0xf8) {
        if(!infinity80(y) && x.significand) {
            int ea=x.sign_exponent&0x7fff,eb=y.sign_exponent&0x7fff;
            uint64_t sig=x.significand;if(!ea)ea=1;
            while(!(sig>>63)) {sig<<=1;--ea;}
            int difference=ea-eb;unsigned partial=difference>=64;
            uint64_t quotient=0,remainder=sig;int re=ea;
            if(difference>=0) {
                unsigned shift=partial?63:difference;
                unsigned __int128 numerator=(unsigned __int128)sig<<shift;
                quotient=(uint64_t)(numerator/y.significand);remainder=numerator%y.significand;
                re=ea-shift;
            }
            if(remainder) {
                while(!(remainder>>63)) {remainder<<=1;--re;}
                e87_real80_t normal={remainder,(x.sign_exponent&0x8000)|0x3fff};
                if(!e87_scale80(normal,re-0x3fff,rounding[rc],n->control,&r,&flags)) return 0;
                /* Exact remainder cannot incur precision loss. */
                if(flags&0x20) return 0;
            } else r=(e87_real80_t){0,x.sign_exponent&0x8000};
            if(partial) n->status|=0x400;
            else {
                /* Early 287 shifts existing CC bits for small quotients. */
                unsigned cc=(quotient&1)?0x200:0;
                cc|=quotient<2?(n->status&0x200)<<5:(quotient&2)?0x4000:0;
                cc|=quotient<4?(n->status&0x4000)>>6:(quotient&4)?0x100:0;
                n->status=(n->status&~0x4700u)|cc;
            }
        } else n->status&=~0x400u;
        result80(n,t,r);e87_raise_exception(n,flags&0x3f);return 1;
    }
    result80(n,t,r);
    if(mr==0xfa || mr==0xfc || mr==0xfd) n->status=(n->status&~0x200u)|(flags&0x200);
    e87_raise_exception(n,flags&0x3f);return 1;
}
/* Comparisons and format conversion. All memory is staged by the CPU adapter.
 * Intel 287 PRM tables 1-9, 1-14, 1-17, 2-6/7/8 and Appendix C govern the
 * early-chip responses here; these are deliberately not modern x87 semantics. */
static void pop_preserving_cc(e87_t *n,unsigned count)
{
    unsigned t=e87_top(n);for(unsigned k=0;k<count;++k)tag(n,(t+k)&7,3);
    n->status=(n->status&~0x3800u)|(((t+count)&7)<<11);
}
static unsigned comparison_size(unsigned op,unsigned mr)
{
    unsigned g=(mr>>3)&7;
    if(mr>=0xc0 || (g!=2 && g!=3))return 0;
    return op==0xd8 || op==0xda?4:op==0xdc?8:op==0xde?2:0;
}
static int comparison_opcode(unsigned op,unsigned mr)
{
    return comparison_size(op,mr) || (op==0xd8 && mr>=0xd0 && mr<=0xdf) ||
        (op==0xde && mr==0xd9) || (op==0xd9 && (mr==0xe4 || mr==0xe5));
}
/* Signed magnitude comparison, normalizing in a work area only. */
static int compare_value(e87_real80_t a,e87_real80_t b)
{
    if(!a.significand && !b.significand)return 0;
    unsigned sa=a.sign_exponent>>15,sb=b.sign_exponent>>15;
    if(sa!=sb)return sa?-1:1;
    int order;
    if(infinity80(a) || infinity80(b))order=infinity80(a)-infinity80(b);
    else if(!a.significand || !b.significand)order=!!a.significand-!!b.significand;
    else {
        int ea=a.sign_exponent&0x7fff,eb=b.sign_exponent&0x7fff;
        if(!ea)ea=1;
        if(!eb)eb=1;
        while(!(a.significand>>63)){a.significand<<=1;--ea;}
        while(!(b.significand>>63)){b.significand<<=1;--eb;}
        order=ea!=eb?(ea>eb?1:-1):a.significand==b.significand?0:a.significand>b.significand?1:-1;
    }
    return sa?-order:order;
}
static int comparison(e87_t *n,unsigned op,unsigned mr,const unsigned char *input)
{
    unsigned t=e87_top(n),size=comparison_size(op,mr),memory_de=0,pops=0;
    e87_real80_t x=n->reg[t],y={0,0};unsigned empty=e87_tag(n,0)==3;
    if(op==0xd9 && mr==0xe5) { /* FXAM never raises a numeric exception. */
        unsigned cc=empty?0x4100:nan80(x)?0x100:infinity80(x)?0x500:
            denormal80(x)?0x4400:unnormal80(x)?0:!x.significand?0x4000:0x400;
        cc|=(x.sign_exponent>>15)<<9;
        n->status=(n->status&~0x4700u)|cc;return 1;
    }
    if(size) {
        if(!input)return 0;
        y=decode_operand(input,size,(op==0xda || op==0xde)?E87_INTEGER:E87_REAL,&memory_de);
        pops=((mr>>3)&7)==3;
    } else if(op!=0xd9) {
        unsigned index=op==0xde?1:mr&7;
        y=n->reg[e87_index(n,index)];empty|=e87_tag(n,index)==3;
        pops=op==0xde?2:mr>=0xd8;
    }
    if(empty) {
        /* Appendix C: empty ST with denormal MEMORY source reports I and D
         * simultaneously, unlike the usual stack-invalid-first ordering. */
        unsigned flags=1|(memory_de?2:0);e87_raise_exception(n,flags);
        if(flags&~n->control)return 1;
        n->status|=0x4500;pop_preserving_cc(n,pops);return 1;
    }
    unsigned de=memory_de || denormal80(x)||denormal80(y)||
        (unnormal80(x)&&x.significand)||(unnormal80(y)&&y.significand);
    if(de){e87_raise_exception(n,2);if(!(n->control&2))return 1;}
    unsigned invalid=nan80(x)||nan80(y)||
        (!(n->control&0x1000)&&(infinity80(x)||infinity80(y)));
    unsigned cc;
    if(invalid) {
        e87_raise_exception(n,1);if(!(n->control&1))return 1;
        cc=0x4500;
    } else {int relation=compare_value(x,y);cc=relation<0?0x100:relation==0?0x4000:0;}
    n->status=(n->status&~0x4500u)|cc;pop_preserving_cc(n,pops);return 1;
}
/* Return operand size and direction for documented memory transfer encodings. */
static unsigned conversion_size(unsigned op,unsigned mr,unsigned *kind,unsigned *store,unsigned *pop)
{
    unsigned g=(mr>>3)&7;*store=0;*pop=0;*kind=E87_REAL;
    if(mr>=0xc0)return 0;
    if((op==0xd9 || op==0xdd) && (g==0 || g==2 || g==3)) {
        *store=g!=0;*pop=g==3;return op==0xd9?4:8;
    }
    if((op==0xdb || op==0xdf) && (g==0 || g==2 || g==3)) {
        *kind=E87_INTEGER;*store=g!=0;*pop=g==3;return op==0xdb?4:2;
    }
    if(op==0xdf && g>=4) {
        *kind=(g&1)?E87_INTEGER:E87_BCD;*store=g>=6;*pop=*store;return (g&1)?8:10;
    }
    return 0;
}
static int register_transfer_opcode(unsigned op,unsigned mr)
{
    return (op==0xd9 && mr>=0xc0 && mr<=0xcf) || (op==0xdd && mr>=0xd0 && mr<=0xdf);
}
static int register_transfer(e87_t *n,unsigned op,unsigned mr)
{
    unsigned t=e87_top(n),i=e87_index(n,mr&7),p=(t+7)&7;
    e87_real80_t source=n->reg[i];
    if(op==0xd9 && mr<0xc8) { /* FLD ST(i), source captured before TOP changes. */
        if(e87_tag(n,mr&7)==3 || ((n->tags>>(2*p))&3)!=3) {
            if(stack_invalid(n)==1)invalid_result(n,p,p);return 1;
        }
        result80(n,p,source);n->status=(n->status&~0x3800u)|(p<<11);return 1;
    }
    if(op==0xd9) { /* FXCH: masked empty operands become indefinite, then swap. */
        unsigned et=e87_tag(n,0)==3,ei=e87_tag(n,mr&7)==3;
        if(et || ei) {
            e87_raise_exception(n,1);if(!(n->control&1))return 1;
            if(et)result80(n,t,indefinite);
            if(ei)result80(n,i,indefinite);
        }
        e87_real80_t a=n->reg[t],b=n->reg[i];result80(n,t,b);result80(n,i,a);return 1;
    }
    source=n->reg[t];
    if(e87_tag(n,0)==3) {if(stack_invalid(n)!=1)return 1;source=indefinite;}
    result80(n,i,source);if(mr>=0xd8)pop_preserving_cc(n,1);return 1;
}
/* Round an unsigned significand right by an arbitrary distance. RC is the
 * architectural two-bit encoding. This also handles subnormal store shifts. */
static uint64_t round_shift(uint64_t sig,unsigned shift,unsigned sign,unsigned rc,unsigned *flags)
{
    uint64_t kept,rem,half;
    if(!shift){*flags=0;return sig;}
    if(shift>=64){kept=0;rem=sig;half=UINT64_C(1)<<63;}
    else {kept=sig>>shift;rem=sig&((UINT64_C(1)<<shift)-1);half=UINT64_C(1)<<(shift-1);}
    unsigned up=rc==0?(shift<=64 && (rem>half || (rem==half && (kept&1)))):
        rc==1?sign && rem:rc==2?!sign && rem:0;
    *flags=(rem?0x20:0)|(up?0x200:0);return kept+up;
}
static int integer_magnitude(e87_real80_t x,unsigned rc,uint64_t *magnitude,unsigned *flags)
{
    if(!finite(x))return 0;
    if(!x.significand){*magnitude=0;*flags=0;return 1;}
    int exponent=(x.sign_exponent&0x7fff)-0x3fff;
    if(exponent>63)return 0;
    *magnitude=round_shift(x.significand,63-exponent,x.sign_exponent>>15,rc,flags);return 1;
}
/* 1 value delivered, 2 suppressed store, 0 historically unresolved case.
 * Packed special operands are chopped exactly as described in PRM 2-2. */
static int encode_real(e87_real80_t x,unsigned size,unsigned control,uint64_t *raw,unsigned *flags)
{
    unsigned f=size==4?23:52,bias=size==4?127:1023,emax=2*bias+1;
    unsigned sign=x.sign_exponent>>15,rc=(control>>10)&3;
    uint64_t signbit=(uint64_t)sign<<(size*8-1),mask=(UINT64_C(1)<<f)-1;
    *flags=0;
    if(nan80(x)||infinity80(x)||denormal80(x)) {
        *raw=signbit|((uint64_t)((x.sign_exponent&0x7fff)?emax:0)<<f)|((x.significand>>(63-f))&mask);
        return 1;
    }
    if(!x.significand && !unnormal80(x)){*raw=signbit;return 1;}
    int e=(x.sign_exponent&0x7fff)-0x3fff+bias;
    if(unnormal80(x) && e>1) {
        *flags=1;if(!(control&1))return 2;
        *raw=((UINT64_C(1)<<(size*8-1))|((uint64_t)emax<<f)|(UINT64_C(1)<<(f-1)));return 1;
    }
    if(e>=(int)emax) {
        if(!(control&8)){*flags=8;return 2;}
        unsigned inf=rc==0 || rc==3 || (rc==1?sign:!sign);
        *raw=signbit|(inf?((uint64_t)emax<<f):((uint64_t)(emax-1)<<f)|(mask-1));
        *flags=0x20|(inf?8:0);return 1;
    }
    unsigned tiny=e<1 || unnormal80(x);
    if(tiny && !(control&0x10)){*flags=0x10;return 2;}
    unsigned shift=63-f+(e<1?1-e:0);
    uint64_t q=round_shift(x.significand,shift,sign,rc,flags);
    if(tiny) {
        if(rc==3 || (*flags&0x20))*flags|=0x10;
        e=q>mask?1:0;
    } else if(q==(UINT64_C(1)<<(f+1))) {
        q>>=1;++e;
        if(e>=(int)emax) {
            /* Keep the same explicit exclusion as arithmetic carry overflow. */
            if(!(control&8))return 0;
            unsigned inf=rc==0 || rc==3 || (rc==1?sign:!sign);
            *raw=signbit|(inf?((uint64_t)emax<<f):((uint64_t)(emax-1)<<f)|(mask-1));
            *flags=0x220|(inf?8:0);return 1;
        }
    }
    *raw=signbit|((uint64_t)e<<f)|(q&mask);return 1;
}
static int conversion(e87_t *n,unsigned op,unsigned mr,const unsigned char *input,unsigned char *output)
{
    unsigned kind,store,pop,size=conversion_size(op,mr,&kind,&store,&pop);
    unsigned t=e87_top(n),dest=(t+7)&7,flags=0;
    e87_real80_t x=n->reg[t];
    if(!size || (store?!output:!input))return 0;
    if(!store) {
        if(((n->tags>>(2*dest))&3)!=3) {if(stack_invalid(n)==1)invalid_result(n,dest,dest);return 1;}
        if(kind==E87_BCD) {
            uint64_t magnitude=0;
            for(unsigned k=18;k>0;--k) {
                unsigned digit=(input[(k-1)/2]>>(((k-1)&1)*4))&15;
                if(digit>9)return 0; /* Invalid decimal encodings are undefined. */
                magnitude=magnitude*10+digit;
            }
            x=integer80(magnitude,input[9]>>7);
        } else {
            unsigned de=0;x=decode_operand(input,size,kind,&de);
            if(de){e87_raise_exception(n,2);if(!(n->control&2))return 1;}
        }
        result80(n,dest,x);n->status=(n->status&~0x3800u)|(dest<<11);return 1;
    }
    unsigned invalid=e87_tag(n,0)==3,rc=(n->control>>10)&3;
    uint64_t raw=0,magnitude=0;
    if(kind==E87_REAL) {
        int action;
        if(invalid){flags=1;action=(n->control&1)?encode_real(indefinite,size,n->control,&raw,&flags):2;flags|=1;}
        else action=encode_real(x,size,n->control,&raw,&flags);
        if(!action)return 0;
        e87_raise_exception(n,flags&0x3f);if(action==2)return 2;
        write_le(output,size,raw);
    } else {
        if(!invalid) {
            invalid=!integer_magnitude(x,rc,&magnitude,&flags);
            if(kind==E87_BCD && !invalid && (flags&0x20)) {
                /* Intel iRMX Troubleshooting Guide (1990), 6.5 corrects the
                 * documented +0.5 rule: even 11.5 stores as 11. The threshold
                 * is not specified. Require integral input, e.g. FRNDINT first. */
                return 0;
            }
            if(!invalid) {
                uint64_t limit=kind==E87_BCD?UINT64_C(999999999999999999):
                    (UINT64_C(1)<<(size*8-1))-(!(x.sign_exponent>>15));
                invalid=magnitude>limit;
            }
        }
        if(invalid) {
            flags=1;e87_raise_exception(n,1);if(!(n->control&1))return 2;
            if(kind==E87_BCD) {
                for(unsigned k=0;k<8;++k)output[k]=0; /* Undefined low bytes, deterministic policy. */
                output[8]=output[9]=0xff;
            } else write_le(output,size,UINT64_C(1)<<(size*8-1));
        } else if(kind==E87_BCD) {
            for(unsigned k=0;k<9;++k){unsigned low=magnitude%10;magnitude/=10;output[k]=low|((magnitude%10)<<4);magnitude/=10;}
            output[9]=(x.sign_exponent>>15)<<7;
        } else {
            write_le(output,size,(x.sign_exponent>>15)?-magnitude:magnitude);
            e87_raise_exception(n,flags&0x3f);
        }
    }
    if(!(flags&1))n->status=(n->status&~0x200u)|(flags&0x200);
    if(pop)pop_preserving_cc(n,1);return 1;
}

static int constant_opcode(unsigned op,unsigned mr)
{return op==0xd9 && mr>=0xe8 && mr<=0xee;}
static int constant(e87_t *n,unsigned mr)
{
    static const e87_real80_t values[]={
        {UINT64_C(0x8000000000000000),0x3fff},
        {UINT64_C(0xd49a784bcd1b8afe),0x4000},
        {UINT64_C(0xb8aa3b295c17f0bc),0x3fff},
        {UINT64_C(0xc90fdaa22168c235),0x4000},
        {UINT64_C(0x9a209a84fbcff799),0x3ffd},
        {UINT64_C(0xb17217f7d1cf79ac),0x3ffe},{0,0}};
    unsigned p=(e87_top(n)+7)&7;
    if(((n->tags>>(2*p))&3)!=3){if(stack_invalid(n)==1)invalid_result(n,p,p);return 1;}
    result80(n,p,values[mr-0xe8]);
    n->status=(n->status&~0x3800u)|(p<<11);return 1;
}
static int tangent(e87_t *n)
{
    unsigned t=e87_top(n),p=(t+7)&7;e87_real80_t x=n->reg[t],num,den;
    /* Before-operation stack failure is documented; masked result payloads
     * need original-chip evidence. Do not invent those two result slots. */
    if(e87_tag(n,0)==3 || ((n->tags>>(2*p))&3)!=3){
        if(n->control&1)return 0;
        e87_raise_exception(n,1);return 1;
    }
    unsigned e=x.sign_exponent&0x7fff;
    if(!finite(x) || ((x.sign_exponent&0x8000)&&x.significand) || e>0x3ffe ||
       (e==0x3ffe&&x.significand>UINT64_C(0xc90fdaa22168c234)))return 0;
    if(!e87_tangent(x,(n->control>>10)&3,&num,&den))return 0;
    result80(n,t,num);result80(n,p,den);n->status=(n->status&~0x3800u)|(p<<11);
    if(x.significand)e87_raise_exception(n,0x20);
    return 1;
}
static int transcendental_opcode(unsigned op,unsigned mr)
{return op==0xd9 && (mr==0xf0||mr==0xf1||mr==0xf2||mr==0xf3||mr==0xf9);}
static int transcendental(e87_t *n,unsigned mr)
{
    if(mr==0xf2)return tangent(n);
    unsigned t=e87_top(n),i=(t+1)&7,binary=mr!=0xf0;
    e87_real80_t x=n->reg[t],y=n->reg[i],r;
    /* Invalid operands have undefined early-chip results, not the 387 domain
     * exception policy. Defer with no mutation, including empty input slots. */
    if(e87_tag(n,0)==3 || (binary&&e87_tag(n,1)==3) || !finite(x) || (binary&&!finite(y)))return 0;
    unsigned ex=x.sign_exponent&0x7fff,ey=y.sign_exponent&0x7fff;
    if(mr==0xf0 && ((x.sign_exponent&0x8000&&x.significand)||ex>0x3ffe ||
        (ex==0x3ffe&&x.significand>UINT64_C(0x8000000000000000))))return 0;
    if((mr==0xf1||mr==0xf3) && (!x.significand||(x.sign_exponent&0x8000)))return 0;
    /* Compatibility boundary: MicroWay 87TEST 1.02 uses atan(1). Accept
     * positive equal operands; original-chip boundary bits are unverified. */
    if(mr==0xf3 && ((y.sign_exponent&0x8000&&y.significand)||ey>ex||
        (ey==ex&&y.significand>x.significand)))return 0;
    /* Largest representable magnitude strictly below 1-sqrt(1/2). */
    if(mr==0xf9 && (ex>0x3ffd || (ex==0x3ffd&&x.significand>UINT64_C(0x95f619980c4336f7))))return 0;
    /* Preserve exact cases without turning intermediate approximation error
     * into PE: log2(power-of-two) is an exact signed integer. */
    unsigned flags=0;
    if(mr==0xf1 && x.significand==UINT64_C(0x8000000000000000)){
        int e=(int)ex-16383; e87_real80_t power=integer80(e<0?-e:e,e<0);
        static const unsigned rounding[]={0,2,3,1};
        if(!e87_mul80(y,power,80,rounding[(n->control>>10)&3],&r,&flags))return 0;
        flags=flags?0x20:0;
    }else if((mr==0xf0&&!x.significand)||(mr==0xf3&&!y.significand)||
             ((mr==0xf1||mr==0xf9)&&(!y.significand||(mr==0xf9&&!x.significand)))){
        r=(e87_real80_t){0,mr==0xf0?(x.sign_exponent&0x8000):0};
        /* Non-power logarithmic zero products have the sign of Y*log(X). */
        if(mr==0xf1)r.sign_exponent=(y.sign_exponent&0x8000)^(ex<0x3fff?0x8000:0);
        if(mr==0xf9)r.sign_exponent=(y.sign_exponent^x.sign_exponent)&0x8000;
    }else{
        if(!e87_transcendental(mr,x,y,(n->control>>10)&3,n->control,&r,&flags))return 0;
    }
    result80(n,binary?i:t,r);if(binary)pop_preserving_cc(n,1);
    e87_raise_exception(n,flags);return 1;
}

int e87_numeric(e87_t *n, unsigned op, unsigned mr,
                const unsigned char *input, unsigned char *output)
{
    e87_real80_t x, y, r, truncated;
    unsigned t = e87_top(n), i, dest, flags, pc, rc, truncate_flags, rounded_up = 0;
    static const unsigned precisions[4] = {32, 0, 64, 80};
    static const unsigned rounding[4] = {0, 2, 3, 1};
    if (n->transfer_blocked) return 0;
    /* 8087 continues after FNDISI; IR remains latched until acknowledged.
     * WAIT can override IEM. A bare ESC while error-BUSY remains asserted
     * is still unsupported, rather than inventing asynchronous bus behavior. */
    if (((n->status & 0x80) || e87_pending_error(n)) &&
        (n->model == E87_80287 || !(n->control & 0x80) || n->wait_error)) return 0;
    unsigned kind,store,pop;
    if(constant_opcode(op,mr))return constant(n,mr);
    if(transcendental_opcode(op,mr))return transcendental(n,mr);
    if(comparison_opcode(op,mr))return comparison(n,op,mr,input);
    if(register_transfer_opcode(op,mr))return register_transfer(n,op,mr);
    unsigned conversion_bytes=conversion_size(op,mr,&kind,&store,&pop);
    if(conversion_bytes) {
        e87_t next=*n;unsigned char staged[10];
        if(store && !output)return 0;
        int action=conversion(&next,op,mr,input,store?staged:0);
        if(!action)return 0;
        *n=next;
        if(store && action==1)for(unsigned k=0;k<conversion_bytes;++k)output[k]=staged[k];
        return action;
    }
    if(other_arithmetic_opcode(op,mr)) {
        e87_t next=*n;
        if(!other_arithmetic(&next,mr)) return 0;
        *n=next;return 1;
    }
    if(arithmetic_memory_size(op,mr)) return memory_arithmetic(n,op,mr,input);
    if (op == 0xdb && mr < 0xc0 && ((mr >> 3) & 7) == 5) {
        i = (t + 7) & 7;
        if (!input) return 0;
        e87_real80_load(&x, input);
        if (!finite(x) && !infinity80(x) && !nan80(x) && !denormal80(x) && !unnormal80(x)) return 0;
        if (((n->tags >> (2*i)) & 3) != 3) {
            int action = stack_invalid(n);
            if (action == 1) invalid_result(n, i, i);
            return action != 0;
        }
        /* PRM table 1-8 limits FLD's D exception to short/long real.
         * Temporary-real loads preserve raw encodings without conversion. */
        n->reg[i] = x;
        tag(n, i, (infinity80(x) || nan80(x) || denormal80(x)) ? 2 :
            unnormal80(x) ? 0 : x.significand ? 0 : 1);
        top(n, i);
        return 1;
    }
    if (op == 0xdb && mr < 0xc0 && ((mr >> 3) & 7) == 7) {
        if (!output) return 0;
        if (e87_tag(n, 0) == 3) {
            int action = stack_invalid(n);
            if (action != 1) return action; /* Unmasked: suppress guest write. */
            unsigned c1 = n->status & 0x200;
            e87_real80_store(output, &indefinite);
            tag(n, t, 3); top(n, t+1); n->status |= c1;
            return 1;
        }
        if (!finite(n->reg[t]) && !is_indefinite(n->reg[t]) &&
            !(e87_tag(n,0) == 2 && (infinity80(n->reg[t]) || denormal80(n->reg[t]))) &&
            !(denormal80(n->reg[t]) || unnormal80(n->reg[t])) &&
            !(e87_tag(n, 0) == 2 &&
              (nan80(n->reg[t]) || infinity80(n->reg[t])))) return 0;
        e87_real80_store(output, &n->reg[t]);
        tag(n, t, 3);
        top(n, t+1);
        return 1;
    }
    if ((op != 0xd8 && op != 0xdc && op != 0xde) || !((mr >= 0xc0 && mr <= 0xcf) || mr >= 0xe0))
        return 0;
    i = e87_index(n, mr & 7);
    if (e87_tag(n, 0) == 3 || e87_tag(n, mr & 7) == 3) {
        int action = stack_invalid(n);
        if (action == 1) {
            dest = op == 0xd8 ? t : i;
            invalid_result(n, dest, op == 0xde ? t+1 : t);
            if (op == 0xde) tag(n, t, 3);
        }
        return action != 0;
    }
    /* Stack invalid takes priority; denormal precedes non-stack invalid.
     * Validate raw/tag combinations before committing any sticky exception. */
    for (unsigned k=0;k<2;++k) {
        unsigned slot=k?i:t, value_tag=(n->tags>>(2*slot))&3;
        e87_real80_t v=n->reg[slot];
        if (denormal80(v) || nan80(v) || infinity80(v)) {
            if(value_tag!=2) return 0;
        } else if(unnormal80(v)) {
            if(value_tag!=0) return 0;
        } else if(!finite(v)) return 0;
    }
    unsigned denormal=denormal80(n->reg[t]) || denormal80(n->reg[i]);
    if(denormal && !(n->control&2)) {e87_raise_exception(n,2);return 1;}
    if (((e87_tag(n, 0) == 2 && nan80(n->reg[t])) ||
         (e87_tag(n, mr & 7) == 2 && nan80(n->reg[i])))) {
        int a_nan = e87_tag(n, 0) == 2 && nan80(n->reg[t]);
        int b_nan = e87_tag(n, mr & 7) == 2 && nan80(n->reg[i]);
        e87_raise_exception(n, 1 | (denormal ? 2 : 0));
        if (!(n->control & 1)) return 1;
        r = a_nan ? n->reg[t] : n->reg[i];
        if (a_nan && b_nan && n->reg[i].significand > r.significand) r = n->reg[i];
        /* Equal payloads retain ST0; the manual does not specify a sign tie. */
        dest = op == 0xd8 ? t : i;
        unsigned c1 = n->status & 0x200;
        n->reg[dest] = r; tag(n, dest, 2);
        if (op == 0xde) { tag(n, t, 3); top(n, t+1); n->status |= c1; }
        return 1;
    }
    if ((mr&0xf8)==0xc8 || mr>=0xf0) {
        unsigned divide=mr>=0xf0, invalid=0, error=0;
        x=n->reg[t];y=n->reg[i];if(mr>=0xf8) {x=n->reg[i];y=n->reg[t];}
        unsigned xi=infinity80(x),yi=infinity80(y),xz=!x.significand,yz=!y.significand;
        unsigned sign=(x.sign_exponent^y.sign_exponent)&0x8000;
        if(divide) invalid=(xi&&yi) || (xz&&yz) || denormal80(y) || unnormal80(y);
        else invalid=(xi&&yz)||(yi&&xz);
        if(invalid) {
            e87_raise_exception(n,1|(denormal?2:0));
            if(!(n->control&1)) return 1;
            r=indefinite;
        } else if(divide && yz && !xi) {
            e87_raise_exception(n,4|(denormal?2:0));
            if(!(n->control&4)) return 1;
            r=(e87_real80_t){UINT64_C(0x8000000000000000),sign|0x7fff};
        } else if(xi || yi) {
            r=(e87_real80_t){divide&&yi?0:UINT64_C(0x8000000000000000),sign|(divide&&yi?0:0x7fff)};
        } else {
            pc=precisions[(n->control>>8)&3];rc=rounding[(n->control>>10)&3];
            if(!e87_product80(x,y,divide,pc,rc,n->control,&r,&error)) return 0;
        }
        if(denormal) error|=2;
        e87_raise_exception(n,error&0x3f);
        dest=op==0xd8?t:i;n->reg[dest]=r;
        tag(n,dest,(infinity80(r)||nan80(r)||denormal80(r))?2:unnormal80(r)?0:r.significand?0:1);
        top(n,t);if(op==0xde) {tag(n,t,3);top(n,t+1);}
        n->status|=error&0x200;
        return 1;
    }
    /* Negate only after NaN propagation: subtraction must not alter its payload
     * sign. E0 uses ST0-STi; E8 uses STi-ST0, irrespective of destination. */
    x=n->reg[t]; y=n->reg[i];
    if ((mr & 0xf8)==0xe0) y.sign_exponent ^= 0x8000;
    if ((mr & 0xf8)==0xe8) x.sign_exponent ^= 0x8000;
    if (((e87_tag(n, 0) == 2 && infinity80(n->reg[t])) ||
         (e87_tag(n, mr & 7) == 2 && infinity80(n->reg[i])))) {
        int a_inf = e87_tag(n, 0) == 2 && infinity80(n->reg[t]);
        int b_inf = e87_tag(n, mr & 7) == 2 && infinity80(n->reg[i]);
        if(denormal) e87_raise_exception(n,2);
        dest = op == 0xd8 ? t : i;
        /* Early-NPX projective addition rejects ANY two infinities. Affine addition
         * rejects opposite signs only (Intel PRM table 1-12). */
        if (a_inf && b_inf && (!(n->control & 0x1000) ||
            ((x.sign_exponent ^ y.sign_exponent) & 0x8000))) {
            e87_raise_exception(n,1);
            if (!(n->control & 1)) return 1;
            invalid_result(n, dest, op == 0xde ? t+1 : t);
        } else {
            n->reg[dest] = a_inf ? x : y;
            tag(n, dest, 2); top(n, op == 0xde ? t+1 : t);
        }
        if (op == 0xde) tag(n, t, 3);
        return 1;
    }
    unsigned nonnormal = denormal80(n->reg[t]) || unnormal80(n->reg[t]) ||
                         denormal80(n->reg[i]) || unnormal80(n->reg[i]);
    pc = precisions[(n->control >> 8) & 3];
    rc = rounding[(n->control >> 10) & 3];
    if (e87_add80_range(x,y,pc,rc,n->control,&r,&flags)) {
        e87_raise_exception(n, (flags & 0x3f) | (denormal ? 2 : 0));
        dest = op == 0xd8 ? t : i;
        n->reg[dest] = r;
        tag(n,dest,(infinity80(r) || denormal80(r)) ? 2 : r.significand ? 0 : 1);
        top(n,op == 0xde ? t+1 : t);
        if(op == 0xde) tag(n,t,3);
        n->status |= flags & 0x200;
        return 1;
    }
    int (*add)(e87_real80_t,e87_real80_t,unsigned,unsigned,e87_real80_t *,unsigned *) =
        nonnormal ? e87_add80_unnormal : e87_add80;
    if (!add(x, y, pc, rc, &r, &flags) ||
        (flags & ~E87_ARITH_INEXACT) || (!finite(r) && !unnormal80(r))) return 0;
    if (flags & E87_ARITH_INEXACT) {
        if (!add(x, y, pc, 1, &truncated, &truncate_flags))
            return 0;
        /* C1 denotes a significand increment (magnitude), including negatives.
         * Compare against truncation at the SAME precision, avoiding double
         * rounding through an intermediate host/extended-precision value. */
        rounded_up = ((r.sign_exponent & 0x7fff) > (truncated.sign_exponent & 0x7fff)) ||
            (((r.sign_exponent & 0x7fff) == (truncated.sign_exponent & 0x7fff)) &&
             r.significand > truncated.significand);
        e87_raise_exception(n, 0x20); /* PE; model-specific IR/BUSY or ES. */
    }
    if (denormal) n->status |= 2; /* Commit sticky DE only with completed arithmetic. */
    dest = op == 0xd8 ? t : i;
    n->reg[dest] = r;
    tag(n, dest, unnormal80(r) ? 0 : r.significand ? 0 : 1);
    top(n, t);
    if (op == 0xde) { tag(n, t, 3); top(n, t+1); }
    if (rounded_up) n->status |= 0x200;
    /* Precision is an after-operation exception: even unmasked, commit/pop. */
    return 1;
}

int e87_cpu_control(void *n, unsigned op, unsigned mr, unsigned short *ax)
{
    if (op == 0xd9 && mr == 0xd0) {
        e87_t *state = n;
        if (state->transfer_blocked || e87_error_signal(state)) return 0;
    }
    return e87_control(n, op, mr, ax) || e87_numeric(n, op, mr, 0, 0);
}
int e87_cpu_memory(void *n, unsigned op, unsigned mr, int query, unsigned char *bytes)
{
    unsigned form = (mr >> 3) & 7;
    unsigned size = e87_memory_control(n, op, mr, 1, 0);
    if (size) {
        if (query) return (size << 2) | (form >= 6 ? 2 : 1);
        return e87_memory_control(n, op, mr, 0, bytes) ? 1 : 0;
    }
    unsigned kind,store,pop;
    size=conversion_size(op,mr,&kind,&store,&pop);
    if(size) {
        if(query)return (size<<2)|(store?2:1);
        return e87_numeric(n,op,mr,store?0:bytes,store?bytes:0);
    }
    size=comparison_size(op,mr);
    if(size) {
        if(query)return (size<<2)|1;
        return e87_numeric(n,op,mr,bytes,0);
    }
    size=arithmetic_memory_size(op,mr);
    if(size) {
        if(query) return (size<<2)|1;
        return e87_numeric(n,op,mr,bytes,0);
    }
    if (op != 0xdb || mr >= 0xc0 || (form != 5 && form != 7)) return 0;
    if (query) return form == 5 ? 1 : 2;
    return e87_numeric(n, op, mr, form == 5 ? bytes : 0, form == 7 ? bytes : 0);
}

int e87_cpu_event(void *opaque, unsigned event)
{
    e87_t *n = opaque;
    if (event == E86_NPX_ERROR) return n->model == E87_80287 && e87_error_signal(n);
    if (event == E86_NPX_WAIT) return e87_error_wait(n);
    if (event == E86_NPX_BUSY) return n->transfer_blocked != 0;
    if (event == E86_NPX_OVERRUN && n->model == E87_80287) {
        n->transfer_blocked = 1;
        return 1;
    }
    return 0;
}

void e87_cpu_address(void *opaque, const struct e86_npx_address *a)
{
    e87_t *n=opaque;
    /* Only implemented numeric forms update exception pointers. Controls,
     * including save/restore and stack control operations, preserve them. */
    unsigned kind,store,pop;
    if (!(constant_opcode(a->op,a->mr) || transcendental_opcode(a->op,a->mr) || comparison_opcode(a->op,a->mr) || register_transfer_opcode(a->op,a->mr) ||
          conversion_size(a->op,a->mr,&kind,&store,&pop) || other_arithmetic_opcode(a->op,a->mr) || arithmetic_memory_size(a->op,a->mr) || (a->op==0xdb && a->memory &&
           (((a->mr>>3)&7)==5 || ((a->mr>>3)&7)==7)) ||
          ((a->op==0xd8 || a->op==0xdc || a->op==0xde) &&
           ((a->mr>=0xc0 && a->mr<=0xcf) || a->mr>=0xe0)))) return;
    n->opcode=((a->op&7)<<8)|a->mr;
    if(n->protected_mode && n->model==E87_80287) {
        n->instruction_pointer=a->ip; n->instruction_selector=a->cs;
        if(a->memory) { n->operand_pointer=a->offset; n->operand_selector=a->selector; }
    } else {
        n->instruction_pointer=(n->model==E87_8087 ? a->linear_esc : a->linear_ip)&0xfffff;
        if(a->memory) n->operand_pointer=a->linear_operand&0xfffff;
    }
}
