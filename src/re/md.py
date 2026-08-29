import struct
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
P = os.environ.get('NEURON_METADATA',
                   os.path.join(_ROOT, 'apk/extracted/assets/bin/Data/Managed/Metadata/global-metadata.dat'))
d=open(P,'rb').read()
v=list(struct.unpack_from('<95I',d,0))
S=[tuple(v[2+3*i:5+3*i]) for i in range(31)]
STR=S[2][0]
def cstr(i):
    e=d.index(b'\0',STR+i); return d[STR+i:e].decode('utf8','replace')
TOFF,_,TCNT=S[19]
MOFF,_,MCNT=S[5]
FOFF,_,FCNT=S[11]
LOFF,_,LCNT=S[0]   # string literal offset table
LDOFF,_,_=S[1]
def td(i):
    return struct.unpack_from('<19I',d,TOFF+i*76)
def tname(i):
    u=td(i); ns=cstr(u[1]); return (ns+'.' if ns else '')+cstr(u[0])
def md_(i):
    raw=d[MOFF+i*30:MOFF+(i+1)*30]
    u32=struct.unpack_from('<5I',raw); u16=struct.unpack_from('<5H',raw,20)
    return dict(name=cstr(u32[0]),decl=u32[1]&0xffff,ret=u32[1]>>16,
                paramStart=u32[3],rid=u32[4]>>16,gc=u32[4]&0xffff,
                flags=u16[1],iflags=u16[2],slot=u16[3],pcount=u16[4])
def fd(i):
    off=FOFF+i*10
    n,t=struct.unpack_from('<II',d,off); tok=struct.unpack_from('<H',d,off+8)[0]
    return dict(name=cstr(n),type=t,tok=tok)
def litstr(i):
    o1,o2=struct.unpack_from('<II',d,LOFF+i*4)
    return d[LDOFF+o1:LDOFF+o2].decode('utf8','replace')
