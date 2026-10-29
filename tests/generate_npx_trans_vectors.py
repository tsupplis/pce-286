#!/usr/bin/env python3
# GPL-2.0-or-later; optional oracle generation, requires mpmath 1.3.0.
from pathlib import Path
import mpmath as mp,random
mp.mp.dps=180

rng=random.Random(871287)
def raw(v):
 if not v:return (0,0)
 sign=int(v<0);v=abs(v);e=int(mp.floor(mp.log(v,2)));s=int(mp.nint(v*mp.power(2,63-e)))
 if s==2**64:s//=2;e+=1
 return s,(e+16383)|(sign<<15)
def val(a):
 s,e=a;return (-1 if e&32768 else 1)*mp.mpf(s)*mp.power(2,(e&32767)-16383-63)
def fmt(a):return '{UINT64_C(0x%016x),0x%04x}'%a
cases=[]
for op in [0xf0,0xf1,0xf3,0xf9]:
 for k in range(250):
  if op==0xf0:x=raw(mp.mpf(rng.randrange(1,2**64))/2**65)
  elif op==0xf1:x=(rng.randrange(2**63,2**64),rng.randrange(100,32600))
  elif op==0xf3:x=raw(mp.mpf(rng.randrange(1,2**64))/2**63)
  else:x=raw(mp.mpf(rng.randrange(-2**63,2**63))/2**65)
  y=raw(mp.mpf(rng.randrange(-100000,100000))/32768)
  if op==0xf3:y=raw(val(x)*mp.mpf(rng.randrange(1,2**64))/2**64)
  cases.append((op,x,y))
# Near one, tiny normal, close domain boundary and exponent-extreme ratios.
for e in [-16000,-1000,-120,-64,-1]:
 x=raw(mp.power(2,e));cases.extend([(0xf0,x,raw(1)),(0xf3,raw(1),x),(0xf9,x if e<-1 else raw(mp.mpf('.25')),raw(1)),(0xf9,raw(-val(x)) if e<-1 else raw(-mp.mpf('.25')),raw(1))])
for x in [(2**63+1,16383),(2**64-1,16382),(2**63,16383),(2**63,16384)]:cases.append((0xf1,x,raw(3)))
rows=[]
for op,x,y in cases:
 # Resolve curvature below one output ulp for the tiny explicit inputs.
 mp.mp.prec=max(600,2*max(0,16383-(x[1]&32767),16383-(y[1]&32767))+256)
 a,b=val(x),val(y)
 z=mp.expm1(a*mp.log(2)) if op==0xf0 else b*mp.log(a,2) if op==0xf1 else mp.atan(b/a) if op==0xf3 else b*mp.log1p(a)/mp.log(2)
 for rc in range(4):
  if not z:r=(0,0)
  else:
   sign=int(z<0);e=int(mp.floor(mp.log(abs(z),2)));v=abs(z)*mp.power(2,63-e)
   s=int(mp.nint(v) if rc==0 else mp.ceil(v) if (rc==1 and sign) or (rc==2 and not sign) else mp.floor(v))
   if s==2**64:s//=2;e+=1
   r=s,(e+16383)|(sign<<15)
  rows.append('{0x%x,%d,%s,%s,%s},'%(op,rc,fmt(x),fmt(y),fmt(r)))
Path('tests/npx_trans_vectors.h').write_text('/* Generated with mpmath 1.3.0, at least 600 bits (adaptive for tiny inputs), seed 871287.\n * Mathematical references, not original-chip readbacks. */\nstatic const struct vector {unsigned op,rc;e87_real80_t x,y,result;} vectors[]={\n'+'\n'.join(rows)+'\n};\n')
