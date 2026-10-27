/* 8080 regression, including focused shared Z80 checks.
 * Reference: Intel 8080/8085 Assembly Language Programming (1977),
 * auxiliary carry section, instruction descriptions and interrupt chapter.
 * Original failures and repair evidence: tests/8080-review.md. */
#include <stdio.h>
#include <string.h>
#include <cpu/e8080/e8080.h>
static e8080_t c;
static unsigned char mem[65536];
static unsigned checks, failures;
static void check(const char *name, int ok) {
 checks++; if (!ok) { failures++; printf("FAIL %s\n",name); }
}
static void setup(void) {
 memset(&c,0,sizeof c); memset(mem,0,sizeof mem);
 e8080_init(&c); e8080_reset(&c);
 e8080_set_mem_map_rd(&c,0,65535,mem);
 e8080_set_mem_map_wr(&c,0,65535,mem);
}
static unsigned szp(unsigned v) {
 unsigned n=0,i; v&=255;
 for(i=0;i<8;i++) n+=(v>>i)&1;
 return (v&128)|(v==0?64:0)|((n%2)==0?4:0);
}
static void arithmetic(void) {
 unsigned op,a,b,carry,form,bad[8]={0};
 static const char *names[]={"ADD exhaustive byte result/flags","ADC exhaustive byte result/flags",
 "SUB exhaustive byte result/flags","SBB exhaustive byte result/flags",
 "ANA exhaustive byte result/flags","XRA exhaustive byte result/flags",
 "ORA exhaustive byte result/flags","CMP exhaustive byte result/flags"};
 setup();
 for(form=0;form<3;form++) for(op=0;op<8;op++) for(a=0;a<256;a++) for(b=0;b<256;b++) for(carry=0;carry<2;carry++) {
  unsigned v=0,f=2,cin=0;
  c.pc=0;c.delay=0;c.psw=2|carry; e8080_set_a(&c,a);e8080_set_b(&c,b);
  mem[0]=(form==2?0xc6:0x80)+8*op+(form==1?6:0);
  e8080_set_hl(&c,0x2000);mem[0x2000]=b;mem[1]=b;
  if(op<2) {cin=op?carry:0;v=a+b+cin;f|=(v>255)|(((a&15)+(b&15)+cin>15)?16:0);}
  else if(op==2||op==3||op==7) {cin=op==3?carry:0;v=(a-b-cin)&255;
   f|=(a<b+cin)|(( (a&15)>=(b&15)+cin)?16:0);}
  else if(op==4) {v=a&b;f|=((a|b)&8)?16:0;}
  else if(op==5) v=a^b; else v=a|b;
  f|=szp(v); e8080_execute(&c);
  checks++;
  if(e8080_get_a(&c)!=(op==7?a:(v&255)) || c.psw!=f || c.pc!=(form==2?2:1)) bad[op]++;
 }
 for(op=0;op<8;op++) {printf("%s %s: %u mismatches / 393216\n",bad[op]?"FAIL":"PASS",names[op],bad[op]);failures+=bad[op];}
}
static unsigned char port(void *ext,unsigned long addr) {return ext==mem && addr==0x42 ? 0xa5:0;}
static unsigned char ack_bytes[3];
static unsigned ack_count;
static unsigned char ack(void *ext,unsigned index) {
 check("ack callback context/index",ext==ack_bytes&&index<3);
 ack_count++;return ack_bytes[index];
}
static void interrupt_tests(void) {
 unsigned v,model;
 for(v=0;v<8;v++) {
  setup();c.pc=0x100;c.sp=0x2000;c.iff=1;ack_count=0;
  ack_bytes[0]=0xc7+8*v;e8080_set_inta_fct(&c,ack_bytes,ack);
  e8080_set_int(&c,1);e8080_execute(&c);
  check("supplied RST vector and exact return address",c.pc==8*v&&c.sp==0x1ffe&&e8080_get_mem16(&c,c.sp)==0x101&&ack_count==1&&!c.iff);
 }
 setup();c.pc=0x100;c.sp=0x2000;c.iff=1;ack_count=0;
 ack_bytes[0]=0xcd;ack_bytes[1]=0x34;ack_bytes[2]=0x12;
 e8080_set_inta_fct(&c,ack_bytes,ack);e8080_set_int(&c,1);e8080_execute(&c);
 check("supplied CALL operands and return address",c.pc==0x1234&&e8080_get_mem16(&c,c.sp)==0x101&&ack_count==3);
 setup();c.pc=0x100;c.iff=1;ack_bytes[0]=0x3e;ack_bytes[1]=0x7a;
 e8080_set_inta_fct(&c,ack_bytes,ack);e8080_set_int(&c,1);e8080_execute(&c);
 check("supplied MVI does not consume program bytes",c.pc==0x101&&e8080_get_a(&c)==0x7a);
 for(model=0;model<2;model++) {
  setup();if(model) {e8080_set_z80(&c);c.im=1;}
  c.sp=0x2000;mem[0]=0xfb;mem[1]=0xfb;mem[2]=0;
  e8080_set_int(&c,1);e8080_execute(&c);e8080_execute(&c);
  check("EI EI postpones acknowledge",c.int_cnt==0);
  e8080_execute(&c);check("EI EI NOP acknowledges",c.int_cnt==1&&c.pc==0x38&&e8080_get_mem16(&c,c.sp)==3);
  setup();if(model) {e8080_set_z80(&c);c.im=1;}
  mem[0]=0xfb;mem[1]=0xf3;mem[2]=0;e8080_set_int(&c,1);
  e8080_execute(&c);e8080_execute(&c);e8080_execute(&c);
  check("EI DI cancels enable",!c.iff&&c.int_cnt==0);
  setup();if(model) {e8080_set_z80(&c);c.im=1;}
  c.sp=0x2000;mem[0]=0xfb;mem[1]=0x76;
  e8080_execute(&c);e8080_execute(&c);
  check("EI HLT halts before external request",c.halt&&c.int_cnt==0);
  e8080_set_int(&c,1);e8080_execute(&c);
  check("HLT wake returns after halt",!c.halt&&c.pc==0x38&&e8080_get_mem16(&c,c.sp)==2);
  setup();if(model) {e8080_set_z80(&c);c.im=1;}
  c.iff=1;e8080_set_int(&c,1);e8080_set_int(&c,0);e8080_execute(&c);
  check("withdrawn INT is not latched",c.int_cnt==0);
 }
 setup();e8080_set_z80(&c);c.im=2;c.i=0x20;c.sp=0x3000;c.iff=1;
 mem[0x20ff]=0x78;mem[0x2100]=0x56;e8080_set_int(&c,1);e8080_execute(&c);
 check("Z80 IM2 retains vector dispatch",c.pc==0x5678&&e8080_get_mem16(&c,c.sp)==1&&!c.iff);
 setup();e8080_set_z80(&c);e8080_set_a(&c,0);e8080_set_b(&c,0);mem[0]=0xa0;e8080_execute(&c);
 check("Z80 AND still sets H",e8080_get_af(&c));
 c.pc=0;mem[0]=0x90;e8080_execute(&c);
 check("Z80 SUB equal still clears H",!e8080_get_af(&c));
 c.pc=0;mem[0]=0x05;e8080_execute(&c);
 check("Z80 DEC zero still sets H",e8080_get_af(&c));
}
int main(void) {
 unsigned a,b,bad=0;
 arithmetic();
 interrupt_tests();
 setup();
 for(a=0;a<100;a++) for(b=0;b<100;b++) {
  c.pc=0;c.delay=0;c.psw=2;
  e8080_set_a(&c,(a/10)*16+a%10);e8080_set_b(&c,(b/10)*16+b%10);
  mem[0]=0x80;mem[1]=0x27;e8080_execute(&c);e8080_execute(&c);
  checks++;if(e8080_get_a(&c)!=(((a+b)%100)/10*16+(a+b)%10)||e8080_get_cf(&c)!=((a+b)>=100))bad++;
 }
 printf("%s DAA valid decimal additions: %u mismatches / 10000\n",bad?"FAIL":"PASS",bad); failures+=bad;
 setup();bad=0;
 for(a=0;a<256;a++) for(b=0;b<2;b++) {
  c.pc=0;c.delay=0;c.psw=2|b;e8080_set_b(&c,a);mem[0]=0x05;
  e8080_execute(&c);checks++;
  if(e8080_get_b(&c)!=((a-1)&255)||c.psw!=(2|b|szp(a-1)|((a&15)?16:0)))bad++;
 }
 printf("%s DCR result/flags: %u mismatches / 512\n",bad?"FAIL":"PASS",bad);failures+=bad;
 setup();c.mem_rd_ext=&c;e8080_set_port_read_fct(&c,mem,port);
 check("input setter preserves memory context",c.mem_rd_ext==&c);
 check("separate input callback context", e8080_get_port8(&c,0x42)==0xa5);
 setup();c.pc=0xffff;mem[0xffff]=0x01;mem[0]=0x34;mem[1]=0x12;e8080_execute(&c);
 check("immediate fetch wraps at FFFF",c.pc==2&&e8080_get_bc(&c)==0x1234);
 e8080_set_mem16(&c,0xffff,0xabcd);
 check("word memory wraps at FFFF",mem[0xffff]==0xcd&&mem[0]==0xab);
 setup();c.sp=1;mem[0]=0xcd;mem[1]=0x00;mem[2]=0x20;mem[0x2000]=0xc9;
 e8080_execute(&c);check("CALL stack wraps",c.pc==0x2000&&c.sp==0xffff&&mem[0xffff]==3&&mem[0]==0);
 e8080_execute(&c);check("RET restores wrapped stack",c.pc==3&&c.sp==1);
 setup();mem[0]=0xfb;mem[1]=0; e8080_set_int(&c,1);e8080_execute(&c);
 check("EI defers interrupt for following instruction",c.int_cnt==0);
 e8080_execute(&c);check("asserted interrupt survives EI",c.int_cnt==1);
 setup();c.iff=1;c.sp=0x1000;e8080_set_int(&c,1);e8080_execute(&c);
 check("8080 interrupt acknowledge disables interrupts and transfers",c.iff==0&&c.sp==0xffe);
 printf("8080 audit: %u checks, %u failed checks\n",checks,failures);
 return failures?1:0;
}
