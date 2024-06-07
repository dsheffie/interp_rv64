#include "trace.hh"

void trace::write_to_disk() {
  fwrite(buf, sizeof(entry), pos, fp);
  pos = 0;  
}
