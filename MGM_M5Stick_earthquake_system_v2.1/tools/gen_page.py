import gzip,sys
src,dst=sys.argv[1],sys.argv[2]
raw=open(src,'rb').read();gz=gzip.compress(raw,9,mtime=0)
with open(dst,'w') as f:
    f.write('#pragma once\n#include <Arduino.h>\n// Otomatik uretildi: web/page.html (gzip)\n')
    f.write('const size_t PAGE_GZ_LEN = %d;\nconst uint8_t PAGE_GZ[] PROGMEM = {\n'%len(gz))
    for i in range(0,len(gz),24): f.write(','.join(str(b) for b in gz[i:i+24])+',\n')
    f.write('};\n')
print(len(raw),'->',len(gz))
