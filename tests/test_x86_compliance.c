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
static const char* names[]={"8086","8088","80186","80188"};
static e8086_t* setup(unsigned model){
 e8086_t*c=e86_new();memset(ram,0,sizeof ram);
 switch(model){case 0:e86_set_8086(c);break;case 1:e86_set_8088(c);break;
 case 2:e86_set_80186(c);break;default:e86_set_80188(c);}
 e86_reset(c);e86_set_mem(c,0,r8,w8,r16,w16);
 e86_set_cs(c,0);e86_set_ip(c,0x100);e86_set_ss(c,0);e86_set_sp(c,0x8000);return c;
}
static unsigned checks,failures;
static unsigned parity(unsigned n){unsigned ones=0,i;for(i=0;i<8;i++)ones+=(n>>i)&1;return !(ones&1);}
int main(void){
 static const unsigned vals[]={0,1,7,8,15,16,31,63,64,127,128,129,240,254,255};
 static const unsigned ops[]={0x04,0x14,0x2c,0x1c,0x3c};
 unsigned model,op,a,b,carry;
 for(model=0;model<4;model++){
 unsigned before=failures,oldchecks=checks;
 for(op=0;op<5;op++)for(a=0;a<15;a++)for(b=0;b<15;b++)for(carry=0;carry<2;carry++){
  unsigned x=vals[a],y=vals[b],ci=(op==1||op==3)?carry:0,sub=op>=2;
  int sum=sub?(int)x-(int)y-(int)ci:(int)x+(int)y+(int)ci;
  int sx=x<128?(int)x:(int)x-256,sy=y<128?(int)y:(int)y-256;
  int signed_sum=sub?sx-sy-(int)ci:sx+sy+(int)ci;
  unsigned result=sum&255,flags=0,expected=op==4?x:result;
  e8086_t*c=setup(model);
  if(sub?(x<y+ci):(sum>255))flags|=1;
  if(parity(result))flags|=4;
  if(sub?((x&15)<(y&15)+ci):((x&15)+(y&15)+ci>15))flags|=16;
  if(!result)flags|=64;if(result&128)flags|=128;
  if(signed_sum < -128 || signed_sum >127)flags|=0x800;
  ram[0x100]=ops[op];ram[0x101]=y;e86_set_al(c,x);c->flg=0x402|carry;e86_execute(c);checks++;
  if(e86_get_al(c)!=expected||(c->flg&0x8d5)!=flags||!(c->flg&0x400)||e86_get_ip(c)!=0x102){
   if(failures<20)printf("FAIL %s op=%02x x=%02x y=%02x carry=%u flags=%04x want=%04x\n",names[model],ops[op],x,y,carry,c->flg&0x8d5,flags);failures++;
  }e86_del(c);
 }
 /* Baseline 8086 unmasked shifts, 186 five-bit count selection. */
 for(a=0;a<256;a++){
  unsigned count=model>=2?(a&31):a,result=1,i;
  e8086_t*c=setup(model);ram[0x100]=0xd3;ram[0x101]=0xe0;
  for(i=0;i<count;i++)result=(result<<1)&65535;
  e86_set_ax(c,1);e86_set_cl(c,a);c->flg=0x402;e86_execute(c);checks++;
  if(e86_get_ax(c)!=result){printf("FAIL %s shift count=%u\n",names[model],a);failures++;}e86_del(c);
 }
 printf("%s: %u checks, %u failures\n",names[model],checks-oldchecks,failures-before);
 }
 printf("TOTAL %u checks, %u failures\n",checks,failures);return failures?1:0;
}
