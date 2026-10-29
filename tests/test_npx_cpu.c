/* PCE NPX CPU-interface regression. GPL-2.0-or-later. */
#include <cpu/e8086/e8086.h>
#include <cpu/e8087/e8087.h>
#include <stdio.h>
#include <string.h>
static unsigned char ram[65536];
static unsigned checks, failures, calls;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; fprintf(stderr,"line %d: %s\n",__LINE__,#x); } } while (0)
static unsigned char r8(void *p,unsigned long a) { (void)p; return ram[a&65535]; }
static unsigned short r16(void *p,unsigned long a) { return r8(p,a)|(r8(p,a+1)<<8); }
static void w8(void *p,unsigned long a,unsigned char v) { (void)p; ram[a&65535]=v; }
static void w16(void *p,unsigned long a,unsigned short v) { w8(p,a,v);w8(p,a+1,v>>8); }
static int control(void *p,unsigned op,unsigned mr,unsigned short *ax)
{ ++calls; return e87_control(p,op,mr,ax); }
static e8086_t *cpu(unsigned model)
{
    e8086_t *c=e86_new();
    if (model) e86_set_80286(c);
    else e86_set_8088(c);
    e86_set_mem(c,NULL,r8,w8,r16,w16);
    e86_reset(c); e86_set_cs(c,0);e86_set_ip(c,0x100);
    e86_set_ss(c,0);e86_set_sp(c,0x8000);
    memset(ram,0,sizeof ram);w16(NULL,7*4,0x400);
    return c;
}
static void step(e8086_t *c,unsigned op,unsigned mr)
{
    unsigned i;
    unsigned short ip=c->ip;
    ram[ip]=op;ram[(ip+1)&65535]=mr;c->pq_cnt=0;
    for(i=0;i<10000 && c->ip==ip && !(c->state&E86_STATE_NPX_STOP);++i)
        e86_clock(c,1);
    CHECK(i<10000);
}
int main(void)
{
    unsigned model,bits;
    for(model=0;model<2;++model) {
        e8086_t *c=cpu(model);e87_t n;
        e87_init(&n,model?E87_80287:E87_8087);
        CHECK(c->npx_control==NULL);
        e86_set_npx_control(c,&n,control);
        n.tags=0;n.status=0x1234;
        step(c,0xdb,0xe3);
        CHECK(n.status==0x200 && n.tags==0xffff && c->ip==0x102);
        if(model) {
            step(c,0xdb,0xe4);CHECK(n.protected_mode);
            step(c,0xdb,0xe3);CHECK(n.protected_mode);
            n.status=0x3800;step(c,0xdf,0xe0);CHECK(e86_get_ax(c)==0x3800);
        }
        step(c,0xd9,0xfb);CHECK(c->state&E86_STATE_NPX_STOP);
        e86_reset(c);CHECK(c->npx_ext==&n && c->npx_control==control);
        CHECK(!(c->state&E86_STATE_NPX_STOP));
        if(model) CHECK(n.protected_mode); /* CPU-only reset must not reset NPX. */
        e86_set_npx_control(c,NULL,NULL);
        CHECK(c->npx_ext==NULL && c->npx_control==NULL);
        e86_set_cs(c,0);e86_set_ip(c,0x100);
        step(c,0xdb,0xe3);CHECK(c->ip==0x102); /* Old no-device behavior. */
        e86_del(c);
    }
    /* Every MP/EM/TS combination; ESC traps before control callback. */
    for(bits=0;bits<8;++bits) {
        e8086_t *c=cpu(1);e87_t n;e87_init(&n,E87_80287);
        e86_set_npx_control(c,&n,control);n.tags=0;calls=0;c->msw=bits<<1;
        step(c,0xdb,0xe3);
        if(bits&6) {
            CHECK(calls==0 && n.tags==0 && c->ip==0x400);
            CHECK(r16(NULL,c->dreg[E86_REG_SP])==0x100);
        } else CHECK(calls==1 && n.tags==0xffff && c->ip==0x102);
        e86_del(c);
    }
    for(bits=0;bits<8;++bits) {
        e8086_t *c=cpu(1);e87_t n;e87_init(&n,E87_80287);
        e86_set_npx_control(c,&n,control);calls=0;c->msw=bits<<1;
        step(c,0x9b,0x90);
        CHECK(calls==0);
        CHECK(c->ip==((bits&5)==5 ? 0x400 : 0x101));
        e86_del(c);
    }
    {
        e8086_t *c=cpu(1);e87_t n;e87_init(&n,E87_80287);
        e86_set_npx_control(c,&n,control);n.tags=0;calls=0;
        step(c,0xd9,0x00); /* Memory form intentionally not integrated yet. */
        CHECK((c->state&E86_STATE_NPX_STOP) && c->ip==0x100);
        CHECK(n.tags==0 && calls==0);
        e86_clock(c,100);CHECK(c->ip==0x100);
        e86_del(c);
    }
    printf("NPX CPU: %u checks, %u failures\n",checks,failures);
    return failures!=0;
}
