/* PC/XT and AT coprocessor wiring. GPL-2.0-or-later. */
#include "ibmpc.h"
#include "npx.h"
#include <cpu/e8087/execute.h>
#include <lib/log.h>
#include <string.h>

/* Validate before constructing devices: no half-created machine on error. */
int pc_npx_config(ini_sct_t *cfg)
{
    const char *n, *cpu, *system;
    ini_get_string(ini_next_sct(cfg, NULL, "npx"), "model", &n, "none");
    if (!strcmp(n, "none")) return 0;
    ini_get_string(ini_next_sct(cfg, NULL, "cpu"), "model", &cpu, "8088");
    ini_get_string(ini_next_sct(cfg, NULL, "system"), "model", &system, "5150");
    if (!strcmp(n, "8087") && (!strcmp(system,"5150") || !strcmp(system,"5160")) &&
        (!strcmp(cpu,"8088") || !strcmp(cpu,"8086"))) return 1;
    if (!strcmp(n,"80287") && !strcmp(system,"5170") && !strcmp(cpu,"80286")) return 2;
    pce_log(MSG_ERR,"*** incompatible NPX configuration: system=%s cpu=%s npx=%s\n",system,cpu,n);
    return -1;
}

static void update(ibmpc_t *pc)
{
    pc_npx_t *b=&pc->npx;
    unsigned error=e87_error_signal(&b->core)!=0;
    if (b->core.model==E87_80287) {
        /* ERROR rising sets the external latch. F0 acknowledges it without
         * changing status; a still asserted ERROR must not relatch it. */
        if (error && !b->error) b->busy_latch=1;
        e8259_set_irq5(&pc->pic2,b->busy_latch);
    } else {
        e86_nmi(pc->cpu,error && b->nmi_enabled);
    }
    b->error=error;
}
/* Intel 286/287 PRM table 2-14 typical execution clocks. First timing
 * profile: register arithmetic and transcendental operations. Unlisted forms
 * retain functional timing; transfer cycles and exception penalties are not
 * modeled. One NPX clock per emulated CPU clock, independent of host speed. */
static unsigned latency(unsigned op,unsigned mr)
{
    if (mr<0xc0) return 0;
    if (op==0xd9) {
        switch(mr) {
        case 0xf0:return 500; case 0xf1:return 950; case 0xf2:return 450;
        case 0xf3:return 650; case 0xf9:return 850; case 0xfa:return 183;
        default:return 0;
        }
    }
    if(op==0xd8 || op==0xdc || op==0xde) {
        unsigned group=(mr>>3)&7,pop=op==0xde;
        if(group==0 || group==4 || group==5)return pop?90:85;
        /* General operands; short-significand fast path is not modeled. */
        if(group==1)return pop?142:138;
        if(group==6 || group==7)return (pop?202:198)+(op==0xd8 ? group==7 : group==6);
    }
    return 0;
}
static unsigned timed_busy(ibmpc_t *pc)
{
    pc_npx_t *b=&pc->npx;
    if(b->busy_cycles && pc->cpu->clock-b->busy_start>=b->busy_cycles)
        b->busy_cycles=0;
    return b->busy_cycles!=0;
}
static int control(void *ext,unsigned op,unsigned mr,unsigned short *ax)
{
    ibmpc_t *pc=ext;
    int done=e87_cpu_control(&pc->npx.core,op,mr,ax);
    if(done && pc->npx.timing) {
        if(op==0xdb && mr==0xe3)pc->npx.busy_cycles=0;
        else if(latency(op,mr)) {
            pc->npx.busy_start=pc->cpu->clock;
            pc->npx.busy_cycles=latency(op,mr);
        }
    }
    update(pc);
    return done;
}
static int memory(void *ext,unsigned op,unsigned mr,int query,unsigned char *bytes)
{
    ibmpc_t *pc=ext;
    int done=e87_cpu_memory(&pc->npx.core,op,mr,query,bytes);
    if (!query) update(pc);
    return done;
}
static void address(void *ext,const e86_npx_address_t *a)
{
    ibmpc_t *pc=ext;
    e87_cpu_address(&pc->npx.core,a);
}
static int event(void *ext,unsigned event)
{
    ibmpc_t *pc=ext;
    int result=0;
    /* IBM AT wires ERROR to IRQ13, not the processor's #16 input. */
    if (event==E86_NPX_ERROR) return 0;
    if (event==E86_NPX_WAIT || event==E86_NPX_OVERRUN)
        result=e87_cpu_event(&pc->npx.core,event);
    update(pc);
    if (event==E86_NPX_BUSY) {
        unsigned op=pc->cpu->pq[0], mr=pc->cpu->pq[1];
        unsigned no_wait=(op==0xdb && mr>=0xe0 && mr<=0xe4) ||
            (op==0xdf && mr==0xe0) ||
            (mr<0xc0 && (op==0xd9 || op==0xdd) && ((mr>>3)&7)>=6);
        /* Error-recovery controls remain usable. A transfer-overrun stall is
         * different and still needs FNINIT/hardware reset. */
        return pc->npx.core.transfer_blocked || (!no_wait &&
            (timed_busy(pc) || pc->npx.busy_latch || (pc->npx.core.model==E87_8087 && pc->npx.error)));
    }
    return result;
}
/* Report once per stopped episode, including stops outside the callbacks
 * (for example an unsupported 8087 transfer crossing the segment boundary). */
void pc_npx_check_stop(ibmpc_t *pc)
{
    pc_npx_t *b=&pc->npx;
    e8086_t *c=pc->cpu;
    if (!b->enabled || !(c->state & E86_STATE_NPX_STOP)) {
        b->stop_reported=0;
        return;
    }
    if (b->stop_reported) return;
    b->stop_reported=1;
    pce_log(MSG_ERR,"NPX: unsupported instruction/operand case; CPU stopped at "
        "%04X:%04X opcode=%02X %02X model=%s CW=%04X SW=%04X TAG=%04X\n",
        (unsigned)e86_get_cs(c),(unsigned)c->ip,(unsigned)c->pq[0],
        (unsigned)c->pq[1],b->core.model==E87_8087 ? "8087" : "80287",
        (unsigned)b->core.control,(unsigned)b->core.status,(unsigned)b->core.tags);
    {
        unsigned i;
        for (i=0;i<2;i++) {
            e87_real80_t v=b->core.reg[e87_index(&b->core,i)];
            pce_log(MSG_ERR,"NPX: ST(%u)=%04X:%08lX%08lX\n",i,
                (unsigned)v.sign_exponent,(unsigned long)(v.significand>>32),
                (unsigned long)(v.significand&0xffffffffUL));
        }
    }
}
void pc_npx_reset(ibmpc_t *pc)
{
    pc_npx_t *b=&pc->npx;
    b->error=b->busy_latch=b->nmi_enabled=b->stop_reported=0;
    b->busy_cycles=0;
    if (!b->enabled) return;
    e87_reset(&b->core);
    if (b->core.model==E87_80287) e8259_set_irq5(&pc->pic2,0);
    else e86_nmi(pc->cpu,0);
}
void pc_npx_init(ibmpc_t *pc,unsigned model)
{
    memset(&pc->npx,0,sizeof pc->npx);
    if (!model) return;
    pc->npx.enabled=1;
    ini_get_uint16(ini_next_sct(pc->cfg,NULL,"npx"),"timing",&pc->npx.timing,model==2);
    if(model!=2)pc->npx.timing=0;
    e87_init(&pc->npx.core,model==1 ? E87_8087 : E87_80287);
    e86_set_npx_control(pc->cpu,pc,control);
    e86_set_npx_memory(pc->cpu,memory);
    e86_set_npx_address(pc->cpu,address);
    e86_set_npx_event(pc->cpu,event);
    pce_log(MSG_INF,"NPX:      timing=%u (partial 287 typical-cycle profile, CPU clock 1:1)\n",(unsigned)pc->npx.timing);
    if (model==1) {
        pc->ppi_port_a[0] |= 2;
        pc->ppi_port_c[0] |= 2;
    }
    pce_log(MSG_INF,"NPX:      model=%s (experimental functional emulation)\n",model==1 ? "8087" : "80287");
}
void pc_npx_write(ibmpc_t *pc,unsigned port,unsigned val)
{
    pc_npx_t *b=&pc->npx;
    if (!b->enabled) return;
    if (b->core.model==E87_8087 && port==0xa0) {
        b->nmi_enabled=(val&0x80)!=0;
        update(pc);
    } else if (b->core.model==E87_80287) {
        if (port==0xf0) {
            b->busy_latch=0;
            e8259_set_irq5(&pc->pic2,0);
        } else if (port==0xf1) {
            b->busy_cycles=0;
            e87_reset(&b->core);
            update(pc);
        }
    }
}
