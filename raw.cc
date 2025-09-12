// BSD 3-Clause License

// Copyright (c) 2025, dsheffie

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:

// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.

// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.

// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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

static const char cmdline[] = "debug keep_bootcon console=csr_console";


#define AMT (1<<24)

#define BOOT_ROM_ADDR 0x1000UL

void load_raw(const char* fn, state_t *ms) {
  struct stat s;
  int fd,rc;
  char *buf = nullptr;
  uint8_t *mem = ms->mem;
  const uint64_t kern_addr = 0x200000UL + globals::ram_phys_start;
  uint64_t kern_end = kern_addr;
  uint64_t initrd_addr = 0;
  uint64_t kern_size = 0, initrd_size = 0;
  
  fd = open(fn, O_RDONLY);
  rc = fstat(fd,&s);
  buf = (char*)mmap(nullptr, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if(buf == reinterpret_cast<void*>(-1L)) {
    std::cout << "mmap of " << fn << " failed!\n";
    exit(-1);
  }
  for(size_t i = 0; i < s.st_size; i++) {
    mem[globals::fw_start_addr+i] = buf[i];
  }
  munmap(buf, s.st_size);
  close(fd);

  //hack linux kernel
  fd = open("kernel.bin", O_RDONLY);
  if(fd != -1) {
    rc = fstat(fd, &s);
    kern_size = s.st_size;
    buf = (char*)mmap(nullptr, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    for(size_t i = 0; i < kern_size; i++) {
      mem[kern_addr+i] = buf[i];
    }
    /* save the size of the kernel */
    *reinterpret_cast<uint64_t*>(&mem[kern_addr-sizeof(uint64_t)]) = kern_size;
    kern_end = kern_addr+kern_size;
    munmap(buf, kern_size);
    close(fd);
  }
  
  
  std::cout << "firmware starts " << std::hex <<globals::fw_start_addr
	    << ", firmware size " << s.st_size 
	    << ", dram starts at " << globals::ram_phys_start
	    << ", kernel at " << kern_addr
	    << std::dec
	    << ", kernel size " << (kern_end-kern_addr)
	    << "\n";
    
  

  if(not(initrd_addr == 0 and initrd_size == 0)) {
    std::cout << "initrd_addr " << std::hex << initrd_addr << std::hex
	      << ", initrd_size " << initrd_size << "\n";
  }

  riscv_build_fdt(&mem[globals::fdt_addr],
		  kern_addr,
		  kern_size,
		  initrd_addr,
		  initrd_size,
		  cmdline);
   
#define WRITE_WORD(OFFS,WORD) { *reinterpret_cast<uint32_t*>(mem + BOOT_ROM_ADDR + OFFS) = WORD; }
  
  WRITE_WORD(0, 0x297 + globals::fw_start_addr - BOOT_ROM_ADDR); //0
  WRITE_WORD(4, 0xf597); //auipc
  WRITE_WORD(8, 0x58593 + ((globals::fdt_addr - 4) << 20)); //2
  WRITE_WORD(12, 0xf1402573); //3
  WRITE_WORD(16, 0x00028067); //4
  ms->pc = BOOT_ROM_ADDR;
}
