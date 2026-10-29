#!/usr/bin/env python3
# GPL-2.0-or-later. Mathematical references, NOT chip captures.
from pathlib import Path
import random
import mpmath as mp
mp.mp.prec=600
rng=random.Random(8087287)
def raw(v,rc=0):
 if not v:return (0,0)
 e=int(mp.floor(mp.log(v,2)));q=v*mp.power(2,63-e)
 s=int(mp.nint(q) if rc==0 else mp.ceil(q) if rc==2 else mp.floor(q))
 if s==2**64:s//=2;e+=1
 return s,e+16383
def value(a):return mp.mpf(a[0])*mp.power(2,a[1]-16383-63)
def fmt(a):return '{UINT64_C(0x%016x),0x%04x}'%a
angles=[mp.atan(mp.power(2,-k)) for k in range(16)]
inputs=[raw(mp.mpf(rng.randrange(1,2**64))/2**64*mp.pi/4) for _ in range(300)]
inputs+=[(0,0),(0xc90fdaa22168c234,0x3ffe)]
for e in [-16382,-1000,-65,-64,-63,-17,-16,-15,-1]:
 inputs.append((2**63,e+16383))
for a in angles[1:]:
 s,e=raw(a);inputs.extend([(s-1,e),(s,e),(s+1,e)])
rows=[]
for a in inputs:
 v=value(a);r=v;bits=[]
 for angle in angles:
  used=r>=angle;bits.append(used)
  if used:r-=angle
 x,y=3-r*r,3*r
 for k in reversed(range(16)):
  if bits[k]:x,y=x-y*mp.power(2,-k),y+x*mp.power(2,-k)
 if not a[0] or a[1]<=16383-64:x,y=mp.mpf(1),v
 for rc in range(4):rows.append('{%d,%s,%s,%s,%s},'%(rc,fmt(a),fmt(raw(y,rc)),fmt(raw(x,rc)),fmt(raw(mp.tan(v),rc))))
Path('tests/npx_tangent_vectors.h').write_text('/* Mathematical/algorithm references; mpmath, 600 bits, seed 8087287. */\nstatic const struct {unsigned rc;e87_real80_t angle,num,den,ratio;} tangents[]={\n'+'\n'.join(rows)+'\n};\n')
