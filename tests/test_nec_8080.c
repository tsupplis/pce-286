/* NEC manual sections 8, 12-169 and 12-176/177: segmented 8080 round trips. */
#include <stdio.h>
#include <string.h>
#include <cpu/e8086/e8086.h>
static unsigned char ram[1<<20];
static unsigned checks,failures;
static unsigned char rd(void*p,unsigned long a){(void)p;return ram[a&0xfffff];}
static void wr(void*p,unsigned long a,unsigned char v){(void)p;ram[a&0xfffff]=v;}
static unsigned short rw(void*p,unsigned long a){return rd(p,a)|(rd(p,a+1)<<8);}
static void ww(void*p,unsigned long a,unsigned short v){wr(p,a,v);wr(p,a+1,v>>8);}
static void check(const char*s,int ok){checks++;if(!ok){failures++;printf("FAIL %s\n",s);}}
static void vector(unsigned v,unsigned cs,unsigned ip){ww(0,v*4,ip);ww(0,v*4+2,cs);}
static e8086_t* setup(unsigned model){
 e8086_t*c=e86_new();memset(ram,0,sizeof ram);
 if(model)e86_set_v30(c);else e86_set_v20(c);e86_reset(c);
 e86_set_mem(c,0,rd,wr,rw,ww);e86_set_cs(c,0);c->ip=0x100;
 e86_set_ds(c,0x2000);e86_set_ss(c,0x3000);e86_set_sp(c,0x8000);
 e86_set_bp(c,0x9000);e86_set_ax(c,0xab11);e86_set_bx(c,0x1234);
 e86_set_cx(c,0x5678);e86_set_dx(c,0x9abc);e86_set_si(c,0x1111);e86_set_di(c,0x2222);
 ram[0x100]=0x0f;ram[0x101]=0xff;ram[0x102]=0x80;
 vector(0x80,0x1000,0x100);vector(0x81,0,0x500);return c;
}
static void step(e8086_t*c,unsigned n){while(n--)e86_execute(c);}
static void roundtrip(unsigned m){
 e8086_t*c=setup(m);
 const unsigned char program[]={0x3e,0x42,0x32,0,2,0xc5,0xd1,0xed,0xed,0x81,0xed,0xfd};
 memcpy(ram+0x10100,program,sizeof program);ram[0x500]=0xfe;ram[0x501]=0xc0;ram[0x502]=0xcf;
 step(c,1);check("BRKEM vector/MD/native frame",c->ip==0x100&&e86_get_cs(c)==0x1000&&!(c->flg&0x8000)&&e86_get_sp(c)==0x7ffa&&rw(0,0x37ffa)==0x103);
 step(c,2);check("split CS fetch / DS store",e86_get_al(c)==0x42&&ram[0x20200]==0x42&&ram[0x10200]==0);
 step(c,2);check("8080 stack uses DS:BP / mapped BC DE",e86_get_dx(c)==0x5678&&e86_get_bp(c)==0x9000&&rw(0,0x28ffe)==0x5678&&e86_get_sp(c)==0x7ffa);
 step(c,1);check("CALLN native frame",c->ip==0x500&&(c->flg&0x8000)&&e86_get_sp(c)==0x7ff4&&rw(0,0x37ff4)==0x10a&&!(rw(0,0x37ff8)&0x8000));
 step(c,2);check("IRET returns emulation with native register change",c->ip==0x10a&&!(c->flg&0x8000)&&e86_get_al(c)==0x43);
 step(c,1);check("RETEM restores native frame and locks MD",c->ip==0x103&&e86_get_cs(c)==0&&(c->flg&0x8000)&&!c->nec_md_write&&e86_get_sp(c)==0x8000);
 check("unmapped registers preserved",e86_get_ah(c)==0xab&&e86_get_si(c)==0x1111&&e86_get_di(c)==0x2222&&e86_get_bx(c)==0x1234);
 e86_del(c);
}
static void events(unsigned m){
 e8086_t*c=setup(m);vector(0x20,0,0x500);ram[0x500]=0xcf;
 step(c,1);c->flg|=0x200;
 check("external interrupt accepted",!e86_interrupt(c,0x20));
 check("interrupt enters native with emulation frame",c->ip==0x500&&(c->flg&0x8000)&&!(rw(0,0x37ff8)&0x8000)&&!(c->flg&0x200));
 step(c,1);check("interrupt IRET restores emulation/IF",c->ip==0x100&&!(c->flg&0x8000)&&(c->flg&0x200));
 ram[0x10100]=0x76;c->flg&=~0x200;step(c,1);check("8080 halt retains next PC",c->ip==0x101&&(c->state&E86_STATE_HALT));
 c->irq=1;step(c,1);check("disabled INT wakes emulation without vector",!(c->state&E86_STATE_HALT)&&c->ip==0x101&&!(c->flg&0x8000));
 e86_reset(c);check("reset aborts emulation",(c->flg&0x8000)&&!c->nec_md_write&&e86_get_cs(c)==0xffff&&c->ip==0);e86_del(c);
 c=setup(m);ram[0x100]=0x9d;ww(0,0x38000,2);step(c,1);check("POPF cannot enter emulation before BRKEM",c->flg&0x8000);e86_del(c);
}
static unsigned char inta(void*p){(void)p;return 0x20;}
static unsigned char inp(void*p,unsigned long a){(void)p;return a==0x42?0x5a:0;}
static unsigned out;
static void outp(void*p,unsigned long a,unsigned char v){(void)p;if(a==0x43)out=v;}
static void workload(unsigned m){
 e8086_t*c=setup(m);unsigned i; e86_disasm_t d;
 /* 256 iterations: increment A, store successive bytes through HL, loop on B. */
 const unsigned char code[]={0x21,0,4,0x06,0,0x3e,0,0x3c,0x77,0x23,0x05,0xc2,7,1,0xed,0xfd};
 memcpy(ram+0x10100,code,sizeof code);
 e86_disasm_cur(c,&d);check("native BRKEM disassembly",!strcmp(d.op,"BRKEM")&&d.dat_n==3);
 step(c,1);e86_disasm_cur(c,&d);check("emulation instruction disassembly",!strcmp(d.op,"LXI")&&d.dat_n==3);
 for(i=0;i<2000 && !(c->flg&0x8000);i++)step(c,1);
 check("bounded 8080 guest returns",c->ip==0x103&&(c->flg&0x8000));
 for(i=0;i<256;i++)check("8080 guest buffer byte",ram[0x20400+i]==((i+1)&255));
 e86_del(c);
 c=setup(m);step(c,1);e86_set_prt(c,0,inp,outp,0,0);
 ram[0x10100]=0xdb;ram[0x10101]=0x42;ram[0x10102]=0xd3;ram[0x10103]=0x43;
 out=0;step(c,2);check("8080 IN/OUT uses machine ports",out==0x5a);e86_del(c);
 c=setup(m);step(c,1);vector(0x20,0,0x500);ram[0x500]=0xcf;
 c->inta=inta;c->irq=1;ram[0x10100]=0xfb;ram[0x10101]=0;
 step(c,1);check("NEC EI delays native IRQ",!(c->flg&0x8000));
 step(c,1);check("NEC pending IRQ enters native after next opcode",c->ip==0x500&&(c->flg&0x8000));
 step(c,1);check("IRQ resume address",c->ip==0x102&&!(c->flg&0x8000));
 ram[0x10102]=0x76;c->irq=0;step(c,1);vector(2,0,0x500);e86_trap(c,2);
 check("NMI-style vector wakes halt in native mode",!(c->state&E86_STATE_HALT)&&(c->flg&0x8000));
 step(c,1);check("NMI return resumes after HLT",c->ip==0x103&&!(c->flg&0x8000));e86_del(c);
 c=setup(m);step(c,1);vector(1,0,0x500);ram[0x500]=0xcf;c->flg|=0x100;
 step(c,1);check("8080 single-step enters native trap",c->ip==0x500&&(c->flg&0x8000)&&!(c->flg&0x100));
 step(c,1);check("single-step IRET restores mode",c->ip==0x101&&!(c->flg&0x8000));e86_del(c);
 c=setup(m);step(c,1);c->ip=0xffff;ram[0x1ffff]=0x3e;ram[0x10000]=0x67;
 step(c,1);check("code immediate wraps within CS",c->ip==1&&e86_get_al(c)==0x67);e86_del(c);
}
int main(void){unsigned m;for(m=0;m<2;m++){roundtrip(m);events(m);workload(m);}printf("NEC 8080: %u checks, %u failures\n",checks,failures);return failures?1:0;}
