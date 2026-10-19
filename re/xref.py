# find instructions referencing given 32-bit immediates/displacements in .text
import pefile,struct,sys,capstone
G="D:/SteamLibrary/steamapps/common/Cabela's Dangerous Hunts Demo/CloakNTEngine.dll"
pe=pefile.PE(G,fast_load=True); img=pe.get_memory_mapped_image(); b=pe.OPTIONAL_HEADER.ImageBase
names={}
for l in open(S:=sys.argv[1]):
    a,n=l.split(' ',1); names[int(a,16)]=n.strip()
import bisect
keys=sorted(names)
def fn(a):
    i=bisect.bisect_right(keys,a)-1
    return "%s+%x"%(names[keys[i]][:70],a-keys[i]) if i>=0 else '?'
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
text=img[0x1000:0x1000+0x6427eb]
for v in sys.argv[2:]:
    pat=struct.pack('<I',int(v,16))
    i=0; seen=set()
    while True:
        i=text.find(pat,i)
        if i<0: break
        # try starts 1..7 bytes before
        for back in range(1,8):
            st=i-back
            ins=next(md.disasm(text[st:st+16],b+0x1000+st),None)
            if ins and ins.size>=back+4 and ins.address not in seen:
                seen.add(ins.address)
                print(v,"%08x %-6s %-40s %s"%(ins.address,ins.mnemonic,ins.op_str,fn(ins.address)))
                break
        i+=1
