/* NEC-manual-derived tests; no legacy synthetic encodings. */
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
int main(void)
{
 unsigned model;
 for(model=0;model<2;model++){
  unsigned char code[]={0x62,0x06,0,2};e8086_t*c=setup(model,code,4);
  e86_set_ax(c,5);w16(0,0x200,1);w16(0,0x202,9);e86_execute(c);
  check(model,"CHKIND in range",c->int_cnt==0&&e86_get_ip(c)==0x104);
  check(model,"bus width selection",!!(c->cpu&E86_CPU_8BIT)==!model);
  e86_del(c);
  code[0]=0x0f;code[1]=0x27;c=setup(model,code,2);e86_set_bx(c,0xaaaa);e86_execute(c);
  check(model,"unsupported extension does not alias bit operation",c->int_vec==6&&e86_get_bx(c)==0xaaaa);e86_del(c);
 }
 printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
