#include <sys/stat.h>
#include <fcntl.h>
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

static const char cmdline[] = "console=hvc0 root=/dev/vda rw";

void load_raw(const char* fn, state_t *ms, uint64_t where) {
  int fd,rc;
  uint8_t *buf = nullptr;
  //uint8_t *mem = ms->mem;
  
  ms->prealloc(0, 16384);
  //hack linux kernel
  fd = open("kernel.bin", O_RDONLY);
  uint64_t kern_addr = 0x200000 + 0x80000000;
  uint64_t kern_size = 0;

  kern_size = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);
  std::cout << "kern size : " << kern_size << " bytes\n";
  
  ms->prealloc(kern_addr, kern_size);
  
  buf = new uint8_t[kern_size];
  read(fd, buf, kern_size);
  printf("mapping from %lx to %lx\n", kern_addr, kern_addr+kern_size);
  for(size_t i = 0; i < kern_size; i++) {
    uint64_t pa  = kern_addr + i;
    uint8_t *ptr = ms->getpa8(pa);
    //printf("%lu remapped to %lx\n", pa, ptr);
    (*ptr) = buf[i];
  }
  delete [] buf;
  close(fd);


  fd = open(fn, O_RDONLY);
  size_t sz = lseek(fd, 0, SEEK_END);
  std::cout << "bbl size : " << sz << " bytes\n";
  lseek(fd, 0, SEEK_SET);
  buf = new uint8_t[sz];
  read(fd, buf, sz);

  ms->prealloc(where, sz);
  for(size_t i = 0; i < sz; i++) {
    uint64_t pa = where + i;
    *(ms->getpa8(pa)) = buf[i];    
  }
  delete [] buf;
  close(fd);
  


  int64_t fdt_addr = 0x1000 + 8 * 8;
  
  riscv_build_fdt(ms->getpa8(fdt_addr),
		  kern_addr,
		  kern_size,
		  0,0,
		  cmdline);

  
#define WRITE_WORD(EA,WORD) { *reinterpret_cast<uint32_t*>(ms->getpa8(EA)) = WORD; }

  WRITE_WORD(0x1000, 0x297 + 0x80000000 - 0x1000); //0
  WRITE_WORD(0x1004, 0x597); //1
  WRITE_WORD(0x1008, 0x58593 + ((fdt_addr - 4) << 20)); //2
  WRITE_WORD(0x100c, 0xf1402573); //3
  WRITE_WORD(0x1010, 0x00028067); //4
  ms->pc = 0x1000;
}
