import pefile, sys, struct, capstone
G="D:/SteamLibrary/steamapps/common/Cabela's Dangerous Hunts Demo/CloakNTEngine.dll"
pe=pefile.PE(G,fast_load=True); img=pe.get_memory_mapped_image(); base=pe.OPTIONAL_HEADER.ImageBase
names={}
for l in open(sys.argv[1]):
    a,n=l.split(' ',1); names.setdefault(int(a,16),n.strip())
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
def rf(a):
    try: return struct.unpack_from('<f',img,a-base)[0]
    except: return None
for spec in sys.argv[2:]:
    a,n=(spec.split(':')+['60'])[:2]; a=int(a,16); n=int(n)
    print("=== %08x %s"%(a,names.get(a,'')))
    for i,ins in enumerate(md.disasm(img[a-base:a-base+n*8],a)):
        if i>=n: break
        c=''
        for tok in ins.op_str.replace('[',' ').replace(']',' ').replace(',',' ').split():
            if tok.startswith('0x'):
                v=int(tok,16)
                if v in names: c+=' <'+names[v][:90]+'>'
                elif 0x10644000<=v<0x10977000:
                    f=rf(v); 
                    if f is not None and 'dword' in ins.op_str and abs(f)<1e7 and (abs(f)>1e-6 or f==0): c+=' {%g}'%f
        print("  %08x %-8s %s%s"%(ins.address,ins.mnemonic,ins.op_str,c))
        if ins.mnemonic in('ret',) and n<200: break
