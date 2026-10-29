/* PCE NPX architectural state. GPL-2.0-or-later. */
#include "e8087.h"
#include <string.h>

int e87_pending_error(const e87_t *n)
{
    return (n->status & ~n->control & 0x3f) != 0;
}
void e87_raise_exception(e87_t *n, unsigned flags)
{
    flags &= 0x3f;
    n->status |= flags;
    if (flags & ~n->control) {
        n->status |= 0x80; /* 8087 IR / 287 ES */
        if (n->model == E87_8087 && (!(n->control & 0x80) || n->wait_error))
            n->status |= 0x8000;
    }
}
int e87_error_signal(const e87_t *n)
{
    return (n->status & 0x80) && (n->model == E87_80287 ||
           !(n->control & 0x80) || n->wait_error);
}
int e87_error_wait(e87_t *n)
{
    if (n->model == E87_8087 && (n->status & 0x80)) {
        n->wait_error = 1;
        n->status |= 0x8000;
        return 1;
    }
    return 0;
}
unsigned e87_top(const e87_t *n) { return (n->status >> 11) & 7; }
unsigned e87_index(const e87_t *n, unsigned logical)
{
    return (e87_top(n) + logical) & 7;
}
unsigned e87_tag(const e87_t *n, unsigned logical)
{
    return (n->tags >> (2 * e87_index(n, logical))) & 3;
}
void e87_fninit(e87_t *n)
{
    n->control = n->model == E87_8087 ? 0x03ff : 0x037f;
    /* Original 8087/287 retain C0..C3 (Intel 387 PRM Appendix C.3).
     * Construction zeros unspecified initial state; subsequent resets retain it. */
    n->status &= 0x4700;
    n->transfer_blocked = 0;
    n->wait_error = 0;
    n->tags = 0xffff;
    n->opcode = 0;
    n->instruction_pointer = n->operand_pointer = 0;
    n->instruction_selector = n->operand_selector = 0;
}
void e87_reset(e87_t *n)
{
    n->protected_mode = 0;
    e87_fninit(n);
}
int e87_init(e87_t *n, e87_model_t model)
{
    if (model != E87_8087 && model != E87_80287) return 0;
    memset(n, 0, sizeof(*n));
    n->model = model;
    e87_reset(n);
    return 1;
}
int e87_control(e87_t *n, unsigned opcode, unsigned modrm, uint16_t *ax)
{
    unsigned top;
    if (opcode == 0xd9 && modrm == 0xd0) return 1; /* FNOP */
    if (opcode == 0xdb) {
        switch (modrm) {
        case 0xe0: /* FNENI: interrupt mask bit, ignored by 287. */
            if (n->model == E87_8087) {
                n->control &= ~0x0080;
                if(n->status & 0x80) n->status |= 0x8000;
            }
            return 1;
        case 0xe1: /* FNDISI */
            if (n->model == E87_8087) {
                n->control |= 0x0080;
                n->status &= ~0x8000u;
                n->wait_error = 0;
            }
            return 1;
        case 0xe2: /* FNCLEX: preserve TOP/condition codes/register payloads. */
            n->status &= ~0x80bfu;
            n->wait_error = 0;
            return 1;
        case 0xe3: e87_fninit(n); return 1;
        case 0xe4:
            if (n->model != E87_80287) return 0;
            n->protected_mode = 1;
            return 1;
        }
    }
    if (opcode == 0xdf && modrm == 0xe0) {
        if (n->model != E87_80287 || ax == NULL) return 0;
        *ax = n->status;
        return 1;
    }
    if (opcode == 0xdd && modrm >= 0xc0 && modrm <= 0xc7) {
        n->tags |= (uint16_t)(3u << (2 * e87_index(n, modrm & 7)));
        return 1;
    }
    if (opcode == 0xd9 && (modrm == 0xf6 || modrm == 0xf7)) {
        top = (e87_top(n) + (modrm == 0xf6 ? 7 : 1)) & 7;
        /* Rotate TOP, clear C1, preserve tags and physical contents. */
        n->status = (n->status & ~0x3a00) | (top << 11);
        return 1;
    }
    return 0;
}

static unsigned env_word(const unsigned char *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}
static void env_put(unsigned char *p, unsigned v)
{
    p[0] = v; p[1] = v >> 8;
}
static void env_store(const e87_t *n, unsigned char *p)
{
    env_put(p, n->control); env_put(p+2, n->status); env_put(p+4, n->tags);
    env_put(p+6, n->instruction_pointer); env_put(p+10, n->operand_pointer);
    env_put(p+8, n->protected_mode ? n->instruction_selector :
        ((n->instruction_pointer >> 4) & 0xf000) | (n->opcode & 0x7ff));
    env_put(p+12, n->protected_mode ? n->operand_selector :
        ((n->operand_pointer >> 4) & 0xf000));
}
/* Control/environment changes reevaluate the live exception request, unlike
 * arithmetic's sticky error latch. A stored image retains the original state. */
static void env_errors(e87_t *n)
{
    n->status = (n->status & ~0x80u) | (e87_pending_error(n) ? 0x80 : 0);
    n->wait_error = 0;
    if (n->model == E87_8087) {
        n->status &= ~0x8000u;
        if ((n->status & 0x80) && !(n->control & 0x80)) n->status |= 0x8000;
    }
}
static void env_load(e87_t *n, const unsigned char *p)
{
    n->control = env_word(p); n->status = env_word(p+2); n->tags = env_word(p+4);
    n->instruction_pointer = env_word(p+6); n->operand_pointer = env_word(p+10);
    if (n->protected_mode) {
        n->instruction_selector = env_word(p+8);
        n->operand_selector = env_word(p+12);
    } else {
        n->instruction_pointer |= (env_word(p+8) & 0xf000) << 4;
        n->operand_pointer |= (env_word(p+12) & 0xf000) << 4;
        n->opcode = env_word(p+8) & 0x7ff;
    }
    /* React to restored exception flags and masks at the next CPU boundary. */
    env_errors(n);
}
unsigned e87_memory_control(e87_t *n, unsigned op, unsigned mr,
                            int query, unsigned char *p)
{
    unsigned f = (mr >> 3) & 7, size = 0, i, j, k;
    if (mr >= 0xc0) return 0;
    if (op == 0xd9 && (f == 5 || f == 7)) size = 2;
    if (op == 0xdd && f == 7) size = 2;
    if (op == 0xd9 && (f == 4 || f == 6)) size = 14;
    if (op == 0xdd && (f == 4 || f == 6)) size = 94;
    if (!size || query) return size;
    if (!p || n->transfer_blocked) return 0;
    /* Loads are error-checking; no-wait stores can inspect pending errors. */
    if ((f == 4 || f == 5) && e87_error_signal(n)) return 0;
    if (size == 2) {
        if (f == 5) {
            n->control = env_word(p);
            env_errors(n);
        } else env_put(p, op == 0xd9 ? n->control : n->status);
    } else if (f == 4) {
        env_load(n, p);
        if (size == 94) for (i = 0; i < 8; ++i) {
            k = e87_index(n, i);
            n->reg[k].significand = 0;
            for (j = 0; j < 8; ++j)
                n->reg[k].significand |= (uint64_t)p[14+10*i+j] << (8*j);
            n->reg[k].sign_exponent = env_word(p+22+10*i);
        }
    } else {
        env_store(n, p);
        if (size == 14) {
            /* Intel Numerics Supplement S-41: mask six errors, preserve IEM
             * and stack, release the error request before a subsequent WAIT. */
            n->control |= 0x3f;
            env_errors(n);
            return size;
        }
        for (i = 0; i < 8; ++i) {
            k = e87_index(n, i);
            for (j = 0; j < 8; ++j)
                p[14+10*i+j] = n->reg[k].significand >> (8*j);
            env_put(p+22+10*i, n->reg[k].sign_exponent);
        }
        e87_fninit(n);
    }
    return size;
}
