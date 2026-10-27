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
 unsigned model,op,mem,bit,value_index;
 const unsigned values[]={0,0xffff,0xa55a};
 for(model=0;model<2;model++)for(op=0x10;op<=0x1f;op++)
 for(mem=0;mem<3;mem++)for(bit=0;bit<32;bit++)for(value_index=0;value_index<3;value_index++) {
  unsigned word=op&1,kind=(op>>1)&3,imm=op&8,n=3;
  unsigned char code[8]={0x0f,op,0xc3};
  unsigned value=values[value_index]&(word?65535:255),mask=1U<<(bit&(word?15:7)),expected=value,actual;
  e8086_t*c;
  if(mem==1){code[2]=0x06;code[3]=0xff;code[4]=0x02;n=5;}
  if(mem==2){code[0]=0x36;code[1]=0x0f;code[2]=op;code[3]=0x46;code[4]=0xff;n=5;}
  if(imm)code[n++]=bit;
  c=setup(model,code,n);e86_set_cl(c,bit);e86_set_bx(c,value);e86_set_bp(c,0x300);
  if(mem){if(word)w16(0,0x2ff,value);else w8(0,0x2ff,value);}
  c->flg=0x88d7;
  if(kind==1)expected&=~mask;else if(kind==2)expected|=mask;else if(kind==3)expected^=mask;
  e86_execute(c);
  actual=mem?(word?r16(0,0x2ff):r8(0,0x2ff)):(word?e86_get_bx(c):e86_get_bl(c));
  checks++;
  if(actual!=expected||e86_get_ip(c)!=0x100+n||c->int_cnt||
    (kind?c->flg!=0x88d7:(e86_get_zf(c)!=!(value&mask)||e86_get_cf(c)||e86_get_of(c)))) {
   failures++;printf("FAIL V%u op=%02x mem=%u bit=%u value=%04x got=%04x IP=%04x flags=%04x\n",model?30:20,op,mem,bit,value,actual,e86_get_ip(c),c->flg);
  }
  e86_del(c);
 }
 for(model=0;model<2;model++)for(op=0;op<2;op++)for(mem=0;mem<2;mem++){
  unsigned char code[]={0x0f,op?0x2a:0x28,mem?0x06:0xc3,0,2};
  e8086_t*c=setup(model,code,mem?5:3);
  e86_set_bl(c,0x95);ram[0x200]=0x95;e86_set_al(c,3);c->flg=0x88d7;e86_execute(c);
  check(model,"nibble rotate data/flags/length",(mem?ram[0x200]:e86_get_bl(c))==(op?0x39:0x53)&&
   (e86_get_al(c)&15)==(op?5:9)&&c->flg==0x88d7&&e86_get_ip(c)==0x100+(mem?5:3));e86_del(c);
 }
 printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
