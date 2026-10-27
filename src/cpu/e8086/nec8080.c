/* NEC V20/V30 8080 adapter. NEC User's Manual sections 8 and 12.
 * No standalone 8080 interrupt is delivered: INT enters a native handler.
 * Instruction timings currently use the 8080 engine's functional estimates. */
#include "e8086.h"
#include "internal.h"
static unsigned char data_read(void *p,unsigned long a) {
 e8086_t*c=p; return e86_get_mem8(c,e86_get_ds(c),a&65535);
}
static void data_write(void*p,unsigned long a,unsigned char v) {
 e8086_t*c=p; e86_set_mem8(c,e86_get_ds(c),a&65535,v);
}
static unsigned char code_read(void*p,unsigned long a) {
 e8086_t*c=p;return e86_get_mem8(c,e86_get_cs(c),a&65535);
}
static unsigned char port_read(void*p,unsigned long a) {
 e8086_t*c=p;return e86_get_prt8(c,a&255);
}
static void port_write(void*p,unsigned long a,unsigned char v) {
 e8086_t*c=p;e86_set_prt8(c,a&255,v);
}
void e86_nec_init(e8086_t*c) {
 e8080_init(&c->nec8080);e8080_reset(&c->nec8080);c->nec_md_write=0;
 e8080_set_mem_fct(&c->nec8080,c,data_read,data_write);
 e8080_set_port_fct(&c->nec8080,c,port_read,port_write);
 c->nec8080.fetch_ext=c;c->nec8080.get_fetch8=code_read;
}
void e86_nec_brkem(e8086_t*c,unsigned vector) {
 /* Nested BRKEM is explicitly unsupported/undefined by NEC. */
 if(c->nec_md_write) {e86_undefined(c);return;}
 e86_push(c,c->flg);e86_push(c,e86_get_cs(c));e86_push(c,(c->ip+3)&65535);
 c->flg &= ~0x8000U;c->nec_md_write=1;
 c->ip=e86_get_mem16(c,0,vector*4);e86_set_cs(c,e86_get_mem16(c,0,vector*4+2));
 c->nec8080.ei_delay=0;e86_pq_init(c);e86_set_clk(c,50);
}
void e86_nec_step(e8086_t*c) {
 e8080_t*x=&c->nec8080;unsigned op,sub;
 if(c->state & E86_STATE_HALT) {
  e86_set_clk(c,2);
  if(c->irq) {c->state &= ~E86_STATE_HALT;if(e86_get_if(c))e86_irq_ack(c);}
  return;
 }
 c->cur_ip=c->ip;c->prefix=0;c->save_flags=c->flg;
 op=code_read(c,c->ip);
 if(op==0xed) {
  sub=code_read(c,(c->ip+1)&65535);
  if(sub==0xed) { /* CALLN imm8 */
   unsigned v=code_read(c,(c->ip+2)&65535);
   e86_push(c,c->flg);e86_push(c,e86_get_cs(c));e86_push(c,(c->ip+3)&65535);
   c->flg |= 0x8000;c->ip=e86_get_mem16(c,0,v*4);
   e86_set_cs(c,e86_get_mem16(c,0,v*4+2));e86_pq_init(c);
   e86_set_clk(c,58);c->opcnt++;return;
  }
  if(sub==0xfd) { /* RETEM: native SS:SP, not emulated DS:BP. */
   unsigned cs,flags;c->ip=e86_pop(c);cs=e86_pop(c);flags=e86_pop(c);
   e86_set_cs(c,cs);c->flg=(flags&0x8fd5)|0x8002;c->nec_md_write=0;
   x->ei_delay=0;e86_pq_init(c);e86_set_clk(c,39);c->opcnt++;return;
  }
 }
 e8080_set_a(x,e86_get_al(c));e8080_set_bc(x,e86_get_cx(c));
 e8080_set_de(x,e86_get_dx(c));e8080_set_hl(x,e86_get_bx(c));
 x->sp=e86_get_bp(c);x->pc=c->ip;x->psw=(c->flg&0xd5)|2;
 x->iff=e86_get_if(c);x->int_val=x->int_req=0;x->halt=0;x->delay=0;
 e8080_execute(x);
 e86_set_al(c,e8080_get_a(x));e86_set_cx(c,e8080_get_bc(x));
 e86_set_dx(c,e8080_get_de(x));e86_set_bx(c,e8080_get_hl(x));e86_set_bp(c,x->sp);
 c->ip=x->pc;c->flg=(c->flg&~0x2d5U)|(x->psw&0xd5)|2|(x->iff?E86_FLG_I:0);
 if(x->halt) {c->ip=(c->ip+1)&65535;c->state|=E86_STATE_HALT;}
 e86_set_clk(c,x->delay);c->opcnt++;e86_pq_init(c);
 if(c->save_flags & E86_FLG_T) {
  c->state &= ~E86_STATE_HALT;e86_trap(c,1);
 } else if(c->irq && e86_get_if(c) && !x->ei_delay) {
  c->state &= ~E86_STATE_HALT;e86_irq_ack(c);
 }
}
