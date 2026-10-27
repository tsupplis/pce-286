/* Review probe: NEC uPD70108/70116 manual pp. 12-14, 12-45/46,
 * 12-81..96, 12-170. Intentionally fails until the native decoder is fixed.
 * Run separately; not part of check-unit while these findings are open. */
#include <stdio.h>
#include <string.h>
#include "cpu/e8086/e8086.h"
static unsigned char ram[65536];
static unsigned char r8(void *x,unsigned long a){(void)x;return ram[a&65535];}
static unsigned short r16(void*x,unsigned long a){return r8(x,a)|(r8(x,a+1)<<8);}
static void w8(void*x,unsigned long a,unsigned char v){(void)x;ram[a&65535]=v;}
static void w16(void*x,unsigned long a,unsigned short v){w8(x,a,v);w8(x,a+1,v>>8);}
static e8086_t *setup(unsigned model,const unsigned char *code,unsigned n){
 e8086_t*c=e86_new();memset(ram,0,sizeof ram);
 if(model)e86_set_v30(c);else e86_set_v20(c);e86_reset(c);
 e86_set_mem(c,0,r8,w8,r16,w16);e86_set_cs(c,0);e86_set_ip(c,0x100);
 e86_set_ds(c,0);e86_set_es(c,0);e86_set_ss(c,0);e86_set_sp(c,0x8000);
 memcpy(ram+0x100,code,n);return c;
}
static unsigned checks,failures;
static void check(unsigned model,const char*label,int ok){checks++;failures+=!ok;printf("%s V%u %s\n",ok?"PASS":"FAIL",model?30:20,label);}
int main(void){unsigned model,i;
 for(model=0;model<2;model++){
  {unsigned char code[]={0xd3,0xe0};e8086_t*c=setup(model,code,2);
   e86_set_ax(c,1);e86_set_cl(c,32);e86_execute(c);
   check(model,"SHL AX,CL count 32 is not masked to zero",e86_get_ax(c)==0);e86_del(c);}
  {unsigned char code[]={0x90};e8086_t*c=setup(model,code,1);e86_reset(c);
   check(model,"NEC reset CS:IP = FFFF:0000",e86_get_cs(c)==0xffff&&e86_get_ip(c)==0);e86_del(c);}
  {unsigned char code[]={0xb8,0x34,0x12,0x05,1,0,0x50,0x5b};e8086_t*c=setup(model,code,8);
   for(i=0;i<4;i++)e86_execute(c);
   check(model,"baseline MOV ADD PUSH POP",e86_get_ax(c)==0x1235&&e86_get_bx(c)==0x1235&&e86_get_sp(c)==0x8000);e86_del(c);}
  for(i=0;i<16;i++){
   unsigned char code[]={0x0f,0x10+i,0xc3,3};
   unsigned kind=(i&7)/2,word=i&1,imm=i>=8;
   unsigned old=word?0xffff:0xff,expected=old;
   e8086_t*c=setup(model,code,sizeof code);char label[80];
   e86_set_bx(c,old);e86_set_cl(c,3);c->flg=0x88d7;
   if(kind==1)expected=old&~8U;else if(kind==2)expected=old|8;else if(kind==3)expected=old^8;
   e86_execute(c);
   snprintf(label,sizeof label,"bit-op 0F %02X encoding/result/flags",0x10+i);
   check(model,label,e86_get_ip(c)==0x103+imm && e86_get_bx(c)==expected &&
     (kind?c->flg==0x88d7:(!e86_get_zf(c)&&!e86_get_cf(c))));e86_del(c);
  }
  for(i=0;i<2;i++){
   unsigned char code[]={0x0f,i?0x2a:0x28,0xc3};e8086_t*c=setup(model,code,3);
   e86_set_bl(c,0x95);e86_set_al(c,3);c->flg=0x88d7;e86_execute(c);
   check(model,i?"ROR4 manual example":"ROL4 manual example",e86_get_bl(c)==(i?0x39:0x53)&&(e86_get_al(c)&15)==(i?5:9));
   check(model,i?"ROR4 preserves flags":"ROL4 preserves flags",c->flg==0x88d7);e86_del(c);
  }
  for(i=0;i<2;i++){
   unsigned char code[]={i?0x65:0x64,0xa6};e8086_t*c=setup(model,code,2);
   e86_set_si(c,0x200);e86_set_di(c,0x300);e86_set_cx(c,2);
   ram[0x200]=i?2:1;ram[0x300]=i?1:2;
   e86_execute(c);
   check(model,i?"REPC stops on clear carry":"REPNC stops on set carry",c->int_cnt==0&&e86_get_cx(c)==1&&e86_get_ip(c)==0x102);e86_del(c);
  }
  {unsigned char code[]={0x62,0x06,0x00,0x02};e8086_t*c=setup(model,code,4);
   e86_set_ax(c,5);w16(0,0x200,1);w16(0,0x202,9);e86_execute(c);
   check(model,"CHKIND native opcode 62 in range",c->int_cnt==0&&e86_get_ip(c)==0x104);e86_del(c);}
  {unsigned char code[]={0x0f,0x10,0xc3};e86_disasm_t d;e86_disasm_model(&d,code,0x100,E86_CPU_NEC);
   check(model,"disassembler recognizes TEST1",!strcmp(d.op,"TEST1")&&d.dat_n==3);}
 }
 printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
