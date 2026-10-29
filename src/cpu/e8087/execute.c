/* PCE NPX numeric execution foundation. GPL-2.0-or-later. */
#include "execute.h"
#include "arithmetic.h"
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
int e87_numeric(e87_t *n, unsigned op, unsigned mr,
                const unsigned char *input, unsigned char *output)
{
    e87_real80_t x, r, truncated;
    unsigned t = e87_top(n), i, dest, flags, pc, rc, truncate_flags, rounded_up = 0;
    static const unsigned precisions[4] = {32, 0, 64, 80};
    static const unsigned rounding[4] = {0, 2, 3, 1};
    if (n->transfer_blocked) return 0;
    /* 8087 continues after FNDISI; IR remains latched until acknowledged.
     * WAIT can override IEM. A bare ESC while error-BUSY remains asserted
     * is still unsupported, rather than inventing asynchronous bus behavior. */
    if (((n->status & 0x80) || e87_pending_error(n)) &&
        (n->model == E87_80287 || !(n->control & 0x80) || n->wait_error)) return 0;
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
    if ((op != 0xd8 && op != 0xdc && op != 0xde) || mr < 0xc0 || mr > 0xc7)
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
    if (((e87_tag(n, 0) == 2 && infinity80(n->reg[t])) ||
         (e87_tag(n, mr & 7) == 2 && infinity80(n->reg[i])))) {
        int a_inf = e87_tag(n, 0) == 2 && infinity80(n->reg[t]);
        int b_inf = e87_tag(n, mr & 7) == 2 && infinity80(n->reg[i]);
        if(denormal) e87_raise_exception(n,2);
        dest = op == 0xd8 ? t : i;
        /* Early-NPX projective addition rejects ANY two infinities. Affine addition
         * rejects opposite signs only (Intel PRM table 1-12). */
        if (a_inf && b_inf && (!(n->control & 0x1000) ||
            ((n->reg[t].sign_exponent ^ n->reg[i].sign_exponent) & 0x8000))) {
            e87_raise_exception(n,1);
            if (!(n->control & 1)) return 1;
            invalid_result(n, dest, op == 0xde ? t+1 : t);
        } else {
            n->reg[dest] = a_inf ? n->reg[t] : n->reg[i];
            tag(n, dest, 2); top(n, op == 0xde ? t+1 : t);
        }
        if (op == 0xde) tag(n, t, 3);
        return 1;
    }
    unsigned nonnormal = denormal80(n->reg[t]) || unnormal80(n->reg[t]) ||
                         denormal80(n->reg[i]) || unnormal80(n->reg[i]);
    pc = precisions[(n->control >> 8) & 3];
    rc = rounding[(n->control >> 10) & 3];
    if (e87_add80_range(n->reg[t],n->reg[i],pc,rc,n->control,&r,&flags)) {
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
    if (!add(n->reg[t], n->reg[i], pc, rc, &r, &flags) ||
        (flags & ~E87_ARITH_INEXACT) || (!finite(r) && !unnormal80(r))) return 0;
    if (flags & E87_ARITH_INEXACT) {
        if (!add(n->reg[t], n->reg[i], pc, 1, &truncated, &truncate_flags))
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
