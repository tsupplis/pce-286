/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/cpu/e8086/pqueue.c                                       *
 * Created:     2003-09-20 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2003-2026 Hampa Hug <hampa@hampa.ch>                     *
 *****************************************************************************/

/*****************************************************************************
 * This program is free software. You can redistribute it and / or modify it *
 * under the terms of the GNU General Public License version 2 as  published *
 * by the Free Software Foundation.                                          *
 *                                                                           *
 * This program is distributed in the hope  that  it  will  be  useful,  but *
 * WITHOUT  ANY   WARRANTY,   without   even   the   implied   warranty   of *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General *
 * Public License for more details.                                          *
 *****************************************************************************/


#include "e8086.h"
#include "internal.h"


/*
 * Before an instruction is executed the prefetch buffer (pq_buf) is
 * filled up to pq_fill bytes. After the instruction is executed
 * the instruction bytes are discarded and the remaining bytes up
 * to pq_size are copied to the front of the queue. Bytes between
 * pq_size and pq_fill are discarded (and possibly read again from
 * RAM).
 *
 * The prefetch buffer is filled with pq_fill instead of pq_size bytes
 * so that there is always at least one entire instruction in the
 * prefetch buffer. Yes, this is ugly.
 */


void e86_pq_init (e8086_t *c)
{
	c->pq_cnt = 0;
}

/*
 * Fill the prefetch queue
 */
void e86_pq_fill (e8086_t *c)
{
	unsigned       i;
	unsigned short val;
	unsigned short seg, ofs;
	unsigned       cnt;
	unsigned long  addr, base;

	seg = e86_get_cs (c);
	ofs = e86_get_ip (c);

	cnt = c->pq_fill;
	if ((c->cpu & (E86_CPU_FLAGS286 | E86_CPU_INT6)) ==
	    (E86_CPU_FLAGS286 | E86_CPU_INT6) &&
	    (unsigned long)ofs + cnt - 1 > c->seg_limit[E86_REG_CS]) {
		for (i = 0; i < cnt; i++) {
			c->pq[i] = ((unsigned long)ofs + i <= c->seg_limit[E86_REG_CS]) ?
				e86_get_mem8_phys(c,c->seg_base[E86_REG_CS]+ofs+i) : 0;
		}
		c->pq_cnt = 0;
		return;
	}

	/* The 286 retains its hidden CS base after RESET and LOADALL. */
	if ((c->msw & E286_MSW_PE) || (c->cpu & E86_CPU_FLAGS286)) {
		base = c->seg_base[E86_REG_CS];
	}
	else {
		base = (unsigned long) seg << 4;
	}

	if (ofs <= (0xffff - cnt)) {
		addr = base + ofs;

		i = c->pq_cnt;
		while (i + 1 < cnt) {
			val = e86_get_mem16_phys (c, addr + i);
			c->pq[i] = val & 0xff;
			c->pq[i + 1] = (val >> 8) & 0xff;
			i += 2;
		}
		if (i < cnt) {
			c->pq[i] = e86_get_mem8_phys (c, addr + i);
		}
	}
	else {
		for (i = c->pq_cnt; i < cnt; i++) {
			addr = base + ((ofs + i) & 0xffff);
			c->pq[i] = e86_get_mem8_phys (c, addr);
		}
	}

	c->pq_cnt = c->pq_size;
}

/*
 * Remove cnt bytes from the prefetch queue and copy the rest to the front
 */
void e86_pq_adjust (e8086_t *c, unsigned cnt)
{
	unsigned      n;
	unsigned char *d, *s;

	if (cnt >= c->pq_cnt) {
		c->pq_cnt = 0;
		return;
	}

	n = c->pq_cnt - cnt;
	s = c->pq + cnt;
	d = c->pq;

	while (n > 0) {
		*(d++) = *(s++);
		n -= 1;
	}

	c->pq_cnt -= cnt;
}

/* Validate consumed instruction bytes, not speculative queue fill. 286 encodings
 * use 16-bit addressing and have a ten-byte limit including all prefixes. */
static int e286_code_span (e8086_t *c, unsigned size)
{
 unsigned used = (c->ip - c->cur_ip) & 0xffff;
 if ((c->seg_ar[E86_REG_CS] & 0x80) &&
     (unsigned long)c->ip + size - 1 <= c->seg_limit[E86_REG_CS] &&
     used + size <= 10) return 0;
 e86_fault_error (c, 13, 0);
 c->delay += 10;
 return 1;
}

int e86_check_instruction (e8086_t *c, int decode)
{
 unsigned op, len=1, imm=0, modrm=0, m;
 if ((c->cpu & (E86_CPU_FLAGS286|E86_CPU_INT6)) !=
     (E86_CPU_FLAGS286|E86_CPU_INT6)) return 0;
 if (!decode) return e286_code_span(c,1);
 op=c->pq[0];
 if(op<0x40) {
  if(op==0x0f) {
   if(e286_code_span(c,2)) return 1;
   len=2; if(c->pq[1]<=3)modrm=1;
  } else if((op&7)<4)modrm=1;
  else if((op&7)==4)imm=1;
  else if((op&7)==5)imm=2;
 } else if(op==0x62||op==0x63)modrm=1;
 else if(op==0x66 && c->pq[1]==0x66)imm=3; /* PCE host hook */
 else if(op==0x68)imm=2;
 else if(op==0x69){modrm=1;imm=2;}
 else if(op==0x6a)imm=1;
 else if(op==0x6b){modrm=1;imm=1;}
 else if(op>=0x70&&op<=0x7f)imm=1;
 else if(op>=0x80&&op<=0x8f){modrm=1;if(op<=0x83)imm=op==0x81?2:1;}
 else if(op==0x9a||op==0xea)imm=4;
 else if(op>=0xa0&&op<=0xa3)imm=2;
 else if(op==0xa8)imm=1;
 else if(op==0xa9)imm=2;
 else if(op>=0xb0&&op<=0xbf)imm=op<0xb8?1:2;
 else if(op==0xc0||op==0xc1){modrm=1;imm=1;}
 else if(op==0xc2||op==0xca)imm=2;
 else if(op==0xc4||op==0xc5)modrm=1;
 else if(op==0xc6||op==0xc7){modrm=1;imm=op==0xc6?1:2;}
 else if(op==0xc8)imm=3;
 else if(op==0xcd||op==0xd4||op==0xd5)imm=1;
 else if((op>=0xd0&&op<=0xd3)||(op>=0xd8&&op<=0xdf))modrm=1;
 else if(op>=0xe0&&op<=0xe7)imm=1;
 else if(op==0xe8||op==0xe9)imm=2;
 else if(op==0xeb)imm=1;
 else if(op==0xf6||op==0xf7||op==0xfe||op==0xff)modrm=1;
 if(modrm) {
  if(e286_code_span(c,len+1))return 1;
  m=c->pq[len++];
  if((m&0xc0)==0x40)len++;
  else if((m&0xc0)==0x80||((m&0xc7)==6))len+=2;
  if((op==0xf6||op==0xf7)&&((m>>3)&7)==0)imm=op==0xf6?1:2;
 }
 return e286_code_span(c,len+imm);
}
