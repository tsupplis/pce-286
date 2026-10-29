/* PCE NPX CPU-interface regression. GPL-2.0-or-later. */
#include <cpu/e8086/e8086.h>
#include <cpu/e8087/execute.h>
#include <stdio.h>
#include <string.h>
static unsigned char ram[65536];
static unsigned checks, failures, calls, operand_writes;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; fprintf(stderr,"line %d: %s\n",__LINE__,#x); } } while (0)
static unsigned char r8(void *p,unsigned long a) { (void)p; return ram[a&65535]; }
static unsigned short r16(void *p,unsigned long a) { return r8(p,a)|(r8(p,a+1)<<8); }
static void w8(void *p,unsigned long a,unsigned char v) { (void)p; if(a>=0x600 && a<0x60a) ++operand_writes; ram[a&65535]=v; }
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
static void protected_setup(e8086_t *c)
{
    c->gdtr_base=0x2000;c->gdtr_limit=0x1f;
    w16(0,0x2008,0xffff);ram[0x200d]=0x9b; /* flat code */
    w16(0,0x2010,0xffff);ram[0x2015]=0x93; /* flat stack */
    c->sreg[E86_REG_CS]=8;c->sreg[E86_REG_SS]=16;c->sreg[E86_REG_DS]=24;
    c->seg_base[E86_REG_CS]=0;c->seg_base[E86_REG_SS]=0;c->seg_base[E86_REG_DS]=0;
    c->seg_ar[E86_REG_CS]=0x9b;c->seg_ar[E86_REG_SS]=0x93;c->seg_ar[E86_REG_DS]=0x93;
    c->idtr_base=0x3000;c->idtr_limit=0x7f;
    for(unsigned v=9;v<=13;++v) {
        w16(0,0x3000+v*8,0x500);w16(0,0x3002+v*8,8);ram[0x3005+v*8]=0x86;
    }
    c->msw|=1;
}
/* Real CPU bytes: after-operation PE, mask controls, WAIT and recovery. */
static void precision_wait(void)
{
    for (unsigned iem=0; iem<2; ++iem) {
        e8086_t *c=cpu(0); e87_t n;
        e87_init(&n,E87_8087);
        e86_set_npx_control(c,&n,e87_cpu_control);
        e86_set_npx_event(c,e87_cpu_event);
        n.control=(n.control & ~0xa0u) | (iem<<7);
        n.tags=0xfff0;
        n.reg[0]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff};
        n.reg[1]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3fbe};
        step(c,0xde,0xc1);
        CHECK(e87_top(&n)==1 && (n.status&0x80a0)==(iem?0xa0:0x80a0));
        CHECK(e87_error_signal(&n)==!iem);
        step(c,0xdb,0xe0); /* FNENI executes despite pending IR/BUSY. */
        CHECK(e87_error_signal(&n));
        step(c,0xdb,0xe1); /* FNDISI preserves IR but masks INT. */
        CHECK(!e87_error_signal(&n) && (n.status&0x80) && !(n.status&0x8000));
        step(c,0xd8,0xc0); /* Exact arithmetic continues with old PE/IR latched. */
        CHECK(n.reg[1].sign_exponent==0x4000 && (n.status&0xa0)==0xa0);
        CHECK(!(n.status&0x8000));
        unsigned ip=c->ip;
        ram[ip]=0x9b;c->pq_cnt=0;
        e86_clock(c,100);
        CHECK(c->ip==ip && !(c->state&E86_STATE_NPX_STOP));
        CHECK(n.wait_error && e87_error_signal(&n));
        /* Simulate an interrupt handler entering at a separate code location. */
        e86_set_ip(c,0x200);c->pq_cnt=0;
        step(c,0xdb,0xe2);
        CHECK(!e87_error_signal(&n) && !(n.status&0x80bf));
        e86_set_ip(c,ip);c->pq_cnt=0;
        step(c,0x9b,0x90);
        CHECK(c->ip==ip+1);
        e86_del(c);
    }
}
static void pending_287(void)
{
    const unsigned ops[]={0x9b,0xd8,0xd9,0xdd,0xdb};
    const unsigned mrs[]={0x90,0xc1,0xf7,0xc0,0x3f};
    for(unsigned pm=0;pm<2;++pm) for(unsigned k=0;k<5;++k) {
        e8086_t *c=cpu(1);e87_t n,before;e87_init(&n,E87_80287);
        e86_set_npx_control(c,&n,e87_cpu_control);
        e86_set_npx_memory(c,e87_cpu_memory);e86_set_npx_event(c,e87_cpu_event);
        if(pm) {
            protected_setup(c);c->idtr_limit=0x87;
            w16(0,0x3080,0x500);w16(0,0x3082,8);ram[0x3085]=0x86;
        } else w16(0,16*4,0x500);
        n.control&=~0x20u;e87_raise_exception(&n,0x20);before=n;
        e86_set_bx(c,0x600);operand_writes=0;
        step(c,ops[k],mrs[k]);
        CHECK(c->ip==0x500 && c->dreg[E86_REG_SP]==0x7ffa);
        CHECK(r16(0,0x7ffa)==0x100); /* Faulting WAIT/ESC, no error word. */
        CHECK(!memcmp(&n,&before,sizeof n) && operand_writes==0);
        CHECK(!(c->state&E86_STATE_NPX_STOP));
        /* Nonwaiting status read and clear must work in the handler. */
        step(c,0xdf,0xe0);CHECK(e86_get_ax(c)==before.status);
        step(c,0xdb,0xe2);CHECK(!e87_error_signal(&n));
        e86_del(c);
    }
    /* #7 takes precedence; EM suppresses WAIT's direct ERROR test. */
    for(unsigned bits=0;bits<8;++bits) for(unsigned wait=0;wait<2;++wait) {
        e8086_t *c=cpu(1);e87_t n;e87_init(&n,E87_80287);
        e86_set_npx_control(c,&n,e87_cpu_control);e86_set_npx_event(c,e87_cpu_event);
        w16(0,16*4,0x500);n.control&=~1u;e87_raise_exception(&n,1);
        c->msw=bits<<1;
        step(c,wait?0x9b:0xd8,wait?0x90:0xc0);
        unsigned nm=wait?((bits&5)==5):!!(bits&6);
        CHECK(c->ip==(nm?0x400:wait && (bits&2)?0x101:0x500));
        CHECK(!(c->state&E86_STATE_NPX_STOP));e86_del(c);
    }
    /* All implemented nonchecking control encodings remain available. */
    for(unsigned mr=0xe0;mr<=0xe4;++mr) {
        e8086_t *c=cpu(1);e87_t n;e87_init(&n,E87_80287);
        e86_set_npx_control(c,&n,e87_cpu_control);e86_set_npx_event(c,e87_cpu_event);
        n.control&=~1u;e87_raise_exception(&n,1);
        step(c,0xdb,mr);CHECK(c->ip==0x102 && !(c->state&E86_STATE_NPX_STOP));
        e86_del(c);
    }
}
int main(void)
{
    unsigned model, kind;
    precision_wait();
    pending_287();
    const unsigned char one[10]={0,0,0,0,0,0,0,0x80,0xff,0x3f};
    for (model=0; model<2; ++model) {
        e8086_t *c=cpu(model); e87_t n;
        e87_init(&n,model?E87_80287:E87_8087);
        e86_set_npx_control(c,&n,e87_cpu_control);
        e86_set_npx_memory(c,e87_cpu_memory);
        /* DS and ES share selectors but have different hidden bases on 286. */
        e86_set_ds(c,0); e86_set_es(c,0);
        if (model) c->seg_base[E86_REG_DS]=0x1000;
        e86_set_bx(c,0x600);
        memcpy(ram+(model?0x1600:0x600),one,10);
        step(c,0xdb,0x2f); /* FLD [BX] */
        CHECK(c->ip==0x102 && e87_top(&n)==7);
        step(c,0xdb,0x2f);
        step(c,0xde,0xc1); /* FADDP ST1,ST0 */
        e86_set_bx(c,0x700);
        memset(ram+(model?0x1700:0x700)-1,0xa5,12);
        step(c,0xdb,0x3f); /* FSTP [BX] */
        CHECK(c->ip==0x108 && n.tags==0xffff);
        CHECK(ram[(model?0x1700:0x700)+7]==0x80);
        CHECK(ram[(model?0x1700:0x700)+8]==0);
        CHECK(ram[(model?0x1700:0x700)+9]==0x40);
        CHECK(ram[(model?0x1700:0x700)-1]==0xa5);
        CHECK(ram[(model?0x1700:0x700)+10]==0xa5);
        e86_reset(c); CHECK(c->npx_memory==e87_cpu_memory);
        e86_set_npx_control(c,NULL,NULL); CHECK(c->npx_memory==NULL);
        e86_del(c);
    }
    /* Invalid access must leave the NPX stack and destination unchanged. */
    for(kind=0;kind<4;++kind) {
        e8086_t *c=cpu(1); e87_t n, before;
        e87_init(&n,E87_80287); e87_numeric(&n,0xdb,0x2f,one,0); before=n;
        e86_set_npx_control(c,&n,e87_cpu_control);
        e86_set_npx_memory(c,e87_cpu_memory);
        e86_set_ds(c,0);e86_set_bx(c,0x600);
        w16(NULL,13*4,0x500);memset(ram+0x600,0xa5,10);
        if(kind==0) c->seg_limit[E86_REG_DS]=0x5ff; /* first byte invalid */
        if(kind==1) c->seg_limit[E86_REG_DS]=0x605; /* later byte: deferred */
        if(kind==2) c->seg_ar[E86_REG_DS]=0x91; /* read-only */
        if(kind==3) c->msw|=4; /* EM: #7 before memory */
        step(c,0xdb,0x3f);
        CHECK(!memcmp(&n,&before,sizeof n));
        CHECK(ram[0x600]==0xa5 && ram[0x609]==0xa5);
        CHECK(c->ip==(kind==1?0x100:kind==3?0x400:0x500));
        CHECK(!!(c->state&E86_STATE_NPX_STOP)==(kind==1));
        e86_del(c);
    }
    /* First-word fault vs later overrun vs exactly fitting operand, both
     * directions and CPU modes. Validate frames and recovery, not just vector. */
    for(unsigned pm=0;pm<2;++pm) for(unsigned store=0;store<2;++store)
    for(unsigned boundary=0;boundary<3;++boundary) {
        e8086_t *c=cpu(1);e87_t n;
        e87_init(&n,E87_80287);
        if(store) CHECK(e87_numeric(&n,0xdb,0x2f,one,0));
        e86_set_npx_control(c,&n,e87_cpu_control);
        e86_set_npx_memory(c,e87_cpu_memory);
        e86_set_npx_event(c,e87_cpu_event);
        w16(0,9*4,0x500);w16(0,13*4,0x500);
        e86_set_ds(c,0);e86_set_bx(c,0x600);
        if(pm) protected_setup(c);
        c->seg_limit[E86_REG_DS]=boundary==0?0x600:boundary==1?0x601:0x609;
        memcpy(ram+0x600,one,10);
        step(c,0xdb,store?0x3f:0x2f);
        CHECK(!(c->state & E86_STATE_NPX_STOP));
        CHECK(c->ip==(boundary==2?0x102:0x500));
        CHECK(n.transfer_blocked==(boundary==1));
        if(boundary==2) {
            CHECK(n.tags==(store?0xffff:0x3fff));
        } else {
            CHECK(n.tags==(store?0x3fff:0xffff));
            CHECK(!memcmp(ram+0x600,one,10));
            CHECK(c->int_vec==(boundary==0?13:9));
            CHECK(e86_get_sp(c)==(boundary==0 && pm?0x7ff8:0x7ffa));
            unsigned frame=e86_get_sp(c)+(boundary==0 && pm?2:0);
            CHECK(r16(0,frame)==(boundary==1 && pm?0x102:0x100));
            if(boundary==0 && pm) CHECK(r16(0,e86_get_sp(c))==0);
        }
        if(boundary==1) {
            ram[0x500]=0x9b;c->pq_cnt=0;
            e86_clock(c,200);CHECK(c->ip==0x500 && n.transfer_blocked);
            ram[0x500]=0xd9;ram[0x501]=0xf7;c->pq_cnt=0;
            e86_clock(c,100);CHECK(c->ip==0x500 && n.transfer_blocked);
            step(c,0xdb,0xe3); /* FNINIT must bypass busy. */
            CHECK(c->ip==0x502 && !n.transfer_blocked && n.tags==0xffff);
            step(c,0x9b,0x90);CHECK(c->ip==0x503);
        }
        e86_del(c);
    }
    for(unsigned model=0;model<2;++model) for(unsigned iem=0;iem<2;++iem)
    for(unsigned mask=0;mask<2;++mask) {
        e8086_t *c=cpu(model);e87_t n;e87_init(&n,model?E87_80287:E87_8087);
        if(!model) n.control=(n.control&~0x80u)|(iem<<7);
        n.control=(n.control&~1u)|mask;
        e86_set_npx_control(c,&n,e87_cpu_control);
        e86_set_npx_memory(c,e87_cpu_memory);
        e86_set_ds(c,0);e86_set_bx(c,0x600);
        memset(ram+0x600,0xa5,10);operand_writes=0;
        step(c,0xdb,0x3f);
        CHECK(c->ip==0x102 && !(c->state&E86_STATE_NPX_STOP));
        CHECK(operand_writes==(mask?10:0));
        CHECK(e87_error_signal(&n)==(!mask && (model || !iem)));
        CHECK(!!(n.status&0x8000)==(!mask && !model && !iem));
        CHECK((n.status&0xc1)==(mask?1:0x81));
        CHECK(e87_top(&n)==(mask?1:0) && n.tags==0xffff);
        if(!mask) for(unsigned i=0;i<10;++i) CHECK(ram[0x600+i]==0xa5);
        else CHECK(ram[0x607]==0xc0 && ram[0x608]==0xff && ram[0x609]==0xff);
        e86_del(c);
    }
    printf("NPX guest memory: %u checks, %u failures\n",checks,failures);
    return failures!=0;
}
