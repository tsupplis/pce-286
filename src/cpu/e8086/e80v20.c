/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/cpu/e8086/e80v20.c                                       *
 * Created:     2025-01-01                                                    *
 * Copyright:   (C) 2025 pce contributors                                    *
 *****************************************************************************/

/*****************************************************************************
 * This program is free software. You can redistribute it and / or modify it *
 * under the terms of the GNU General Public License version 2 as  published *
 * by the Free Software Foundation.                                          *
 *                                                                           *
 * This program is distributed in the hope  that  it  will  be  useful,  but *
 * WITHOUT  ANY   WARRANTY,   without   even   the   implied   warranty   of *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU  General *
 * Public License for more details.                                          *
 *****************************************************************************/

/* NEC uPD70108/70116 native extensions. Encoding and flag behavior follow
 * the NEC User's Manual, instruction section 12. Unsupported extensions
 * take the existing undefined-instruction path; they must not alias bit ops.
 * 8080 execution is delegated through the NEC adapter. */
#include "e8086.h"
#include "internal.h"

static unsigned op_nec_bit(e8086_t *c)
{
 unsigned op=c->pq[1], word=op&1, immediate=op&8;
 unsigned kind=(op>>1)&3, bit, mask, value;
 e86_get_ea_ptr(c,c->pq+2);
 value=word?e86_get_ea16(c):e86_get_ea8(c);
 bit=(immediate?c->pq[c->ea.cnt+2]:e86_get_cl(c)) & (word?15:7);
 mask=1U<<bit;
 switch(kind) {
 case 0:
  e86_set_zf(c,(value&mask)==0);
  e86_set_cf(c,0);e86_set_of(c,0);
  break;
 case 1: value &= ~mask;break;
 case 2: value |= mask;break;
 case 3: value ^= mask;break;
 }
 if(kind) {
  if(word)e86_set_ea16(c,value);else e86_set_ea8(c,value);
 }
 e86_set_clk_ea(c,immediate?4:3,12);
 return c->ea.cnt+2+(immediate!=0);
}

static unsigned op_nec_rotate(e8086_t *c)
{
 unsigned value,al,result,next;
 e86_get_ea_ptr(c,c->pq+2);
 value=e86_get_ea8(c);al=e86_get_al(c);
 if(c->pq[1]==0x28) {
  result=((value<<4)|(al&15))&255;next=value>>4;
 } else {
  result=(value>>4)|((al&15)<<4);next=value&15;
 }
 e86_set_ea8(c,result);e86_set_al(c,(al&0xf0)|next);
 /* All flags preserved; upper AL nibble is unspecified by NEC. */
 if(c->pq[1]==0x28)e86_set_clk_ea(c,25,28);
 else e86_set_clk_ea(c,29,33);
 return c->ea.cnt+2;
}

/* Packed decimal strings run from least significant byte upwards. IX/IY
 * and CL are unchanged (the manual chains ADD/SUB/CMP using the same pointers).
 * Odd digit counts operate on the full final byte; its upper digit is undefined. */
static unsigned op_nec_bcd(e8086_t *c)
{
 unsigned count=(e86_get_cl(c)+1)/2,i,carry=0,nonzero=0;
 unsigned src=e86_get_seg_reg(c,E86_REG_DS),si=e86_get_si(c),di=e86_get_di(c);
 for(i=0;i<count;i++) {
  unsigned a=e86_get_mem8_sreg(c,src,(si+i)&65535);
  unsigned b=e86_get_mem8_sreg(c,E86_REG_ES,(di+i)&65535),result;
  int value;
  a=(a>>4)*10+(a&15);b=(b>>4)*10+(b&15);
  if(c->pq[1]==0x20) {value=b+a+carry;carry=value>99;if(carry)value-=100;}
  else {value=(int)b-(int)a-(int)carry;carry=value<0;if(carry)value+=100;}
  result=((unsigned)(value/10)<<4)|(unsigned)(value%10);nonzero|=result;
  if(c->pq[1]!=0x26)e86_set_mem8_sreg(c,E86_REG_ES,(di+i)&65535,result);
 }
 e86_set_cf(c,carry);e86_set_zf(c,nonzero==0);
 e86_set_clk(c,7+19*count);return 2;
}

/* Bit offsets are relative to a word. Crossing bit 15 advances IX/IY by two
 * bytes and retains the remaining bit offset. All arithmetic is unsigned. */
static unsigned op_nec_field(e8086_t *c)
{
 unsigned op=c->pq[1],reg=c->pq[2]&7,immediate=op&8;
 unsigned bit=e86_get_reg8(c,reg)&15;
 unsigned length=((immediate?c->pq[3]:e86_get_reg8(c,(c->pq[2]>>3)&7))&15)+1;
 unsigned extract=op&2,seg=extract?e86_get_seg_reg(c,E86_REG_DS):E86_REG_ES;
 unsigned ofs=extract?e86_get_si(c):e86_get_di(c),next=bit+length;
 unsigned long mask=(1UL<<length)-1,data;
 if((c->pq[2]&0xc0)!=0xc0)return e86_undefined(c);
 data=e86_get_mem16_sreg(c,seg,ofs);
 if(next>16)data|=(unsigned long)e86_get_mem16_sreg(c,seg,(ofs+2)&65535)<<16;
 if(extract)e86_set_ax(c,(data>>bit)&mask);
 else {
  data=(data&~(mask<<bit))|(((unsigned long)e86_get_ax(c)&mask)<<bit);
  e86_set_mem16_sreg(c,seg,ofs,data&65535);
  if(next>16)e86_set_mem16_sreg(c,seg,(ofs+2)&65535,data>>16);
 }
 e86_set_reg8(c,reg,next&15);
 if(extract)e86_set_si(c,(ofs+2*(next>>4))&65535);
 else e86_set_di(c,(ofs+2*(next>>4))&65535);
 /* Functional timing estimate; bus-cycle accuracy is outside this core. */
 e86_set_clk(c,30+length*2);return immediate?4:3;
}

static unsigned op_0f_v20(e8086_t *c)
{
 if(c->pq[1]==0xff) { e86_nec_brkem(c,c->pq[2]); return 0; }
 if(c->pq[1]>=0x10 && c->pq[1]<=0x1f)return op_nec_bit(c);
 if(c->pq[1]==0x28 || c->pq[1]==0x2a)return op_nec_rotate(c);
 if(c->pq[1]==0x20 || c->pq[1]==0x22 || c->pq[1]==0x26)return op_nec_bcd(c);
 if(c->pq[1]==0x31 || c->pq[1]==0x33 || c->pq[1]==0x39 || c->pq[1]==0x3b)return op_nec_field(c);
 return e86_undefined(c);
}

static unsigned op_nec_repeat(e8086_t *c)
{
 c->prefix &= ~(E86_PREFIX_REP|E86_PREFIX_REPN);
 c->prefix |= E86_PREFIX_NEW|E86_PREFIX_CARRY|
  (c->pq[0]==0x65?E86_PREFIX_REP:E86_PREFIX_REPN);
 e86_set_clk(c,2);return 1;
}

/* NEC CVTBO/CVTOB (AAM/AAD): fixed decimal base, unlike Intel.
 * Keep PCE's explicitly handled D5 00 host-service ABI. Without a handler,
 * even an encoding with a zero second byte performs decimal conversion. */
static unsigned op_nec_aam(e8086_t*c)
{
 unsigned a=e86_get_al(c);
 e86_set_ax(c,((a/10)<<8)|(a%10));
 e86_set_flg_szp_8(c,a%10);e86_set_clk(c,15);return 2;
}
static unsigned op_nec_aad(e8086_t*c)
{
 unsigned a;
 if(c->pq[1]==0 && e86_hook(c)==0) {e86_set_clk(c,4);return 2;}
 a=(e86_get_ah(c)*10+e86_get_al(c))&255;
 e86_set_ax(c,a);e86_set_flg_szp_8(c,a);e86_set_clk(c,7);return 2;
}

void e86_set_v30(e8086_t *c)
{
 e86_set_80186(c);
 c->cpu &= ~(E86_CPU_REP_BUG|E86_CPU_MASK_SHIFT);
 c->cpu |= E86_CPU_NEC;
 c->op[0x0f]=op_0f_v20;
 c->op[0xd4]=op_nec_aam;c->op[0xd5]=op_nec_aad;
 c->op[0x64]=op_nec_repeat;c->op[0x65]=op_nec_repeat;
 /* Keep native CHKIND (62h), inherited as BOUND from the 80186.
  * The remaining NEC families are separate follow-ups. */
}

void e86_set_v20(e8086_t *c)
{
 e86_set_v30(c);c->cpu|=E86_CPU_8BIT;e86_set_pq_size(c,4);
}
