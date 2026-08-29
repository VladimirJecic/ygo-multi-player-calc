import struct
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
P = os.environ.get('NEURON_LIBIL2CPP',
                   os.path.join(_ROOT, 'apk/extracted/lib/arm64-v8a/libil2cpp.so'))
d=open(P,'rb').read()
e_shoff=struct.unpack_from('<Q',d,0x28)[0]; e_shentsize,e_shnum,e_shstrndx=struct.unpack_from('<HHH',d,0x3a)
_secs=[]
for i in range(e_shnum):
    o=e_shoff+i*e_shentsize
    nm,typ,flags,addr,off,size,link,info,align,entsz=struct.unpack_from('<IIQQQQIIQQ',d,o)
    _secs.append((nm,addr,off,size))
_st=_secs[e_shstrndx][2]
def _sn(nm):
    e=d.index(b'\0',_st+nm); return d[_st+nm:e].decode()
SEC={_sn(s[0]):s for s in _secs}
_r=SEC['.rela.dyn']; _buf=d[_r[2]:_r[2]+_r[3]]
REL={}
for i in range(_r[3]//24):
    ro,ri,ra=struct.unpack_from('<QQq',_buf,i*24)
    REL[ro]=ra
def a2o(a):
    for k,(nm,addr,off,size) in SEC.items():
        if size and addr and addr<=a<addr+size: return off+(a-addr)
    return None
def qword(a):
    if a in REL: return REL[a]
    return struct.unpack_from('<Q',d,a2o(a))[0]
CGM=0x2ef35c8               # Assembly-CSharp codegen module
MPCOUNT=qword(CGM+8)&0xffffffff
MPTR=qword(CGM+16)
def method_addr(rid):
    return qword(MPTR+(rid-1)*8)
