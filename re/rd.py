import pefile,struct,sys
G="D:/SteamLibrary/steamapps/common/Cabela's Dangerous Hunts Demo/CloakNTEngine.dll"
pe=pefile.PE(G,fast_load=True); img=pe.get_memory_mapped_image(); b=pe.OPTIONAL_HEADER.ImageBase
for s in sys.argv[1:]:
    t,a=s.split(':'); a=int(a,16)
    print(s, struct.unpack_from('<'+t,img,a-b)[0])
