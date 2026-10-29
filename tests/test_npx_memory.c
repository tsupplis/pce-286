/* PCE NPX CPU-interface regression. GPL-2.0-or-later. */
#include <cpu/e8086/e8086.h>
#include <cpu/e8087/execute.h>
#include <stdio.h>
#include <string.h>
static unsigned char ram[65536];
static unsigned checks, failures, calls, operand_writes, priority_reads, priority_writes;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; fprintf(stderr,"line %d: %s\n",__LINE__,#x); } } while (0)
static unsigned char r8(void *p,unsigned long a) { (void)p; if(a>=0x9000 && a<0x900a) ++priority_reads; return ram[a&65535]; }
static unsigned short r16(void *p,unsigned long a) { return r8(p,a)|(r8(p,a+1)<<8); }
static void w8(void *p,unsigned long a,unsigned char v) { (void)p; if(a>=0x9000 && a<0x900a) ++priority_writes; if(a>=0x600 && a<0x60a) ++operand_writes; ram[a&65535]=v; }
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
static void guest_comparisons_conversions(void)
{
    /* Every new memory encoding, with complete access, first-word fault and
     * later-transfer overrun. Two-byte operands have no later-word case. */
    static const struct {unsigned op,mr,size,write;} forms[]={
      {0xd8,0x10,4,0},{0xd8,0x18,4,0},{0xdc,0x10,8,0},{0xdc,0x18,8,0},
      {0xda,0x10,4,0},{0xda,0x18,4,0},{0xde,0x10,2,0},{0xde,0x18,2,0},
      {0xd9,0,4,0},{0xd9,0x10,4,1},{0xd9,0x18,4,1},
      {0xdd,0,8,0},{0xdd,0x10,8,1},{0xdd,0x18,8,1},
      {0xdb,0,4,0},{0xdb,0x10,4,1},{0xdb,0x18,4,1},
      {0xdf,0,2,0},{0xdf,0x10,2,1},{0xdf,0x18,2,1},
      {0xdf,0x28,8,0},{0xdf,0x38,8,1},{0xdf,0x20,10,0},{0xdf,0x30,10,1}};
    for(unsigned m=0;m<2;++m)for(unsigned k=0;k<sizeof(forms)/sizeof(forms[0]);++k)
    for(unsigned edge=0;edge<(m?(forms[k].size>2?3:2):1);++edge){
        e8086_t *c=cpu(m);e87_t n,before,expected;e87_init(&n,m);n.tags=0xfffc;
        n.reg[0]=(e87_real80_t){UINT64_C(0x8000000000000000),0x4000};
        unsigned char bytes[10]={0},want[10]={0};
        if(k<4 || (k>=8 && k<14))bytes[forms[k].size-1]=0x40;else bytes[0]=2;
        memcpy(ram+0x600,bytes,10);ram[0x5ff]=ram[0x60a]=0xa5;
        if(m){protected_setup(c);c->seg_limit[E86_REG_DS]=edge==0?0x600+forms[k].size-1:edge==1?0x600:0x600+forms[k].size-2;}
        e86_set_bx(c,0x600);e86_set_npx_control(c,&n,e87_cpu_control);
        e86_set_npx_memory(c,e87_cpu_memory);e86_set_npx_event(c,e87_cpu_event);e86_set_npx_address(c,e87_cpu_address);
        before=n;expected=n;
        CHECK(e87_numeric(&expected,forms[k].op,forms[k].mr,forms[k].write?0:bytes,forms[k].write?want:0));
        operand_writes=0;step(c,forms[k].op,forms[k].mr|7);
        CHECK(!(c->state&E86_STATE_NPX_STOP));
        if(!edge){
            CHECK(c->ip==0x102 && n.status==expected.status && n.tags==expected.tags);
            for(unsigned j=0;j<8;++j)CHECK(n.reg[j].significand==expected.reg[j].significand&&n.reg[j].sign_exponent==expected.reg[j].sign_exponent);
            CHECK(!memcmp(ram+0x600,forms[k].write?want:bytes,forms[k].size));
            CHECK(operand_writes==(forms[k].write?forms[k].size:0));
            CHECK(n.instruction_pointer==0x100 && n.operand_pointer==0x600);
            CHECK(n.opcode==(((forms[k].op&7)<<8)|forms[k].mr|7));
        }else{
            CHECK(c->ip==0x500 && n.status==before.status && n.tags==before.tags);
            CHECK(!memcmp(n.reg,before.reg,sizeof n.reg) && !memcmp(ram+0x600,bytes,10));
            CHECK(!operand_writes && !n.instruction_pointer && !n.operand_pointer);
            CHECK(n.transfer_blocked==(edge==2));
        }
        CHECK(ram[0x5ff]==0xa5 && ram[0x60a]==0xa5);e86_del(c);
    }
    /* Actual CPU suppression of invalid, overflow and underflow stores. */
    for(unsigned m=0;m<2;++m)for(unsigned which=0;which<3;++which){
        e8086_t *c=cpu(m);e87_t n;e87_init(&n,m);n.tags=0xfffc;
        unsigned exception=which==0?1:which==1?8:0x10;
        n.control&=~exception;n.reg[0]=(e87_real80_t){UINT64_C(0x8000000000000000),which==0?0x401f:which==1?0x407f:0x3f80};
        e86_set_bx(c,0x600);e86_set_npx_control(c,&n,e87_cpu_control);e86_set_npx_memory(c,e87_cpu_memory);
        e86_set_npx_event(c,e87_cpu_event);memset(ram+0x600,0xa5,10);operand_writes=0;
        step(c,which==0?0xdb:0xd9,0x1f);
        CHECK(c->ip==0x102 && !operand_writes && e87_top(&n)==0 && n.tags==0xfffc);
        CHECK((n.status&0xbf)==(0x80|exception));for(unsigned j=0;j<10;++j)CHECK(ram[0x600+j]==0xa5);
        e86_del(c);
    }
    const unsigned ops[]={0xd8,0xd8,0xde,0xd9,0xd9,0xd9,0xd9,0xdd,0xdd};
    const unsigned mrs[]={0xd1,0xd9,0xd9,0xe4,0xe5,0xc0,0xc9,0xd1,0xd9};
    for(unsigned m=0;m<2;++m)for(unsigned k=0;k<9;++k){
        e8086_t *c=cpu(m);e87_t n;e87_init(&n,m);n.tags=0xfff0;
        n.reg[0]=n.reg[1]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff};
        e86_set_npx_control(c,&n,e87_cpu_control);e86_set_npx_address(c,e87_cpu_address);
        step(c,ops[k],mrs[k]);CHECK(c->ip==0x102 && !(c->state&E86_STATE_NPX_STOP));
        CHECK(n.opcode==(((ops[k]&7)<<8)|mrs[k]) && n.instruction_pointer==0x100);e86_del(c);
    }
}
static void guest_memory_arithmetic(void)
{
    const unsigned ops[]={0xd8,0xdc,0xda,0xde},sizes[]={4,8,4,2},groups[]={0,1,4,5,6,7};
    for(unsigned model=0;model<2;++model) for(unsigned f=0;f<4;++f)
    for(unsigned g=0;g<6;++g) for(unsigned edge=0;edge<(model?3:1);++edge) {
        e8086_t *c=cpu(model);e87_t n,before;e87_init(&n,model);n.tags=0xfffc;
        n.reg[0]=(e87_real80_t){UINT64_C(0x8000000000000000),0x4002};
        uint64_t raw=f==0?0x40000000:f==1?UINT64_C(0x4000000000000000):2;
        for(unsigned j=0;j<sizes[f];++j)ram[0x600+j]=raw>>(8*j);
        if(model) {protected_setup(c);c->seg_limit[E86_REG_DS]=edge==0?0x600+sizes[f]-1:edge==1?0x600:0x600+sizes[f]-2;}
        e86_set_bx(c,0x600);e86_set_npx_control(c,&n,e87_cpu_control);
        e86_set_npx_memory(c,e87_cpu_memory);e86_set_npx_event(c,e87_cpu_event);e86_set_npx_address(c,e87_cpu_address);
        before=n;operand_writes=0;step(c,ops[f],(groups[g]<<3)|7);
        CHECK(!(c->state&E86_STATE_NPX_STOP) && operand_writes==0);
        if(!edge) {
            CHECK(c->ip==0x102 && n.instruction_pointer==0x100 && n.operand_pointer==0x600);
            CHECK(n.opcode==(((ops[f]&7)<<8)|(groups[g]<<3)|7));
            CHECK(n.reg[0].significand && e87_top(&n)==0 && n.tags==0xfffc);
        } else {
            CHECK(c->ip==0x500 && n.status==before.status && n.tags==before.tags);
            CHECK(n.reg[0].significand==before.reg[0].significand && n.reg[0].sign_exponent==before.reg[0].sign_exponent);
            CHECK(n.instruction_pointer==before.instruction_pointer && n.operand_pointer==before.operand_pointer);
            CHECK(n.transfer_blocked==(edge==2 && sizes[f]>2));
        }
        e86_del(c);
    }
    const unsigned unary[]={0xe0,0xe1,0xf4,0xfa,0xfc,0xfd,0xf8};
    for(unsigned model=0;model<2;++model)for(unsigned k=0;k<7;++k) {
        e8086_t *c=cpu(model);e87_t n;e87_init(&n,model);n.tags=0xfff0;
        n.reg[0]=(e87_real80_t){UINT64_C(0x8000000000000000),0x4001};n.reg[1]=n.reg[0];
        e86_set_npx_control(c,&n,e87_cpu_control);e86_set_npx_address(c,e87_cpu_address);
        step(c,0xd9,unary[k]);CHECK(c->ip==0x102 && !(c->state&E86_STATE_NPX_STOP));
        CHECK(n.opcode==(0x100|unary[k]) && n.instruction_pointer==0x100);e86_del(c);
    }
}
static void guest_product(void)
{
    const unsigned ops[]={0xd8,0xdc,0xde}, groups[]={0xc9,0xf1,0xf9};
    for(unsigned model=0;model<2;++model) for(unsigned f=0;f<3;++f) for(unsigned k=0;k<3;++k) {
        e8086_t *c=cpu(model);e87_t n;e87_init(&n,model);n.tags=0xfff0;
        n.reg[0]=(e87_real80_t){UINT64_C(0x8000000000000000),0x4001};
        n.reg[1]=(e87_real80_t){UINT64_C(0x8000000000000000),0x4000};
        e86_set_npx_control(c,&n,e87_cpu_control);e86_set_npx_address(c,e87_cpu_address);
        step(c,ops[f],groups[k]);
        CHECK(c->ip==0x102 && !(c->state&E86_STATE_NPX_STOP));
        CHECK(n.reg[f?1:0].sign_exponent==(k==0?0x4002:k==1?0x4000:0x3ffe));
        CHECK(e87_top(&n)==(f==2) && n.opcode==(((ops[f]&7)<<8)|groups[k]));
        e86_del(c);
    }
}
static void guest_subtraction(void)
{
    const unsigned ops[]={0xd8,0xdc,0xde};
    for(unsigned model=0;model<2;++model) for(unsigned f=0;f<3;++f)
    for(unsigned reverse=0;reverse<2;++reverse) {
        e8086_t *c=cpu(model);e87_t n;e87_init(&n,model);
        n.tags=0xfff0;
        n.reg[0]=(e87_real80_t){UINT64_C(0xc000000000000000),0x4000};
        n.reg[1]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3fff};
        e86_set_npx_control(c,&n,e87_cpu_control);e86_set_npx_event(c,e87_cpu_event);
        e86_set_npx_address(c,e87_cpu_address);
        unsigned mr=reverse?0xe9:0xe1;
        step(c,ops[f],mr);
        CHECK(c->ip==0x102 && n.reg[f?1:0].sign_exponent==(reverse?0xc000:0x4000));
        CHECK(n.reg[f?1:0].significand==UINT64_C(0x8000000000000000));
        CHECK(e87_top(&n)==(f==2));
        CHECK(n.instruction_pointer==0x100 && n.opcode==(((ops[f]&7)<<8)|mr));
        e86_del(c);
    }
}
static void guest_nop(void)
{
    for(unsigned model=0;model<2;++model) {
        e8086_t *c=cpu(model);e87_t n,before;e87_init(&n,model);
        e86_set_npx_control(c,&n,e87_cpu_control);e86_set_npx_event(c,e87_cpu_event);
        e86_set_npx_address(c,e87_cpu_address);
        n.status=0x4500;n.tags=0x1234;n.instruction_pointer=0xabcde;n.operand_pointer=0x12345;
        before=n;step(c,0xd9,0xd0);
        CHECK(c->ip==0x102 && !memcmp(&n,&before,sizeof n));
        n.control&=~1u;e87_raise_exception(&n,1);before=n;w16(0,16*4,0x500);
        step(c,0xd9,0xd0);
        CHECK(c->ip==(model?0x500:0x104));
        CHECK(!memcmp(&n,&before,sizeof n));
        e86_del(c);
    }
}
static void pointer_capture(void)
{
    for(unsigned late=0;late<2;++late) {
        e8086_t *c=cpu(1);e87_t n;
        e87_init(&n,E87_80287);protected_setup(c);n.protected_mode=1;
        n.instruction_pointer=0x1234;n.operand_pointer=0x5678;n.opcode=0x321;
        n.instruction_selector=8;n.operand_selector=24;
        e86_set_npx_control(c,&n,e87_cpu_control);e86_set_npx_memory(c,e87_cpu_memory);
        e86_set_npx_event(c,e87_cpu_event);e86_set_npx_address(c,e87_cpu_address);
        c->seg_limit[E86_REG_DS]=late?0x608:0x600;e86_set_bx(c,0x600);
        step(c,0xdb,0x2f);
        CHECK(c->ip==0x500 && n.transfer_blocked==late);
        CHECK(n.instruction_pointer==0x1234 && n.operand_pointer==0x5678);
        CHECK(n.opcode==0x321 && n.instruction_selector==8 && n.operand_selector==24);
        e86_del(c);
    }
    for(unsigned kind=0;kind<3;++kind) {
        e8086_t *c=cpu(kind!=0); e87_t n;
        e87_init(&n,kind ? E87_80287 : E87_8087);
        if(kind==2) { protected_setup(c); n.protected_mode=1; }
        e86_set_es(c,0xa020);
        /* Explicit cached base avoids selector lookup and tests segment override. */
        if(kind) { c->seg_base[E86_REG_ES]=0xa0200; c->seg_ar[E86_REG_ES]=0x93; }
        e86_set_bx(c,0x600);
        e86_set_npx_control(c,&n,e87_cpu_control);e86_set_npx_memory(c,e87_cpu_memory);
        e86_set_npx_event(c,e87_cpu_event);e86_set_npx_address(c,e87_cpu_address);
        ram[0x807]=0x80;ram[0x808]=0xff;ram[0x809]=0x3f;
        ram[0x101]=0xdb;ram[0x102]=0x2f;
        step(c,0x26,0xdb); /* ES: FLD tword [BX] */
        CHECK(c->ip==0x103);
        CHECK(n.instruction_pointer==(kind?0x100:0x101));
        CHECK(n.operand_pointer==(kind==2?0x600:0xa0800));
        CHECK(n.opcode==0x32f);
        if(kind==2) CHECK(n.instruction_selector==8 && n.operand_selector==0xa020);
        step(c,0xd8,0xc0);
        CHECK(n.instruction_pointer==0x103 && n.opcode==0xc0);
        CHECK(n.operand_pointer==(kind==2?0x600:0xa0800));
        if(kind) {
            unsigned char image[94]; e87_t copy=n;
            CHECK(e87_memory_control(&copy,0xdd,0x30,0,image)==94);
            e86_set_bx(c,0x900);step(c,0xdd,0x37);
            CHECK(!memcmp(ram+0x900,image,94));
            CHECK(n.instruction_pointer==0); /* FNSAVE reset not overwritten */
        }
        e86_reset(c);CHECK(c->npx_address==e87_cpu_address);
        e86_set_npx_control(c,0,0);CHECK(c->npx_address==0);
        e86_del(c);
    }
}
static void guest_environment(void)
{
    const unsigned ops[]={0xd9,0xd9,0xdd,0xd9,0xdd,0xdd,0xd9};
    const unsigned forms[]={5,7,7,4,4,6,6}, sizes[]={2,2,2,14,94,94,14};
    for(unsigned model=0;model<2;++model) for(unsigned k=0;k<7;++k) {
        e8086_t *c=cpu(model);e87_t n,expected,source;unsigned char image[94], want[94];
        e87_init(&source,model);source.tags=0x1234;source.status=0x2800;
        source.reg[5]=(e87_real80_t){UINT64_C(0x8765432101234567),0x4001};
        CHECK(e87_memory_control(&source,0xdd,0x30,0,image)==94);
        e87_init(&n,model);n.tags=0xfffc;n.reg[0].significand=123;
        if(forms[k]>=6) { n.control&=~0x81u;e87_raise_exception(&n,1); }
        expected=n;memcpy(want,image,94);
        CHECK(e87_memory_control(&expected,ops[k],forms[k]<<3,0,want)==sizes[k]);
        memcpy(ram+0x600,image,94);ram[0x5ff]=0xa5;ram[0x600+sizes[k]]=0xa5;
        e86_set_bx(c,0x600);e86_set_npx_control(c,&n,e87_cpu_control);
        e86_set_npx_memory(c,e87_cpu_memory);e86_set_npx_event(c,e87_cpu_event);
        e86_set_npx_address(c,e87_cpu_address);
        step(c,ops[k],(forms[k]<<3)|7);
        CHECK(c->ip==0x102 && !(c->state&E86_STATE_NPX_STOP));
        CHECK(!memcmp(&n,&expected,sizeof n));
        CHECK(!memcmp(ram+0x600,forms[k]>=6?want:image,sizes[k]));
        CHECK(ram[0x5ff]==0xa5 && ram[0x600+sizes[k]]==0xa5);
        if(k==6) { step(c,0x9b,0x90); CHECK(c->ip==0x103); }
        e86_del(c);
    }
}
/* Exercise each staged control through actual ESC decoding, including edges. */
static void memory_controls(void)
{
    const unsigned ops[]={0xd9,0xd9,0xdd,0xd9,0xdd,0xdd,0xd9};
    const unsigned forms[]={5,7,7,4,4,6,6}, sizes[]={2,2,2,14,94,94,14};
    for(unsigned k=0;k<7;++k) for(unsigned edge=0;edge<3;++edge) {
        e8086_t *c=cpu(1); e87_t n,before; unsigned char image[94];
        e87_init(&n,E87_80287); n.status=0x1800; n.tags=0x1234;
        n.reg[3]=(e87_real80_t){UINT64_C(0x8123456789abcdef),0x4000};
        e87_t saved=n; CHECK(e87_memory_control(&saved,0xdd,0x30,0,image)==94);
        memcpy(ram+0x600,image,94); ram[0x5ff]=0xa5;ram[0x600+sizes[k]]=0xa5;
        protected_setup(c);c->seg_limit[E86_REG_DS]=edge==0 ? 0x600+sizes[k]-1 : edge==1 ? 0x600 : 0x600+sizes[k]-2;
        e86_set_bx(c,0x600);e86_set_npx_control(c,&n,e87_cpu_control);
        e86_set_npx_memory(c,e87_cpu_memory);e86_set_npx_event(c,e87_cpu_event);
        before=n; unsigned char old[94];memcpy(old,ram+0x600,94);
        step(c,ops[k],(forms[k]<<3)|7);
        CHECK(!(c->state&E86_STATE_NPX_STOP));
        if(edge==0) {
            CHECK(c->ip==0x102);
            CHECK(ram[0x5ff]==0xa5 && ram[0x600+sizes[k]]==0xa5);
            if(k==5) { CHECK(n.tags==0xffff && n.status==0); CHECK(!memcmp(ram+0x600,image,94)); }
            else if(k==1) CHECK(r16(0,0x600)==before.control);
            else if(k==2) CHECK(r16(0,0x600)==before.status);
            else CHECK(n.status==before.status && n.tags==before.tags);
        } else {
            CHECK(c->ip==0x500);
            CHECK(!memcmp(old,ram+0x600,94));
            CHECK(n.control==before.control && n.status==before.status && n.tags==before.tags);
            CHECK(n.transfer_blocked==(edge==2 && sizes[k]>2));
        }
        e86_del(c);
    }
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
/* Simultaneously pending #7 and operand faults, for loads and stores.
 * Check the frame, absence of operand I/O and unchanged NPX state. */
static void device_not_available_priority(void)
{
    for(unsigned bits=1;bits<4;++bits) for(unsigned store=0;store<2;++store)
    for(unsigned stack=0;stack<2;++stack) for(unsigned edge=0;edge<3;++edge) {
        e8086_t *c=cpu(1);e87_t n,before;e87_init(&n,E87_80287);
        protected_setup(c);
        w16(0,0x3038,0x400);w16(0,0x303a,8);ram[0x303d]=0x86;
        unsigned reg=stack?E86_REG_SS:E86_REG_DS;
        /* SS also hosts the exception frame: put the operand above SP. */
        unsigned operand=0x9000;
        c->seg_limit[reg]=edge==0?operand:edge==1?operand+1:0xffff;
        if(edge==2 && !stack)c->seg_ar[reg]=0; /* null/unusable DS */
        e86_set_bx(c,operand);e86_set_bp(c,operand);e86_set_si(c,0);
        c->msw|=bits<<2;
        e86_set_npx_control(c,&n,e87_cpu_control);
        e86_set_npx_memory(c,e87_cpu_memory);e86_set_npx_event(c,e87_cpu_event);
        e86_set_npx_address(c,e87_cpu_address);
        before=n;memset(ram+operand,0xa5,10);priority_reads=priority_writes=0;
        step(c,0xdb,(store?0x38:0x28)|(stack?2:7));
        CHECK(c->int_vec==7 && c->ip==0x400);
        CHECK(priority_reads==0 && priority_writes==0);
        CHECK(e86_get_sp(c)==0x7ffa && r16(0,0x7ffa)==0x100);
        CHECK(!memcmp(&n,&before,sizeof n));
        CHECK(!(c->state&E86_STATE_NPX_STOP));
        for(unsigned j=0;j<10;++j)CHECK(ram[operand+j]==0xa5);
        e86_del(c);
    }
}
/* Independent ESC memory-format table: signed size, negative means store.
 * Validate every ModR/M addressing encoding, including reserved groups and
 * register forms, without executing or mutating the NPX during size queries. */
static void memory_decode_audit(void)
{
    static const int formats[8][8]={
      {4,4,4,4,4,4,4,4}, {4,0,-4,-4,14,2,-14,-2},
      {4,4,4,4,4,4,4,4}, {4,0,-4,-4,0,10,0,-10},
      {8,8,8,8,8,8,8,8}, {8,0,-8,-8,94,0,-94,-2},
      {2,2,2,2,2,2,2,2}, {2,0,-2,-2,10,8,-10,-8}};
    for(unsigned model=0;model<2;++model)for(unsigned op=0xd8;op<=0xdf;++op)
    for(unsigned mr=0;mr<256;++mr) {
        e87_t n;e87_init(&n,model);n.tags=0x1234;n.status=0x4581;
        e87_t before=n;
        int expected=mr>=0xc0?0:formats[op-0xd8][(mr>>3)&7];
        int answer=e87_cpu_memory(&n,op,mr,1,0);
        unsigned direction=answer&3, size=answer>2?(unsigned)answer>>2:10;
        CHECK(expected ? direction==(expected<0?2:1) && size==(unsigned)(expected<0?-expected:expected) : answer==0);
        CHECK(!memcmp(&n,&before,sizeof n));
    }
}
static void pending_memory_control_priority(void)
{
    const unsigned ops[]={0xd9,0xd9,0xdd,0xd9,0xd9,0xdd,0xdd};
    const unsigned groups[]={4,5,4,6,7,6,7};
    for(unsigned k=0;k<7;++k)for(unsigned unavailable=0;unavailable<2;++unavailable) {
        e8086_t *c=cpu(1);e87_t n,before;e87_init(&n,E87_80287);
        protected_setup(c);c->idtr_limit=0x87;
        w16(0,0x3038,0x400);w16(0,0x303a,8);ram[0x303d]=0x86;
        w16(0,0x3080,0x600);w16(0,0x3082,8);ram[0x3085]=0x86;
        c->seg_limit[E86_REG_DS]=0x8fff;e86_set_bx(c,0x9000);
        if(unavailable)c->msw|=8;
        n.control&=~1u;e87_raise_exception(&n,1);before=n;
        e86_set_npx_control(c,&n,e87_cpu_control);e86_set_npx_memory(c,e87_cpu_memory);
        e86_set_npx_event(c,e87_cpu_event);e86_set_npx_address(c,e87_cpu_address);
        priority_reads=priority_writes=0;step(c,ops[k],(groups[k]<<3)|7);
        unsigned vector=unavailable?7:k<3?16:13;
        CHECK(c->int_vec==vector && c->ip==(vector==7?0x400:vector==16?0x600:0x500));
        CHECK(!memcmp(&n,&before,sizeof n));
        CHECK(!priority_reads && !priority_writes && !(c->state&E86_STATE_NPX_STOP));
        CHECK(e86_get_sp(c)==(vector==13?0x7ff8:0x7ffa));
        CHECK(r16(0,0x7ffa)==0x100);
        e86_del(c);
    }
}
static void guest_constants_transcendentals(void)
{
 const unsigned forms[]={0xe8,0xe9,0xea,0xeb,0xec,0xed,0xee,0xf0,0xf1,0xf3,0xf9,0xf2};
 for(unsigned m=0;m<2;++m)for(unsigned k=0;k<12;++k){
  e8086_t *c=cpu(m);e87_t n;e87_init(&n,m);n.tags=0xfff0;
  n.reg[0]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3ffd};
  n.reg[1]=(e87_real80_t){UINT64_C(0x8000000000000000),0x3ffc};n.operand_pointer=0x12345;
  e86_set_npx_control(c,&n,e87_cpu_control);e86_set_npx_address(c,e87_cpu_address);
  step(c,0xd9,forms[k]);CHECK(c->ip==0x102 && !(c->state&E86_STATE_NPX_STOP));
  CHECK(n.opcode==(0x100|forms[k])&&n.instruction_pointer==0x100);
  CHECK(n.operand_pointer==0x12345);CHECK(e87_top(&n)==(k<7||k==11?7:k==7?0:1));e86_del(c);
 }
}
int main(void)
{
    memory_decode_audit();
    pending_memory_control_priority();
    device_not_available_priority();
    guest_constants_transcendentals();
    guest_product();
    guest_memory_arithmetic();
    guest_comparisons_conversions();
    guest_subtraction();
    guest_nop();
    guest_environment();
    pointer_capture();
    memory_controls();
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
