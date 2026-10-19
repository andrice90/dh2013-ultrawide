import pefile,sys
pe=pefile.PE("D:/SteamLibrary/steamapps/common/Cabela's Dangerous Hunts Demo/CloakNTEngine.dll",fast_load=True); img=pe.get_memory_mapped_image(); b=pe.OPTIONAL_HEADER.ImageBase
print("timestamp", pe.FILE_HEADER.TimeDateStamp, "SizeOfImage", hex(pe.OPTIONAL_HEADER.SizeOfImage), "checksum", hex(pe.OPTIONAL_HEADER.CheckSum))
for s in sys.argv[1:]:
    a,n=s.split(':'); a=int(a,16); n=int(n,16)
    bs=img[a-b:a-b+n]
    print("%08x rva %06x [%d] %s"%(a,a-b,n,', '.join('0x%02X'%x for x in bs)))
