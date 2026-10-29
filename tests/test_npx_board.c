/* Production machine integration, no copied device implementation. */
#include <arch/ibmpc/ibmpc.h>
#include <arch/ibmpc/main.h>
#include <lib/log.h>
#include <stdio.h>
#include <string.h>
static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) {printf("FAIL line %u: %s\n",__LINE__,#x); failures++;}} while(0)
static ibmpc_t *machine(const char *sys,const char *cpu,const char *npx)
{
    ini_sct_t *cfg=ini_sct_new(NULL);
    ibmpc_t *pc;
    ini_set_str(cfg,"system.model",sys);
    ini_set_uint32(cfg,"system.patch_bios_init",0);
    ini_set_str(cfg,"cpu.model",cpu);
    if(npx) ini_set_str(cfg,"npx.model",npx);
    ini_set_uint32(cfg,"npx.timing",0);
    ini_set_uint32(cfg,"ram.size",640*1024);
    ini_set_str(cfg,"terminal.driver","null");
    ini_set_str(cfg,"video.device","cga");
    pc=pc_new(cfg);
    if(!pc) ini_sct_del(cfg);
    return pc;
}
static void destroy(ibmpc_t *pc)
{
    pc_del(pc);
}
static void code(ibmpc_t *pc,const unsigned char *bytes,unsigned size)
{
    e8086_t *c=pc->cpu;
    for(unsigned i=0;i<size;i++) mem_set_uint8(pc->mem,0x100+i,bytes[i]);
    c->state=0; c->prefix=0; c->msw=0;
    e86_set_cs(c,0); e86_set_ds(c,0); e86_set_ss(c,0);
    e86_set_sp(c,0x8000); e86_set_ip(c,0x100); e86_set_flags(c,2);
    c->pq_cnt=0;
}
static void steps(ibmpc_t *pc,unsigned n) {while(n--) e86_execute(pc->cpu);}
static void test(const char *sys,const char *cpu,const char *npx)
{
    ibmpc_t *pc=machine(sys,cpu,npx);
    unsigned at=!strcmp(sys,"5170");
    const unsigned char arithmetic[]={0xdb,0xe3,0xd9,0xe8,0xd9,0xe8,0xde,0xc1,0xdf,0x1e,0,3,0xf4};
    /* FNINIT; FLDCW [302]; FLD1; FLDZ; FDIVP ST1,ST0; WAIT */
    const unsigned char divide[]={0xdb,0xe3,0xd9,0x2e,2,3,0xd9,0xe8,0xd9,0xee,0xde,0xf9,0x9b,0x90};
    CHECK(pc!=NULL); if(!pc) return;
    CHECK(pc->npx.enabled);
    CHECK(pc_set_cpu_model(pc,"v20")!=0);
    CHECK(pc_set_cpu_model(pc,cpu)==0);
    if(at) CHECK(pc->at_rtc->data[0x14]&2);
    else {
        e86_set_prt8(pc->cpu,0x61,0x80);
        if(!strcmp(sys,"5150")) CHECK(e86_get_prt8(pc->cpu,0x60)&2);
        else CHECK(e86_get_prt8(pc->cpu,0x62)&2);
    }
    code(pc,arithmetic,sizeof arithmetic); steps(pc,6);
    CHECK(mem_get_uint16_le(pc->mem,0x300)==2);
    CHECK(!(pc->cpu->state&E86_STATE_NPX_STOP));
    mem_set_uint16_le(pc->mem,0x302,0x037b);
    code(pc,divide,sizeof divide); steps(pc,5);
    CHECK(pc->npx.core.status&4);
    CHECK(pc->npx.error);
    CHECK(pc->cpu->ip==0x10c);
    steps(pc,1); CHECK(pc->cpu->ip==0x10c); /* WAIT stalls */
    CHECK(!(pc->cpu->state&E86_STATE_NPX_STOP));
    if(at) {
        CHECK(pc->pic2.irq_inp&0x20);
        CHECK(pc->npx.busy_latch);
        CHECK(pc->cpu->int_vec!=16);
        e86_set_prt8(pc->cpu,0xf0,0);
        CHECK(!(pc->pic2.irq_inp&0x20));
        CHECK(pc->npx.core.status&4);
        steps(pc,1); CHECK(pc->cpu->ip==0x10d);
        CHECK(!pc->npx.busy_latch); /* no relatch of acknowledged ERROR */
        const unsigned char clear[]={0xdb,0xe2};
        code(pc,clear,sizeof clear); steps(pc,1);
        CHECK(!(pc->npx.core.status&0x80));
        CHECK(!pc->npx.error);
        const unsigned char pm[]={0xdb,0xe4};
        code(pc,pm,sizeof pm);steps(pc,1);
        CHECK(pc->npx.core.protected_mode);
        e86_reset(pc->cpu);
        CHECK(pc->npx.core.protected_mode);
        CHECK(pc->cpu->npx_control!=NULL);
        e86_set_prt8(pc->cpu,0xf1,0);
        CHECK(!pc->npx.core.protected_mode);
        CHECK(pc->npx.core.tags==0xffff);
        /* CMOS remains guest writable; CPU reset does not replace it. */
        e86_set_prt8(pc->cpu,0x70,0x14);e86_set_prt8(pc->cpu,0x71,0);
        e86_reset(pc->cpu);CHECK(pc->at_rtc->data[0x14]==0);
    } else {
        CHECK(!pc->cpu->nmi_pending); /* disabled at reset */
        mem_set_uint16_le(pc->mem,8,0x400);mem_set_uint16_le(pc->mem,10,0);
        mem_set_uint8(pc->mem,0x400,0xdb);mem_set_uint8(pc->mem,0x401,0xe2);
        mem_set_uint8(pc->mem,0x402,0xcf); /* FNCLEX; IRET */
        e86_set_prt8(pc->cpu,0xa0,0x80);
        CHECK(pc->cpu->nmi_pending);
        steps(pc,1); CHECK(pc->cpu->ip==0x400);CHECK(pc->cpu->int_vec==2);
        CHECK(pc->cpu->nmi_blocked); /* delivered despite IF=0 */
        steps(pc,1); CHECK(!pc->npx.error);CHECK(pc->cpu->ip==0x402);
        steps(pc,1); CHECK(!pc->cpu->nmi_blocked);CHECK(pc->cpu->ip==0x10c);
        steps(pc,1); CHECK(pc->cpu->ip==0x10d);
        /* Edge, not level, and HALT wake-up. */
        pc->cpu->state=E86_STATE_HALT;
        e86_nmi(pc->cpu,1);steps(pc,1);CHECK(pc->cpu->ip==0x400);
        CHECK(!(pc->cpu->state&E86_STATE_HALT));
        e86_nmi(pc->cpu,1);CHECK(!pc->cpu->nmi_pending);
        e86_reset(pc->cpu);
        const unsigned char ssload[]={0xb8,0,0,0x8e,0xd0,0x90,0x90};
        code(pc,ssload,sizeof ssload);steps(pc,2);
        e86_nmi(pc->cpu,1);steps(pc,1);CHECK(pc->cpu->ip==0x106);
        steps(pc,1);CHECK(pc->cpu->ip==0x400);
    }
    pc_reset(pc);
    CHECK(pc->npx.core.tags==0xffff);CHECK(!pc->npx.core.protected_mode);
    CHECK(!pc->npx.error && !pc->npx.busy_latch && !pc->npx.nmi_enabled);
    destroy(pc);
}
static void protected_irq(void)
{
    ibmpc_t *pc=machine("5170","80286","80287");
    e8086_t *c=pc->cpu;
    const unsigned char program[]={0xdb,0xe3,0xdb,0xe4,0xd9,0x2e,2,3,0xd9,0xe8,0xd9,0xee,0xde,0xf9,0x9b,0x90};
    const unsigned char handler[]={0x50,0x30,0xc0,0xe6,0xf0,0xdb,0xe2,0xb0,0x20,0xe6,0xa0,0xe6,0x20,0x58,0xcf};
    code(pc,program,sizeof program);
    for(unsigned i=0;i<sizeof handler;i++) mem_set_uint8(pc->mem,0x500+i,handler[i]);
    c->gdtr_base=0x2000;c->gdtr_limit=0x1f;
    for(unsigned i=1;i<=3;i++) {
        mem_set_uint16_le(pc->mem,0x2000+i*8,0xffff);
        mem_set_uint8(pc->mem,0x2005+i*8,i==1?0x9b:0x93);
    }
    c->sreg[E86_REG_CS]=8;c->sreg[E86_REG_SS]=16;c->sreg[E86_REG_DS]=24;
    c->seg_base[E86_REG_CS]=c->seg_base[E86_REG_SS]=c->seg_base[E86_REG_DS]=0;
    c->seg_ar[E86_REG_CS]=0x9b;c->seg_ar[E86_REG_SS]=c->seg_ar[E86_REG_DS]=0x93;
    c->idtr_base=0x3000;c->idtr_limit=0x3af;
    mem_set_uint16_le(pc->mem,0x3000+0x75*8,0x500);
    mem_set_uint16_le(pc->mem,0x3002+0x75*8,8);
    mem_set_uint8(pc->mem,0x3005+0x75*8,0x86);
    c->msw=1;
    mem_set_uint16_le(pc->mem,0x302,0x037b);
    const unsigned ports[]={0x20,0x21,0x21,0x21,0xa0,0xa1,0xa1,0xa1,0x21,0xa1};
    const unsigned values[]={0x11,8,4,1,0x11,0x70,2,1,0xfb,0xdf};
    for(unsigned i=0;i<10;i++) e86_set_prt8(c,ports[i],values[i]);
    e86_set_flags(c,0x202);
    steps(pc,6); /* divide raises IRQ13 through the two real PICs */
    CHECK(pc->npx.core.protected_mode);
    CHECK(pc->npx.core.instruction_selector==8);
    CHECK(pc->npx.core.instruction_pointer==0x10c);
    CHECK(pc->pic2.irq_inp&0x20);
    steps(pc,1); CHECK(c->int_vec==0x75);CHECK(c->ip==0x500);
    CHECK(pc->pic2.isr&0x20);CHECK(pc->pic.isr&4);
    steps(pc,9); /* handler clears latch/status, EOIs and IRETs */
    CHECK(c->ip==0x10e);CHECK(c->msw&1);
    CHECK(!pc->npx.busy_latch && !pc->npx.error);
    CHECK(!(pc->pic2.isr&0x20) && !(pc->pic.isr&4));
    steps(pc,1);CHECK(c->ip==0x10f);
    destroy(pc);
}
static void stop_diagnostic(void)
{
    ibmpc_t *pc=machine("5170","80286","80287");
    const unsigned char unsupported[]={0xdb,0xef};
    char message[512]={0};
    FILE *log=tmpfile();
    CHECK(pc!=NULL && log!=NULL);
    if (!pc || !log) {if(pc) destroy(pc);if(log) fclose(log);return;}
    pce_log_add_fp(log,0,MSG_ERR);
    code(pc,unsupported,sizeof unsupported);
    pc_clock(pc,1);
    CHECK(pc->cpu->state&E86_STATE_NPX_STOP);
    fflush(log);long size=ftell(log);CHECK(size>0);
    pc_clock(pc,1);fflush(log);CHECK(ftell(log)==size);
    rewind(log);CHECK(fread(message,1,sizeof message-1,log)>0);
    CHECK(strstr(message,"0000:0100 opcode=DB EF model=80287")!=NULL);
    CHECK(strstr(message,"CW=") && strstr(message,"SW=") && strstr(message,"TAG="));
    /* Clearing the CPU stop arms a new diagnostic without resetting the NPX. */
    pc->cpu->state=0;pc_npx_check_stop(pc);CHECK(!pc->npx.stop_reported);
    fseek(log,0,SEEK_END);pc->cpu->state=E86_STATE_NPX_STOP;
    pc_npx_check_stop(pc);fflush(log);CHECK(ftell(log)>size);
    pce_log_rmv_fp(log);fclose(log);destroy(pc);
}
static void execution_timing(void)
{
    ibmpc_t *pc=machine("5170","80286","80287");
    const unsigned char program[]={0xd9,0xe8,0xd9,0xfa,0x90,0x9b,0x90};
    code(pc,program,sizeof program);pc->npx.timing=1;
    steps(pc,2);CHECK(pc->npx.busy_cycles==183);
    steps(pc,1);CHECK(pc->cpu->ip==0x105); /* Integer NOP overlaps. */
    steps(pc,1);CHECK(pc->cpu->ip==0x105); /* WAIT stalls. */
    e86_clock(pc->cpu,200);CHECK(pc->cpu->ip!=0x105);
    CHECK(!pc->npx.busy_cycles);
    code(pc,program,sizeof program);steps(pc,2);
    CHECK(pc->npx.busy_cycles);pc_npx_write(pc,0xf1,0);
    CHECK(!pc->npx.busy_cycles);
    destroy(pc);
}
int main(void)
{
    par_terminal="null";
    const char *systems[]={"5150","5160","5170"};
    const char *cpus[]={"8088","8086","80286"};
    for(unsigned i=0;i<3;i++) {
        ibmpc_t *pc=machine(systems[i],cpus[i],NULL);
        CHECK(pc!=NULL);CHECK(!pc->npx.enabled);CHECK(pc->cpu->npx_control==NULL);
        if(i==2) CHECK(!(pc->at_rtc->data[0x14]&2));
        destroy(pc);
    }
    CHECK(machine("5160","v20","8087")==NULL);
    CHECK(machine("5160","80186","8087")==NULL);
    CHECK(machine("5170","80286","8087")==NULL);
    CHECK(machine("5160","8088","80287")==NULL);
    CHECK(machine("5170","80286","80387")==NULL);
    test("5150","8088","8087");test("5160","8086","8087");
    test("5170","80286","80287");
    protected_irq();
    stop_diagnostic();
    execution_timing();
    printf("NPX board: %u checks, %u failures\n",checks,failures);
    return failures!=0;
}
