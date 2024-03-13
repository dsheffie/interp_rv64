#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <cinttypes>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <utility>
#include <cstdint>
#include <cstdarg>
#include <list>
#include <map>

#include "helper.hh"
#include "interpret.hh"
#include "globals.hh"
#include "fdt.hh"

void load_raw(const char* fn, state_t *ms, uint64_t where) {
  struct stat s;
  int fd,rc;
  char *buf = nullptr;
  uint8_t *mem = ms->mem;

  //hack linux kernel
  fd = open("kernel.bin", O_RDONLY);
  rc = fstat(fd, &s);
  uint64_t kern_start = 0x10000000;
  uint64_t kern_size = 0;

#if 1
  kern_size = s.st_size;
  buf = (char*)mmap(nullptr, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  for(size_t i = 0; i < s.st_size; i++) {
    mem[kern_start+i] = buf[i];
  }
  munmap(buf, s.st_size);
  close(fd);
#endif

  
  fd = open(fn, O_RDONLY);
  rc = fstat(fd,&s);
  buf = (char*)mmap(nullptr, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

  for(size_t i = 0; i < s.st_size; i++) {
    mem[where+i] = buf[i];
  }
  munmap(buf, s.st_size);

  int64_t fdt_addr = 0x1000 + 8 * 8;
  
  riscv_build_fdt(mem + fdt_addr,
		  kern_start,kern_size,
		  0,0,
		  nullptr);

  
#define WRITE_WORD(EA,WORD) { *reinterpret_cast<uint32_t*>(mem + EA) = WORD; }

  WRITE_WORD(0x1000, 0x297 + 0x80000000 - 0x1000); //0
  WRITE_WORD(0x1004, 0x597); //1
  WRITE_WORD(0x1008, 0x58593 + ((fdt_addr - 4) << 20)); //2
  WRITE_WORD(0x100c, 0xf1402573); //3
  WRITE_WORD(0x1010, 0x00028067); //4
  ms->pc = 0x1000;
}
