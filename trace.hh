#ifndef __tracehh__
#define __tracehh__

#include <cstdint>
#include <string>
#include <cstdlib>
#include <cstdio>

struct trace {
  static const uint64_t BUFLEN = 4096;
  struct entry {
    uint64_t addr;
    uint64_t attr;
  };
  FILE *fp;  
  uint64_t pos;
  entry buf[BUFLEN];
  trace(const std::string &fname) :
    fp(nullptr), pos(0) {
    fp = fopen(fname.c_str(), "w");
  }
  ~trace() {
    write_to_disk();
    fclose(fp);
  }
  void add(uint64_t addr, bool iside=false) {
    buf[pos].addr = addr;
    buf[pos].attr = iside ? 1 : 0;
    pos++;
    if(pos == BUFLEN) {
      write_to_disk();
    }
  }
  void write_to_disk();
};


#endif
