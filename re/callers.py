# find E8 rel32 calls / E9 jumps to target VAs
import pefile,struct,sys,bisect
G="D:/SteamLibrary/steamapps/common/Cabela's Dangerous Hunts Demo/CloakNTEngine.dll"
pe=pefile.PE(G,fast_load=True); img=pe.get_memory_mapped_image(); b=pe.OPTIONAL_HEADER.ImageBase
names={}
for l in open(sys.argv[1]):
    a,n=l.split(' ',1); names.setdefault(int(a,16),n.strip())
keys=sorted(names)
def fn(a):
    i=bisect.bisect_right(keys,a)-1
    return "%s+%x"%(names[keys[i]][:80],a-keys[i])
text=img[0x1000:0x1000+0x6427eb]
import numpy as np
arr=np.frombuffer(text,dtype=np.uint8)
for t in sys.argv[2:]:
    t=int(t,16)
    idx=np.where((arr[:-4]==0xE8)|(arr[:-4]==0xE9))[0]
    rel=(arr[idx+1].astype(np.int64)|(arr[idx+2].astype(np.int64)<<8)|(arr[idx+3].astype(np.int64)<<16)|(arr[idx+4].astype(np.int64)<<24))
    rel=np.where(rel>=2**31,rel-2**32,rel)
    dest=b+0x1000+idx+5+rel
    hits=idx[dest==t]
    print("== %08x %s: %d refs"%(t,names.get(t,''),len(hits)))
    for h in hits[:60]: print("   %08x %s"%(b+0x1000+h, fn(b+0x1000+h)))
