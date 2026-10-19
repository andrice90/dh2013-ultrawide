import sys,re
d=open(sys.argv[1],'rb').read()
a=int(sys.argv[2],16); b=int(sys.argv[3],16)
for m in re.finditer(rb'[\x20-\x7e]{3,}',d[a:b]):
    print(hex(a+m.start()), m.group().decode()[:120])
