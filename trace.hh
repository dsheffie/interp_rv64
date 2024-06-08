#ifndef __tracehh__
#define __tracehh__

#include <cstdint>
#include <string>
#include <cstdlib>
#include <cstdio>

struct cache;

struct trace {
  static const uint64_t BUFLEN = 4096;
  struct entry {
    uint64_t paddr;
    uint64_t vaddr;    
    uint64_t attr;
  };
  FILE *fp;  
  uint64_t pos;
  entry buf[BUFLEN];
  trace(const std::string &fname, bool rd = false) :
    fp(nullptr), pos(0) {
    fp = fopen(fname.c_str(), rd ? "rb" : "wb");
  }
  ~trace() {
    write_to_disk();
    fclose(fp);
  }
  void add(uint64_t vaddr,
	   uint64_t paddr,
	   int type);
  
  void write_to_disk();

  void simulate(cache *c);
};


#endif
