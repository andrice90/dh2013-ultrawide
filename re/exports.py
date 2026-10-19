import pefile, sys, struct
pe = pefile.PE(sys.argv[1], fast_load=True)
d = pe.OPTIONAL_HEADER.DATA_DIRECTORY[0]
data = pe.get_memory_mapped_image()
base=pe.OPTIONAL_HEADER.ImageBase
(_,_,_,_,name,obase,nfun,nnames,afun,anames,aord)=struct.unpack_from('<IIHHIIIIIII',data,d.VirtualAddress)
out=[]
for i in range(nnames):
    nrva=struct.unpack_from('<I',data,anames+4*i)[0]
    o=struct.unpack_from('<H',data,aord+2*i)[0]
    frva=struct.unpack_from('<I',data,afun+4*o)[0]
    n=data[nrva:data.index(b'\0',nrva)].decode()
    out.append((n,base+frva))
with open(sys.argv[2],'w') as f:
    for n,a in out: f.write("%08x %s\n"%(a,n))
print(nfun,nnames)
