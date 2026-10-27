/* Specification-derived regression baseline. See cpu-compliance.md.
 * Arithmetic oracle uses unsigned/signed range and nibble calculations,
 * not emulator flag helpers. Undefined flags are never asserted. */
#include <stdio.h>
#include <string.h>
#include "cpu/e8086/e8086.h"
static unsigned char ram[65536];
static unsigned char r8(void*x,unsigned long a){(void)x;return ram[a&65535];}
static unsigned short r16(void*x,unsigned long a){return r8(x,a)|(r8(x,a+1)<<8);}
static void w8(void*x,unsigned long a,unsigned char v){(void)x;ram[a&65535]=v;}
static void w16(void*x,unsigned long a,unsigned short v){w8(x,a,v);w8(x,a+1,v>>8);}
static const char* names[]={"8086","8088","80186","80188","80286","V20","V30"};
static e8086_t* setup(unsigned model){
 e8086_t*c=e86_new();memset(ram,0,sizeof ram);
 switch(model){case 0:e86_set_8086(c);break;case 1:e86_set_8088(c);break;
 case 2:e86_set_80186(c);break;case 3:e86_set_80188(c);break;
 case 4:e86_set_80286(c);break;case 5:e86_set_v20(c);break;default:e86_set_v30(c);}
 e86_reset(c);e86_set_mem(c,0,r8,w8,r16,w16);
 e86_set_cs(c,0);e86_set_ip(c,0x100);e86_set_ss(c,0);e86_set_sp(c,0x8000);return c;
}
int main(void)
{
 unsigned model,kind,word,scan,df,initial,count_index,checks=0,failures=0;
 const unsigned counts[]={0,1,3};
 for(model=0;model<7;model++)for(kind=0;kind<(model>=5?4:2);kind++)
 for(word=0;word<2;word++)for(scan=0;scan<2;scan++)for(df=0;df<2;df++)
 for(initial=0;initial<2;initial++)for(count_index=0;count_index<3;count_index++) {
  e8086_t*c=setup(model);unsigned count=counts[count_index],i;
  unsigned carry=kind>=2,want=kind&1;
  unsigned first=carry?(want?6:4):(want?5:6),second=carry?(want?4:6):(want?6:5);
  int delta=df?-(int)(1+word):(int)(1+word);
  unsigned steps=count==0?0:count==1?1:2;
  ram[0x100]=carry?(want?0x65:0x64):(want?0xf3:0xf2);
  ram[0x101]=(scan?0xae:0xa6)+word;
  e86_set_si(c,0x300);e86_set_di(c,0x400);e86_set_ax(c,5);e86_set_cx(c,count);
  c->flg=((c->cpu&E86_CPU_NEC)?0x8000:0)|2|(df?0x400:0)|(initial?0x41:0);
  for(i=0;i<3;i++) {
   unsigned src=(0x300+(int)i*delta)&65535,dst=(0x400+(int)i*delta)&65535;
   if(word){w16(0,src,5);w16(0,dst,i?second:first);}
   else {w8(0,src,5);w8(0,dst,i?second:first);}
  }
  for(i=0;i<5&&e86_get_ip(c)!=0x102;i++)e86_execute(c);
  checks++;
  if(c->int_cnt||e86_get_ip(c)!=0x102||e86_get_cx(c)!=count-steps||
   e86_get_di(c)!=((0x400+(int)steps*delta)&65535)||
   e86_get_si(c)!=(scan?0x300:((0x300+(int)steps*delta)&65535))||
   (!count&&c->flg!=(((c->cpu&E86_CPU_NEC)?0x8000:0)|2|(df?0x400:0)|(initial?0x41:0)))) {
   failures++;printf("FAIL %s kind=%u word=%u scan=%u df=%u initial=%u count=%u IP=%04x CX=%u\n",names[model],kind,word,scan,df,initial,count,e86_get_ip(c),e86_get_cx(c));
  }
  e86_del(c);
 }
 printf("%u repeat checks across seven models, %u failures\n",checks,failures);
 return failures?1:0;
}
