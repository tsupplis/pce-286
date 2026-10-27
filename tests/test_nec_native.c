/* Native NEC acceptance. Decimal oracle works one decimal digit at a time;
 * bit-field oracle works one bit at a time, independent of core word masks.
 * Undefined flags, invalid BCD digits and CL outside 1..254 are not asserted. */
#include <stdio.h>
#include <string.h>
#include "cpu/e8086/e8086.h"
static unsigned char ram[1048576];
static unsigned char r8(void*x,unsigned long a){(void)x;return ram[a&1048575];}
static unsigned short r16(void*x,unsigned long a){return r8(x,a)|(r8(x,a+1)<<8);}
static void w8(void*x,unsigned long a,unsigned char v){(void)x;ram[a&1048575]=v;}
static void w16(void*x,unsigned long a,unsigned short v){w8(x,a,v);w8(x,a+1,v>>8);}
static unsigned checks,failures;
static void check(const char *s,int ok){checks++;if(!ok){if(failures<20)printf("FAIL %s\n",s);failures++;}}
static e8086_t* setup(unsigned model){
 e8086_t*c=e86_new();memset(ram,0,sizeof ram);
 if(model)e86_set_v30(c);else e86_set_v20(c);e86_reset(c);
 e86_set_mem(c,0,r8,w8,r16,w16);e86_set_cs(c,0);e86_set_ip(c,0x100);
 e86_set_ds(c,0x1000);e86_set_es(c,0x3000);e86_set_ss(c,0x2000);e86_set_sp(c,0x8000);return c;
}
static void decimal(unsigned model){
 unsigned op,nidx,pattern,override,df;
 const unsigned lengths[]={2,4,8,254},ops[]={0x20,0x22,0x26};
 for(op=0;op<3;op++)for(nidx=0;nidx<4;nidx++)for(pattern=0;pattern<5;pattern++)
 for(override=0;override<2;override++)for(df=0;df<2;df++){
  e8086_t*c=setup(model);unsigned n=lengths[nidx],i,borrow=0,zero=1;
  unsigned char src[127]={0},dst[127]={0},expected[127]={0};unsigned base=override?0:0x10000;
  for(i=0;i<n;i++){
   unsigned a=pattern==0?0:pattern==1?9:pattern==2?1:(i%10);
   unsigned b=pattern==0?0:pattern==1?1:pattern==2?0:pattern==3?a:9;
   int sum=op?(int)b-(int)a-(int)borrow:(int)b+(int)a+(int)borrow;
   src[i/2]|=a<<((i&1)*4);dst[i/2]|=b<<((i&1)*4);
   borrow=op?sum<0:sum>=10;if(borrow)sum+=op?10:-10;
   expected[i/2]|=sum<<((i&1)*4);if(sum)zero=0;
  }
  memcpy(ram+base+0x200,src,n/2);memcpy(ram+0x30600,dst,n/2);
  if(override)ram[0x100]=0x2e;
  ram[0x100+override]=0x0f;ram[0x101+override]=ops[op];
  e86_set_si(c,0x200);e86_set_di(c,0x600);e86_set_cx(c,0xa500|n);c->flg=0x8003|(df?0x400:0);
  e86_execute(c);
  check("BCD result / CMP non-write",!memcmp(ram+0x30600,op==2?dst:expected,n/2));
  check("BCD source unchanged",!memcmp(ram+base+0x200,src,n/2));
  check("BCD carry and zero",e86_get_cf(c)==borrow&&e86_get_zf(c)==zero);
  check("BCD pointers/count/length",e86_get_si(c)==0x200&&e86_get_di(c)==0x600&&e86_get_cx(c)==(0xa500|n)&&e86_get_ip(c)==0x102+override);
  check("BCD neighboring bytes",ram[0x305ff]==0&&ram[0x30600+n/2]==0);e86_del(c);
 }
}
static void fields(unsigned model){
 unsigned extract,imm,bit,len,wrap;
 for(extract=0;extract<2;extract++)for(imm=0;imm<2;imm++)for(bit=0;bit<16;bit++)
 for(len=1;len<=16;len++)for(wrap=0;wrap<2;wrap++){
  e8086_t*c=setup(model);unsigned ofs=wrap?0xfffe:0x400,base=extract?0x10000:0x30000;
  unsigned char bytes[]={0x5a,0xa5,0xc3,0x3c},want[4];unsigned i,result=0,next=bit+len;
  memcpy(want,bytes,4);
  for(i=0;i<4;i++)ram[base+((ofs+i)&65535)]=bytes[i];
  for(i=0;i<len;i++){
   unsigned pos=bit+i;
   if(extract)result|=((bytes[pos/8]>>(pos%8))&1)<<i;
   else want[pos/8]=(want[pos/8]&~(1U<<(pos%8)))|(((0x695a>>i)&1)<<(pos%8));
  }
  ram[0x100]=0x0f;ram[0x101]=(extract?0x33:0x31)+(imm?8:0);ram[0x102]=imm?0xc1:0xd1;ram[0x103]=len-1;
  e86_set_ax(c,0x695a);e86_set_si(c,ofs);e86_set_di(c,ofs);e86_set_cl(c,bit);e86_set_dl(c,len-1);e86_execute(c);
  check("field offset and pointer",e86_get_cl(c)==(next&15)&&(extract?e86_get_si(c):e86_get_di(c))==((ofs+(next>=16?2:0))&65535));
  check("field other pointer/count",(extract?e86_get_di(c):e86_get_si(c))==ofs&&e86_get_dl(c)==len-1);
  if(extract)check("EXT zero-extended result",e86_get_ax(c)==result);
  else check("INS preserves AX",e86_get_ax(c)==0x695a);
  for(i=0;i<4;i++)check("field contents and neighboring bits",ram[base+((ofs+i)&65535)]==(extract?bytes[i]:want[i]));
  check("field instruction length",e86_get_ip(c)==(imm?0x104:0x103));e86_del(c);
 }
}
static void field_override(unsigned model){
 e8086_t*c=setup(model);unsigned char code[]={0x26,0x0f,0x3b,0xc1,8};
 memcpy(ram+0x100,code,sizeof code);w16(0,0x30400,0x5555);w16(0,0x10400,0);
 e86_set_si(c,0x400);e86_set_cl(c,3);e86_execute(c);
 check("EXT explicit source segment",e86_get_ax(c)==0xaa&&e86_get_cl(c)==12&&e86_get_ip(c)==0x105);e86_del(c);
}
static void policy(unsigned model){
 unsigned count,imm;
 for(count=0;count<256;count++)for(imm=0;imm<2;imm++){
  e8086_t*c=setup(model);ram[0x100]=imm?0xc1:0xd3;ram[0x101]=0xe0;ram[0x102]=count;
  e86_set_ax(c,1);e86_set_cl(c,count);e86_execute(c);
  check("NEC full-byte immediate/CL shift",e86_get_ax(c)==(count<16?(1U<<count):0));e86_del(c);
 }
 {e8086_t*c=setup(model);e86_reset(c);check("NEC reset native state",e86_get_cs(c)==0xffff&&e86_get_ip(c)==0&&(c->flg&0x8000)&&!(c->flg&0x700));e86_del(c);}
}
static void disassembly(void){
 const unsigned char codes[][8]={{0x0f,0x10,0xc3},{0x0f,0x1b,0x06,0,2,15},{0x0f,0x20},{0x0f,0x22},{0x0f,0x26},{0x0f,0x31,0xd1},{0x0f,0x3b,0xc1,15},{0x0f,0x28,0xc3},{0x0f,0x2a,0xc3},{0x65,0xa6},{0x64,0xaf},{0x2e,0x0f,0x10,0x06,0,2},{0x62,0x06,0,2}};
 const char*names[]={"TEST1","CLR1","ADD4S","SUB4S","CMP4S","INS","EXT","ROL4","ROR4","REPC CMPSB","REPNC SCASW","CS:TEST1","CHKIND"};
 const unsigned lengths[]={3,6,2,2,2,3,4,3,3,2,2,6,4};unsigned i;
 for(i=0;i<sizeof lengths/sizeof lengths[0];i++){
  e86_disasm_t d;unsigned char code[16]={0};memcpy(code,codes[i],8);e86_disasm_model(&d,code,0x100,E86_CPU_NEC);
  check("NEC disassembly mnemonic/length",!strcmp(d.op,names[i])&&d.dat_n==lengths[i]&&(d.flags&E86_DFLAGS_V20));
 }
 {e86_disasm_t d;unsigned char code[16]={0x0f,0,0xc0};e86_disasm_model(&d,code,0,E86_CPU_FLAGS286|E86_CPU_INT6);check("286 decoder retained",!strcmp(d.op,"SLDT"));e86_disasm_model(&d,code,0,0);check("8086 POP CS decoder",!strcmp(d.op,"POP CS")&&d.dat_n==1);}
}
static int conversion_hook(void*p){e86_set_ax((e8086_t*)p,0xbeef);return 0;}
static void conversion(unsigned model){
 e8086_t*c=setup(model);unsigned base,a;
 /* VCPM.A86's AAD10 macro is D5 10 (hex), not D5 0A. */
 const unsigned char detect[]={0xb8,0x0f,0x0f,0xd5,0x10,0x3c,0xff,0x75,2,0x90,0x90};
 memcpy(ram+0x100,detect,sizeof detect);
 for(a=0;a<4;a++)e86_execute(c);
 check("VCPM exact processor detection branch",c->ip==0x10b&&e86_get_ax(c)==0xa5);
 for(base=0;base<256;base++)for(a=0;a<256;a++){
  c->ip=0x100;c->flg=0x8002;ram[0x100]=0xd4;ram[0x101]=base;e86_set_al(c,a);e86_pq_init(c);
  e86_execute(c);check("NEC AAM fixed decimal base",c->ip==0x102&&e86_get_ax(c)==((a/10)<<8)+(a%10));
  c->ip=0x100;c->flg=0x8002;ram[0x100]=0xd5;e86_set_ax(c,(a<<8)|a);e86_pq_init(c);
  e86_execute(c);check("NEC AAD fixed decimal base",c->ip==0x102&&e86_get_ax(c)==((a*11)&255));
 }
 e86_del(c);
 c=setup(model);ram[0x100]=0xd5;ram[0x101]=0;c->hook=conversion_hook;c->hook_ext=c;
 e86_execute(c);check("NEC PCE host-service hook retained",c->ip==0x102&&e86_get_ax(c)==0xbeef);e86_del(c);
 /* The Intel counterpart must keep its variable-base behavior. */
 c=setup(model);e86_set_8086(c);c->ip=0x100;memcpy(ram+0x100,detect,sizeof detect);e86_pq_init(c);
 for(a=0;a<4;a++)e86_execute(c);
 check("Intel VCPM detector stays negative",c->ip==0x109&&e86_get_ax(c)==0xff);e86_del(c);
}
int main(void){unsigned model;for(model=0;model<2;model++){conversion(model);decimal(model);fields(model);field_override(model);policy(model);}disassembly();printf("%u native NEC checks, %u failures\n",checks,failures);return failures?1:0;}
