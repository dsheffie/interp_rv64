#ifndef __INTERPRET_HH__
#define __INTERPRET_HH__

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <ostream>
#include <map>
#include <string>
#include <cassert>
#include "nway_cache.hh"
#include "temu_code.hh"

#define MARGS 20

enum riscv_priv {
  priv_user = 0,
  priv_supervisor,
  priv_hypervisor,
  priv_machine,
};

struct mip_t {
  uint64_t z0 : 1;
  uint64_t ssip : 1;
  uint64_t z2 : 1;
  /* machine software interrupt pending */
  uint64_t msip : 1;
  uint64_t z4 : 1;
  uint64_t stip : 1;
  uint64_t z6 : 1;
  /* machine time interrupt pending */
  uint64_t mtip : 1;
  uint64_t z8 : 1;
  uint64_t seip : 1;
  uint64_t z10 : 1;
  /* machine external interrupt pending */
  uint64_t meip : 1;
  uint64_t z : 52;
};

struct mie_t {
  uint64_t z0 : 1;
  uint64_t ssie : 1;
  uint64_t z2 : 1;
  uint64_t msie : 1;
  uint64_t z4 : 1;
  uint64_t stie : 1;
  uint64_t z6 : 1;
  uint64_t mtie : 1;
  uint64_t z8 : 1;
  uint64_t seie : 1;
  uint64_t z10 : 1;
  uint64_t meie : 1;
  uint64_t z : 52;
};

struct satp_t {
  uint64_t ppn : 44;
  uint64_t asid : 16;
  uint64_t mode : 4;
};

struct virtio;
struct uart;

struct state_t{
  uint64_t pc;
  uint64_t last_pc;
  uint64_t last_call;
  int64_t gpr[32];
  uint8_t *mem;
  uint8_t brk;
  uint64_t epc;
  uint64_t llsc_addr;
  uint64_t maxicnt;
  uint64_t icnt;
  uint64_t ipgszcnt[4];
  uint64_t dpgszcnt[4];
  cache *icache;
  cache *dcache;
  riscv_priv priv;
  
  /* lots of CSRs */
  int64_t mstatus;
  int64_t misa;
  int64_t mideleg;
  int64_t medeleg;
  int64_t mscratch;
  int64_t mhartid;
  int64_t mtvec;
  int64_t mcounteren;
  int64_t mie;
  int64_t mip;
  int64_t mcause;
  int64_t mepc;
  int64_t mtval;
  int64_t sscratch;
  int64_t scause;
  int64_t stvec;
  int64_t sepc;
  int64_t sip;
  int64_t stval;
  int64_t satp;
  int64_t scounteren;
  int64_t pmpaddr0;
  int64_t pmpaddr1;
  int64_t pmpaddr2;
  int64_t pmpaddr3;
  int64_t pmpcfg0;
  int64_t mtimecmp;
  virtio *vio;
  uart *u8250;
  int xlen() const {
    return 64;
  }
  int64_t get_time() const;
  
  void sext_xlen(int64_t x, int i) {
    gpr[i] = (x << (64-xlen())) >> (64-xlen());
  }
  uint32_t get_reg_u32(int id) const {
    return *reinterpret_cast<const uint32_t*>(&gpr[id]);
  }
  int32_t get_reg_i32(int id) const {
    return *reinterpret_cast<const int32_t*>(&gpr[id]);
  }
  uint64_t get_reg_u64(int id) const {
    return *reinterpret_cast<const uint64_t*>(&gpr[id]);
  }
  bool unpaged_mode() const {
    if((priv == priv_machine) or (priv == priv_hypervisor)) {
      return true;
    }
    if( ((satp >> 60)&0xf) == 0) {
      return true;
    }
    return false;
  }
  bool irqs_enabled() const {
    switch(priv)
      {
      case priv_user:
	return (mstatus & MSTATUS_UIE) |
	  (mstatus & MSTATUS_SIE) |
	  (mstatus & MSTATUS_MIE);
      case priv_hypervisor:
	assert(false);
      case priv_supervisor:
	return (mstatus & MSTATUS_SIE) | (mstatus & MSTATUS_MIE);		
      case priv_machine:
	return (mstatus & MSTATUS_MIE);		
      }
    return false;
  }
  
  bool memory_map_check(uint64_t pa, bool store = false, int64_t x = 0);
  int8_t load8(uint64_t pa);
  int64_t load8u(uint64_t pa);  
  int16_t load16(uint64_t pa);
  int64_t load16u(uint64_t pa);   
  int32_t load32(uint64_t pa);
  int64_t load32u(uint64_t pa);  
  int64_t load64(uint64_t pa);
  void store8(uint64_t pa,  int8_t x);
  void store16(uint64_t pa, int16_t x); 
  void store32(uint64_t pa, int32_t x);
  void store64(uint64_t pa, int64_t x);

  
  uint64_t translate(uint64_t ea, int &fault, int sz,
		     bool store = false, bool fetch = false);

   
};

void handle_syscall(state_t *s, uint64_t tohost);


struct utype_t {
  uint32_t opcode : 7;
  uint32_t rd : 5;
  uint32_t imm : 20;
};

struct branch_t {
  uint32_t opcode : 7;
  uint32_t imm11 : 1; //8
  uint32_t imm4_1 : 4; //12
  uint32_t sel: 3; //15
  uint32_t rs1 : 5; //20
  uint32_t rs2 : 5; //25
  uint32_t imm10_5 : 6; //31
  uint32_t imm12 : 1; //32
};

struct jal_t {
  uint32_t opcode : 7;
  uint32_t rd : 5;
  uint32_t imm19_12 : 8;
  uint32_t imm11 : 1;
  uint32_t imm10_1 : 10;
  uint32_t imm20 : 1;
};

struct jalr_t {
  uint32_t opcode : 7;
  uint32_t rd : 5; //12
  uint32_t mbz : 3; //15
  uint32_t rs1 : 5; //20
  uint32_t imm11_0 : 12; //32
};

struct rtype_t {
  uint32_t opcode : 7;
  uint32_t rd : 5;
  uint32_t sel: 3;
  uint32_t rs1 : 5;
  uint32_t rs2 : 5;
  uint32_t special : 7;
};

struct itype_t {
  uint32_t opcode : 7;
  uint32_t rd : 5;
  uint32_t sel : 3;
  uint32_t rs1 : 5;
  uint32_t imm : 12;
};

struct store_t {
  uint32_t opcode : 7;
  uint32_t imm4_0 : 5; //12
  uint32_t sel : 3; //15
  uint32_t rs1 : 5; //20
  uint32_t rs2 : 5; //25
  uint32_t imm11_5 : 7; //32
};

struct load_t {
  uint32_t opcode : 7;
  uint32_t rd : 5; //12
  uint32_t sel : 3; //15
  uint32_t rs1 : 5; //20
  uint32_t imm11_0 : 12; //32
};

struct amo_t {
  uint32_t opcode : 7;
  uint32_t rd : 5; //12
  uint32_t sel : 3; //15
  uint32_t rs1 : 5; //20
  uint32_t rs2 : 5; //25
  uint32_t rl : 1; //27
  uint32_t aq : 1;
  uint32_t hiop : 5;
};

union riscv_t {
  rtype_t r;
  itype_t i;
  utype_t u;
  jal_t j;
  jalr_t jj;
  branch_t b;
  store_t s;
  load_t l;
  amo_t a;
  uint32_t raw;
  riscv_t(uint32_t x) : raw(x) {}
};

struct mstatus_t {
  uint64_t j0 : 1;
  uint64_t sie : 1;
  uint64_t j2 : 1;
  uint64_t mie : 1;
  uint64_t j4 : 1;
  uint64_t spie : 1;
  uint64_t ube : 1;
  uint64_t mpie : 1;
  uint64_t spp : 1;
  uint64_t vs : 2;
  uint64_t mpp : 2;
  uint64_t fs : 2;
  uint64_t xs : 2;
  uint64_t mprv : 1;
  uint64_t sum : 1;
  uint64_t mxr : 1;
  uint64_t tvm : 1;
  uint64_t tw : 1;
  uint64_t tsr : 1;
  uint64_t junk23 : 9;
  uint64_t uxl : 2;
  uint64_t sxl : 2;
  uint64_t sbe : 1;
  uint64_t mbe : 1;
  uint64_t junk38 : 25;
  uint64_t sd : 1;
};

union csr_t {
  satp_t satp;
  mip_t mip;
  mie_t mie;
  mstatus_t mstatus;
  uint64_t raw;
  csr_t(uint64_t x) : raw(x) {}
};

static inline std::ostream &operator<<(std::ostream &out, mie_t mie) {
  if(mie.ssie)
    out << "ssie = " << mie.ssie << " : supervisor-level sw interrupts\n";
  if(mie.msie)
    out << "msie = " << mie.msie << " : machine-level sw interrupts\n";
  if(mie.stie)
    out << "stie = " << mie.stie << " : supervisor-level timer interrupts\n";
  if(mie.mtie)
    out << "mtie = " << mie.mtie << " : machine-level timer interrupts\n";
  if(mie.seie)
    out << "seie = " << mie.seie << " : supervisor-level external interrupts\n";
  if(mie.meie)
    out << "meie = " << mie.meie << " : machine-level external interrupts\n";  
  return out;
}


static inline std::ostream &operator<<(std::ostream &out, mstatus_t mstatus) {
  out << "sie  " << mstatus.sie << "\n";
  out << "mie  " << mstatus.mie << "\n";
  out << "spie " << mstatus.spie << "\n";
  out << "ube  " << mstatus.ube << "\n";  
  out << "mpie " << mstatus.mpie << "\n";
  out << "spp  " << mstatus.spp << "\n";
  out << "mpp  " << mstatus.mpp << "\n";
  out << "fs   " << mstatus.fs << "\n";
  out << "xs   " << mstatus.xs << "\n";      
  out << "mprv " << mstatus.mprv << "\n";
  out << "sum  " << mstatus.sum << "\n";
  out << "mxr  " << mstatus.mxr << "\n";
  out << "tvm  " << mstatus.tvm << "\n";
  out << "tw   " << mstatus.tw << "\n";      
  out << "tsr  " << mstatus.tsr << "\n";
  out << "uxl  " << mstatus.uxl << "\n";
  out << "sxl  " << mstatus.sxl << "\n";
  out << "sbe  " << mstatus.sbe << "\n";
  out << "mbe  " << mstatus.mbe << "\n";
  out << "sd   " << mstatus.sd << "\n";
  return out;
}

static inline void what_changed(std::ostream &out, mstatus_t a, mstatus_t b) {
  if(a.sie != b.sie) {
    out << "a.sie = " << a.sie << ", b.sie = " << b.sie << "\n";
  }
  if(a.mie != b.mie) {
    out << "a.mie = " << a.mie << ", b.mie = " << b.mie << "\n";
  }  
  if(a.spie != b.spie) {
    out << "a.spie = " << a.spie << ", b.spie = " << b.spie << "\n";
  }
  if(a.ube != b.ube) {
    out << "a.ube = " << a.ube << ", b.ube = " << b.ube << "\n";
  }    
  if(a.mpie != b.mpie) {
    out << "a.mpie = " << a.mpie << ", b.mpie = " << b.mpie << "\n";
  }    
  if(a.spp != b.spp) {
    out << "a.spp = " << a.spp << ", b.spp = " << b.spp << "\n";
  }    
  if(a.mpp != b.mpp) {
    out << "a.mpp = " << a.mpp << ", b.mpp = " << b.mpp << "\n";
  }    
  if(a.fs != b.fs) {
    out << "a.fs = " << a.fs << ", b.fs = " << b.fs << "\n";
  }
  if(a.xs != b.xs) {
    out << "a.xs = " << a.xs << ", b.xs = " << b.xs << "\n";
  }
  if(a.mprv != b.mprv) {
    out << "a.mprv = " << a.mprv << ", b.mprv = " << b.mprv << "\n";
  }
  if(a.sum != b.sum) {
    out << "a.sum = " << a.sum << ", b.sum = " << b.sum << "\n";
  }
  if(a.mxr != b.mxr) {
    out << "a.mxr = " << a.mxr << ", b.mxr = " << b.mxr << "\n";
  }            
  if(a.tvm != b.tvm) {
    out << "a.tvm = " << a.tvm << ", b.tvm = " << b.tvm << "\n";
  }            
  if(a.tw != b.tw) {
    out << "a.tw = " << a.tw << ", b.tw = " << b.tw << "\n";
  }            
  if(a.tsr != b.tsr) {
    out << "a.tsr = " << a.tsr << ", b.tsr = " << b.tsr << "\n";
  }            
  if(a.uxl != b.uxl) {
    out << "a.uxl = " << a.uxl << ", b.uxl = " << b.uxl << "\n";
  }
  if(a.sxl != b.sxl) {
    out << "a.sxl = " << a.sxl << ", b.sxl = " << b.sxl << "\n";
  }
  if(a.sbe != b.sbe) {
    out << "a.sbe = " << a.sbe << ", b.sbe = " << b.sbe << "\n";
  }
  if(a.mbe != b.mbe) {
    out << "a.mbe = " << a.mbe << ", b.mbe = " << b.mbe << "\n";
  }
  if(a.sd != b.sd) {
    out << "a.sd = " << a.sd << ", b.sd = " << b.sd << "\n";
  }                    
}


struct sv39_t {
  uint64_t v : 1;
  uint64_t r : 1;
  uint64_t w : 1;
  uint64_t x : 1;
  uint64_t u : 1;
  uint64_t g : 1;
  uint64_t a : 1;
  uint64_t d : 1;
  uint64_t rsw : 2;
  uint64_t ppn : 44;
  uint64_t mbz : 7;
  uint64_t pbmt : 2;
  uint64_t n : 1;
};

union pte_t {
  sv39_t sv39;
  uint64_t r;
  pte_t(uint64_t x) : r(x) {}
};

void initState(state_t *s);
void runRiscv(state_t *s, uint64_t dumpIcnt);
void execRiscv(state_t *s);
void runInteractiveRiscv(state_t *s);

/* stolen from libgloss-htif : syscall.h */
#define SYS_getcwd 17
#define SYS_fcntl 25
#define SYS_mkdirat 34
#define SYS_unlinkat 35
#define SYS_linkat 37
#define SYS_renameat 38
#define SYS_ftruncate 46
#define SYS_faccessat 48
#define SYS_chdir 49
#define SYS_open   55
#define SYS_openat 56
#define SYS_close 57
#define SYS_lseek 62
#define SYS_read 63
#define SYS_write 64
#define SYS_pread 67
#define SYS_pwrite 68
#define SYS_stat 78
#define SYS_fstatat 79
#define SYS_fstat 80
#define SYS_exit 93
#define SYS_gettimeofday 94
#define SYS_times 95
#define SYS_lstat 1039
#define SYS_getmainvars 2011

std::ostream &operator<<(std::ostream &out, const state_t & s);

struct stat32_t {
  uint16_t st_dev;
  uint16_t st_ino;
  uint32_t st_mode;
  uint16_t st_nlink;
  uint16_t st_uid;
  uint16_t st_gid;
  uint16_t st_rdev;
  uint32_t st_size;
  uint32_t _st_atime;
  uint32_t st_spare1;
  uint32_t _st_mtime;
  uint32_t st_spare2;
  uint32_t _st_ctime;
  uint32_t st_spare3;
  uint32_t st_blksize;
  uint32_t st_blocks;
  uint32_t st_spare4[2];
};

void dump_calls();

#endif
