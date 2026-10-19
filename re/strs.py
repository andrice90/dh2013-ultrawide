import sys,re
d=open(sys.argv[1],'rb').read()
pat=re.compile(sys.argv[2].encode(),re.I)
for m in re.finditer(rb'[\x20-\x7e]{4,}',d):
    if pat.search(m.group()): print(hex(m.start()), m.group().decode()[:160])
