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

static const char cmdline[] = "debug keep_bootcon bootmem_debug console=csr_console";


#define AMT (1<<24)

void load_raw(const char* fn, state_t *ms, uint64_t where) {
  struct stat s;
  int fd,rc;
  char *buf = nullptr;
  uint8_t *mem = ms->mem;
  uint64_t kern_addr = 0x200000 + 0x80000000;
  uint64_t initrd_addr = 0;
  uint64_t kern_size = 0, initrd_size = 0;

  //hack linux kernel
  fd = open("kernel.bin", O_RDONLY);
  if(fd != -1) {
    rc = fstat(fd, &s);
    
    
    kern_size = s.st_size;
    buf = (char*)mmap(nullptr, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    for(size_t i = 0; i < s.st_size; i++) {
      mem[kern_addr+i] = buf[i];
    }
    munmap(buf, s.st_size);
    close(fd);
  }


  fd = open("initramfs.img", O_RDONLY);
  initrd_addr = ((kern_addr + kern_size + AMT - 1) / AMT) * AMT;
  if(fd != -1) {
    rc = fstat(fd, &s);
    initrd_size = s.st_size;
    buf = (char*)mmap(nullptr, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    for(size_t i = 0; i < s.st_size; i++) {
      mem[initrd_addr+i] = buf[i];
    }
    munmap(buf, s.st_size);
    close(fd);
  }

  
  fd = open(fn, O_RDONLY);
  rc = fstat(fd,&s);
  buf = (char*)mmap(nullptr, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  assert(buf != reinterpret_cast<void*>(-1L));
  
  for(size_t i = 0; i < s.st_size; i++) {
    mem[where+i] = buf[i];
  }
  munmap(buf, s.st_size);

  int64_t fdt_addr = 0x1000 + 8 * 8;

  printf("initrd_addr %lx, initrd_size %lx\n", initrd_addr, initrd_size);
  
  riscv_build_fdt(mem + fdt_addr,
		  kern_addr,
		  kern_size,
		  initrd_addr,
		  initrd_size,
		  cmdline);

  
#define WRITE_WORD(EA,WORD) { *reinterpret_cast<uint32_t*>(mem + EA) = WORD; }

  WRITE_WORD(0x1000, 0x297 + 0x80000000 - 0x1000); //0
  WRITE_WORD(0x1004, 0x597); //1
  WRITE_WORD(0x1008, 0x58593 + ((fdt_addr - 4) << 20)); //2
  WRITE_WORD(0x100c, 0xf1402573); //3
  WRITE_WORD(0x1010, 0x00028067); //4
  ms->pc = 0x1000;
}
