#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>
#include <bitset>

#include "interpret.hh"
#include "temu_code.hh"
#include "disassemble.hh"
#include "helper.hh"
#include "globals.hh"
#include "virtio.hh"
#include "uart.hh"
#include "trace.hh"
#include "branch_predictor.hh"

#include <stack>
static uint64_t curr_pc = 0;
static uint64_t last_tval = 0;
static std::stack<int64_t> calls;

static bool tracing_armed = false;
static uint64_t tracing_start_icnt = 0;

static uint64_t lookup_tlb(uint64_t va, bool &hit, bool &dirty) __attribute__((always_inline));

static inline uint64_t ror64(const uint64_t x, int amt) {
 return (x >> amt) | (x << (64 - amt));
}

static inline uint32_t ror32(const uint32_t x, int amt) {
 return (x >> amt) | (x << (32 - amt));
}

static inline uint64_t rol64(const uint64_t x, int amt) {
  return (x << amt) | (x >> (64 - amt));
}

static inline uint32_t rol32(const uint32_t x, int amt) {
  return (x << amt) | (x >> (32 - amt));
}

static inline uint64_t sext(int32_t r) {
  uint32_t x = *reinterpret_cast<uint32_t*>(&r);
  uint32_t s = (x>>31)&1;
  return (s ? 0xffffffff00000000UL : 0UL) | static_cast<uint64_t>(x);
}


void dump_calls() {
  int cnt = 0;
  while(!calls.empty() && (cnt < 5)) {
    int64_t ip = calls.top();
    std::cout << std::hex << ip << std::dec << "\n";
    calls.pop();
    cnt++;
  }
}

void initState(state_t *s) {
  memset(s, 0, sizeof(state_t));
  s->misa = 0x8000000000141101L;
  s->priv = priv_machine;
  s->mstatus = ((uint64_t)2 << MSTATUS_UXL_SHIFT) |((uint64_t)2 << MSTATUS_SXL_SHIFT);
}

uint64_t mtimecmp_cnt = 0;

//100 mhz and 1 IPC
//inst = (cycles/time) * (inst/cycle)
int64_t state_t::get_time() const {
  return icnt;
}

bool state_t::memory_map_check(uint64_t pa, bool store, int64_t x) {
  // if((pa <= (1UL<<21)) and store) {
  //   std::cout << std::hex << this->pc << " writing bbl code "
  // 	      << std::dec << "\n";
  //   dump_calls();
  //   exit(-1);
  // }
  
  //if(pa >= VIRTIO_BASE_ADDR and (pa < (VIRTIO_BASE_ADDR + VIRTIO_SIZE))) {
  //assert(vio);
  //return vio->handle(pa, store, x);
  //}
  if(pa >= PLIC_BASE_ADDR and (pa < (PLIC_BASE_ADDR + PLIC_SIZE))) {
    //printf(">> %s plic range at pc %lx, offset %ld bytes\n", store ? "write" : "read", pc, pa-PLIC_BASE_ADDR);
    //exit(-1);
    return true;
  }
  if(pa >= CLINT_BASE_ADDR and (pa < (CLINT_BASE_ADDR + CLINT_SIZE))) {
    //assert(store);
    switch(pa-CLINT_BASE_ADDR)
      {
      case 0x0:
	break;
      case 0x4000:	
	if(store) {
	  mtimecmp = x;
	  //std::cout << mtimecmp_cnt << "\n";
	  ++mtimecmp_cnt;
	  //dump_calls();
	  //printf(">> mtimecmp = %ld at icnt %ld\n", mtimecmp, icnt);
	  csr_t cc(mip);
	  cc.mie.mtie = 0;
	  mip = cc.raw;	  
	}
	break;
      case 0xbff8:
	//assert(not(store));
	break;
      default:
	break;
      }
    //printf(">> %s clint range at pc %lx, offset %ld bytes, st value %lx\n",
    //store ? "write" : "read", pc, pa-CLINT_BASE_ADDR, x);
    //exit(-1);
    return true;
  }  
  return false;
}

int8_t state_t::load8(uint64_t pa) {
  memory_map_check(pa);
  return *reinterpret_cast<int8_t*>(mem + pa);
}

int64_t state_t::load8u(uint64_t pa) {
  uint64_t z = 0;
  memory_map_check(pa);  
  *reinterpret_cast<uint64_t*>(&z) = *reinterpret_cast<uint8_t*>(mem + pa);
  return z;
}  

int16_t state_t::load16(uint64_t pa) {
  memory_map_check(pa);
  return *reinterpret_cast<int16_t*>(mem + pa);
}

int64_t state_t::load16u(uint64_t pa) {
  uint64_t z = 0;
  memory_map_check(pa);
  *reinterpret_cast<uint64_t*>(&z) = *reinterpret_cast<uint16_t*>(mem + pa);
  return z;
}  

int32_t state_t::load32(uint64_t pa) {
  memory_map_check(pa);  
  return *reinterpret_cast<int32_t*>(mem + pa);
}

int64_t state_t::load32u(uint64_t pa) {
  uint64_t z = 0;
  memory_map_check(pa);
  *reinterpret_cast<uint64_t*>(&z) = *reinterpret_cast<uint32_t*>(mem + pa);
  return z;
}  

int64_t state_t::load64(uint64_t pa) {
  memory_map_check(pa);
  return *reinterpret_cast<int64_t*>(mem + pa);
}

void state_t::store8(uint64_t pa,  int8_t x) {
  memory_map_check(pa,true,x);
  *reinterpret_cast<int8_t*>(mem + pa) = x;
}

void state_t::store16(uint64_t pa, int16_t x) {
  memory_map_check(pa,true,x);
  *reinterpret_cast<int16_t*>(mem + pa) = x;
}

void state_t::store32(uint64_t pa, int32_t x) {
  memory_map_check(pa,true,x);
  *reinterpret_cast<int32_t*>(mem + pa) = x;
}

void state_t::store64(uint64_t pa, int64_t x) {
  memory_map_check(pa,true,x);
  *reinterpret_cast<int64_t*>(mem + pa) = x;
}


struct tlb_entry {
  uint64_t pfn;
  uint64_t paddr;
  bool dirty;
  bool valid;
};



static const uint64_t TLB_SZ = 1UL<<5;

static std::array< tlb_entry, TLB_SZ> tlb;

static void clear_tlb() {
  for(size_t i = 0; i < TLB_SZ; i++) {
    tlb[i].valid = false;
  }
}

static void insert_tlb(uint64_t va, uint64_t paddr, int mask_bits, bool dirty) {
  uint64_t pfn = va >> 12;
  uint64_t h = pfn & (TLB_SZ-1);
  tlb[h].valid = true;
  tlb[h].pfn = pfn;
  tlb[h].dirty = dirty;
  tlb[h].paddr = paddr | mask_bits;
}

static uint64_t lookup_tlb(uint64_t va, bool &hit, bool &dirty) {
  ++globals::tlb_accesses;
  uint64_t pfn = va >> 12;
  uint64_t h = pfn & (TLB_SZ-1);
  if(not(tlb[h].valid)) {
    hit = false;
    return 0;
  }
  if(tlb[h].pfn != pfn) {
    hit = false;
    return 0;
  }
  int mask_bits = tlb[h].paddr & 4095;
  uint64_t ppn = tlb[h].paddr & (~4095UL);
  uint64_t m = ((1UL << mask_bits) - 1);
  uint64_t pa = (ppn & (~m)) | (va & m);
  dirty = tlb[h].dirty;
  hit = true;
  ++globals::tlb_hits;
  return pa;
}

static bool entered_user = false;


uint64_t state_t::translate(uint64_t ea, int &fault, int sz, bool store, bool fetch) {
  csr_t c(satp);
  pte_t r(0);

  uint64_t a = 0, u = 0;
  int mask_bits = -1;
  int pgsz = 0;
  uint64_t tlb_pa = 0;
  bool tlb_hit = false, tlb_dirty = false;
  
  fault = false;
  
  if(unpaged_mode()) {
    if(dcache and not(fetch)) {
      dcache->access(ea, icnt, pc);
    }
    if(globals::tracer and entered_user) {
      globals::tracer->add(ea, ea, fetch ? 1 : 2);      
    }
    return ea;
  }
  
  
  uint64_t t_pa = lookup_tlb(ea, tlb_hit, tlb_dirty);
  
  if((dtlb == nullptr) and tlb_hit and (tlb_dirty or not(store))) {
    if(store) assert(tlb_dirty);
    if(dcache and not(fetch)) {
      dcache->access(t_pa, icnt, pc);
    }
    return t_pa;
  }
  
  assert(c.satp.mode == 8);
  a = (c.satp.ppn * 4096) + (((ea >> 30) & 511)*8);
  u = *reinterpret_cast<uint64_t*>(mem + a);
  r.r = u;
  assert(r.sv39.n == false);  
  if((u&1) == 0) {
    //printf("page not present fault\n");
    fault = 1;
    return 2;
  }

  if(r.sv39.x || r.sv39.w || r.sv39.r) {
    mask_bits = 30;
    pgsz = 0;
    goto translation_complete;
  }
  a = (r.sv39.ppn * 4096) + (((ea >> 21) & 511)*8);
  u = *reinterpret_cast<uint64_t*>(mem + a);
  r.r = u;
  assert(r.sv39.n == false);
  if((u&1) == 0) {
    fault = 1;
    return 1;
  }
  if(r.sv39.x or r.sv39.w or r.sv39.r) {
    mask_bits = 21;
    pgsz = 1;
    goto translation_complete;
  }
  a = (r.sv39.ppn * 4096) + (((ea >> 12) & 511)*8);
  u = *reinterpret_cast<uint64_t*>(mem + a);
  r.r = u;    
  if((u&1) == 0) {
    //std::cout << "mapping does not exist for " << std::hex << ea << std::dec << " <<\n";
    //printf("page not present fault\n");    
    fault = 1;
    return 0;
  }

  if(not(r.sv39.x or r.sv39.w or r.sv39.r)) {
    std::cout << "huh no translation for " << std::hex << pc << std::dec << "\n";
    std::cout << "huh no translation for " << std::hex << ea << std::dec << "\n";
    std::cout << "u = " << std::hex << u << std::dec << "\n";
  }
  
  if(r.sv39.n) {
    assert( (r.sv39.ppn&15) == 8);
    mask_bits = 16;
    pgsz = 3;    
  }
  else {
    mask_bits = 12;
    pgsz = 2;    
  }

 translation_complete:
    
  //* permission checks */
  if(fetch && (r.sv39.x == 0)) {
    //std::cout << "not executable fetch\n";
    //printf("not executable fault\n");    
    fault = 1;
    return 0;
  }
  if(store && (r.sv39.w == 0)) {
    //printf("store to non-writeable page, pte addr %lx\n", a);
    fault = 1;
    return 0;
  }
  if(r.sv39.w && (r.sv39.r == 0)) {
    fault = 1;
    return 0;
  }
  if(not(store or fetch) && (r.sv39.r == 0)) {
    //std::cout << "read to not readable page\n";
    fault = 1;
    return 0;
  }
  if(r.sv39.u == 0 && (priv == priv_user)) {
    fault = 1;
    return 0;
  }
  if(r.sv39.u == 1 && fetch && (priv != priv_user)) {
    fault = 1;
    return 0;
  }
  assert(mask_bits != -1);

  
  if(r.sv39.a == 0) {
    r.sv39.a = 1;
    if(dcache) {
      dcache->access(a, icnt, ~0UL);
    }    
    store64(a, r.r);
  }
  if((r.sv39.d == 0) && store) {
    //printf("marking %lx dirty\n", ea & (~4095UL));
    //abort();
    r.sv39.d = 1;
    if(dcache) {
      dcache->access(a, icnt, ~0UL);
    }
    store64(a, r.r);    
  }
  
  if(fetch) {
    ++ipgszcnt[pgsz];
  }
  else {
    ++dpgszcnt[pgsz];
  }
  int64_t m = ((1L << mask_bits) - 1);
  int64_t pa = ((r.sv39.ppn * 4096) & (~m)) | (ea & m);

  if(dtlb and not(fetch)) {
    if(not(dtlb->access(ea))) {
      dtlb->add((ea & (~m)), m);
    }
  }
  
  if(globals::tracer and entered_user) {
    globals::tracer->add(ea, pa, fetch ? 1 : 2);
  }
  if(dcache and not(fetch)) {
    dcache->access(pa, icnt, pc);
  }

  insert_tlb(ea, r.sv39.ppn * 4096, mask_bits, r.sv39.d);
  return pa;
}

static void set_priv(state_t *s, int priv) {
  if (s->priv != priv) {
    clear_tlb();
    
    int mxl = 2;
    if (priv == priv_supervisor) {
      mxl = (s->mstatus >> MSTATUS_SXL_SHIFT) & 3;
      assert(mxl == 2);
    }
    else if (priv == priv_user) {
      mxl = (s->mstatus >> MSTATUS_UXL_SHIFT) & 3;
      assert(mxl == 2);
    }
    if(mxl != 2) {
      std::cout << "huh trying to change mxl to " << mxl << ", curr priv = " << s->priv << ", next priv = " << priv << "\n";
    }
    //assert(mxl == 2);
  }
  s->priv = static_cast<riscv_priv>(priv);
}

static void set_mstatus(state_t *s, int64_t v) {
  int64_t mask = MSTATUS_MASK;
  s->mstatus = (s->mstatus & ~mask) | (v & mask);
  csr_t c(v);
  //std::cout << c.mstatus << "\n";
  //std::cout << "write mstatus = " << std::hex << s->mstatus << std::dec << "\n";  
}
    


/* shitty dsheffie code */
std::ostream &operator<<(std::ostream &out, const state_t & s) {
  using namespace std;
  out << "PC : " << hex << s.last_pc << dec << "\n";
  for(int i = 0; i < 32; i++) {
    out << getGPRName(i) << " : 0x"
	<< hex << s.gpr[i] << dec
	<< "(" << s.gpr[i] << ")\n";
  }
  out << "icnt : " << s.icnt << "\n";
  return out;
}

static int64_t read_csr(int csr_id, state_t *s, bool &undef) {
  undef = false;
  switch(csr_id)
    {
    case 0x100: {
      return s->mstatus & 0x3000de133UL;
    }
    case 0x104:
      return s->mie & s->mideleg;
    case 0x105:
      return s->stvec;
    case 0x140:
      return s->sscratch;
    case 0x141:
      return s->sepc;
    case 0x142:
      return s->scause;
    case 0x143:
      return s->stval;
    case 0x144:
      return s->mip & s->mideleg;
    case 0x180:
      return s->satp;
    case 0x300:
      return s->mstatus;
    case 0x301: /* misa */
      return s->misa;
    case 0x302:
      return s->medeleg;
    case 0x303:
      return s->mideleg;
    case 0x304:
      return s->mie;
    case 0x305:
      return s->mtvec;
    case 0x340:
      return s->mscratch;
    case 0x341:
      return s->mepc;
    case 0x342:
      return s->mcause;
    case 0x343:
      return s->mtvec;
    case 0x344:
      return s->mip;
    case 0x3b0:
      return s->pmpaddr0;      
    case 0x3b1:
      return s->pmpaddr1;      
    case 0x3b2:
      return s->pmpaddr2;
    case 0x3b3:
      return s->pmpaddr3;      
    case 0xc00:
      return s->icnt;
    case 0xc01:
      return s->get_time();
    case 0xc02:
      return s->icnt;
    case 0xc03:
      return 0;
    case 0xf14:
      return s->mhartid;      
    default:
      if(not(globals::silent)) {
	std::cout << "rd csr id 0x"
		  << std::hex
		  << csr_id
		  <<" unimplemented, pc "
		  << s->pc
		  << std::dec
		  << "\n";
      }
      //undef = true;
      break;
    }
  return 0;
}

static int curr_pos = 0;
static char cons_buf[256] = {0};

static void write_csr(int csr_id, state_t *s, int64_t v, bool &undef) {
  undef = false;
  csr_t c(v);
  switch(csr_id)
    {
    case 0x100:
      //printf("writing sstatus at pc %lx\n", s->pc);
      s->mstatus = (v & 0x0000de133UL) | ((s->mstatus & (~0x000de133UL)));
      assert( ((s->mstatus >> MSTATUS_UXL_SHIFT) & 3) == 2);
      break;
    case 0x104:
      s->mie = (s->mie & ~(s->mideleg)) | (v & s->mideleg);      
      break;
    case 0x105:
      assert((v&3)  == 0);
      s->stvec = v;
      break;
    case 0x106:
      s->scounteren = v;
      break;
    case 0x140:
      s->sscratch = v;
      break;
    case 0x141:
      s->sepc = v;
      break;
    case 0x142:
      s->scause = v;
      break;
    case 0x143:
      s->stval = v;
      break;
    case 0x144: {
      s->mip = (s->mip & ~(s->mideleg)) | (v & s->mideleg);
      // if(s->mip != v) {
      // 	std::cout << "mip changes to " << std::hex << v
      // 		  << " at " << s->pc << std::dec
      // 		  << "\n";
      // 	csr_t c(v);
      // 	std::cout << c.mie << "\n";
      // }            
      break;
    }
    case 0x180:
      if(c.satp.mode == 8 &&
	 c.satp.asid == 0) {
	s->satp = v;
	//printf("tlb had %lu entries\n", tlb.size());
	clear_tlb();
	s->last_phys_pc = 0;	
      }
      break;
    case 0x300:
      set_mstatus(s,v);
      break;
    case 0x301:
      s->misa = v;
      break;
    case 0x302:
      s->medeleg = v;
      break;
    case 0x303:
      s->mideleg = v;
      break;
    case 0x304:
      //if(s->mie != v) {
      //printf("mie changes to %lx at %lx\n", v, s->pc);
      //csr_t c(v);
      //std::cout << c.mie << "\n";
      //}
      s->mie = v;
      break;
    case 0x305:
      s->mtvec = v;
      break;
    case 0x306:
      s->mcounteren = v;
      break;
    case 0x340:
      s->mscratch = v;
      break;
    case 0x341:
      s->mepc = v;
      break;
    case 0x344:
      s->mip = v;
      break;
    case 0x3a0:
      s->pmpcfg0 = v;
      break;
    case 0x3b0:
      //printf("pmpaddr0 set to %lx\n", v);
      s->pmpaddr0 = v;
      break;
    case 0x3b1:
      //printf("pmpaddr1 set to %lx\n", v);
      s->pmpaddr1 = v;
      break;
    case 0x3b2:
      //printf("pmpaddr2 set to %lx\n", v);
      s->pmpaddr2 = v;
      break;
    case 0x3b3:
      //printf("pmpaddr3 set to %lx\n", v);
      s->pmpaddr3 = v;
      break;

      /* linux hacking */
    case 0xc03: {
      char c = static_cast<char>(v&0xff);
      //if(globals::console_log != nullptr) {
      //(*globals::console_log) << std::string(c);
      //}
      cons_buf[curr_pos++] = c;
      curr_pos &= 255;
      if(c == '\n' or c == 0) {
	int m = strncmp("fpga_done", cons_buf, 9);
	if(m == 0) {
	  s->brk = 1;
	}
	m = strncmp("Kernel panic", cons_buf, 12);	
	if(m == 0) {
	  s->brk = 1;
	}
	memset(cons_buf,0,256);	
	curr_pos = 0;
      }
      std::cout << c;
      std::fflush(nullptr);
      break;
    }
    case 0xc04:
      s->brk = v&1;
      //if(s->brk) {
      //std::cout << "you have panicd linux, game over\n";
      //}
      break;
    default:
      if(not(globals::silent)) {
	printf("wr csr id 0x%x unimplemented\n", csr_id);
      }
      undef = true;
      break;
    }
}
template <bool useIcache, bool useDcache, bool useBBV, bool useMAV = false>
void execRiscv_(state_t *s) {
  uint8_t *mem = s->mem;
  int fetch_fault = 0, except_cause = -1;
  uint64_t tval = -1;
  uint64_t tohost = 0;
  uint64_t phys_pc = 0;
  uint32_t inst = 0, opcode = 0, rd = 0, lop = 0;
  int64_t irq = 0;
  riscv_t m(0);
  curr_pc = s->pc;

  if(useBBV) {
    s->bblog->nextSample(s->icnt);
    s->bbsz++;
  }
  if(useMAV) {
    s->mlog->nextSample(s->icnt);
  }
  
  //if( not(s->unpaged_mode()) ) {
  // globals::insn_histo[s->satp>>16][s->pc]++;
  //}
  
  int mxl = (s->mstatus >> MSTATUS_UXL_SHIFT) & 3;
  if(mxl == 0) {
    std::cout << "mxl = 0 at pc " << std::hex
      	      << s->pc << ", status "
	      << s->mstatus
	      << std::dec
	      << ", icnt " << s->icnt
	      << "\n";
    exit(-1);
  }

  if(s->priv == priv_user) {
    entered_user = true;
  }

  //if( (s->icnt % (1UL<<24)) == 0) {
  //printf("%lu hits, %lu accesses, hit ratio %g\n",
  //tlb_hits, tlb_accesses,
  //static_cast<double>(tlb_hits) /  tlb_accesses);
  //}
  //if(globals::tracer and s->priv == priv_user) {
  //s->brk = 1;
  //}
  

  csr_t c(s->mie);
  //if(c.mie.mtie) {
  //std::cout << "(s->get_time() = " << s->get_time() << "\n";
  //std::cout << "s->mtimecmp = "<< s->mtimecmp << "\n";
  //}
  
  if(s->get_time() >= s->mtimecmp) {
    csr_t cc(0);
    cc.mip.mtip = 1;
    s->mip |= cc.raw;
  }

  
  irq = take_interrupt(s);
  if(irq) {
    //printf(">> taking interrupt, irq %ld, time %ld, mtimecmp %ld <<\n",
    //irq, s->get_time(), s->mtimecmp);
    except_cause = CAUSE_INTERRUPT | irq;
    goto handle_exception;
  }

  /* if we're on the same page as the past instruction, reuse
   * the saved physical address */
  static const uint64_t lg_pg_sz = 12;
  static const uint64_t pg_mask = (1UL<<lg_pg_sz)-1;
  if((s->last_pc>>lg_pg_sz) == (s->pc>>lg_pg_sz) && s->last_phys_pc) {
    phys_pc = (s->pc & pg_mask) | (s->last_phys_pc & (~pg_mask));
  }
  else {
    phys_pc = s->translate(s->pc, fetch_fault, 4, false, true);
  }
  if(useIcache) {
    s->icache->access(phys_pc, s->icnt, s->pc);
  }

  s->last_phys_pc = phys_pc;
  
  /* lightly modified from tinyemu */
  
  if(fetch_fault) {
    except_cause = CAUSE_FETCH_PAGE_FAULT;
    tval = s->pc;

    //assert(tval != last_tval);

    last_tval = tval;
    //std::cout << csr_t(s->mstatus).mstatus << "\n";
    goto handle_exception;
  }
  assert(phys_pc < (1UL << 32));
  // assert(!fetch_fault);
  
  inst = s->load32(phys_pc);
  m.raw = inst;
  opcode = inst & 127;

#if 0
  if( (s->icnt % (1UL<<28)) == 0 ) {
    std::cout << std::hex << s->pc << std::dec
	      << " : " << getAsmString(inst, s->pc)
	      << " , raw " << std::hex
	      << inst
	      << std::dec
	      << " , icnt " << s->icnt
	      << " ,  priv " << s->priv
	      << "\n";
  }
#endif  
  
  if(s->priv == priv_machine) {
    tohost = *reinterpret_cast<uint64_t*>(mem + globals::tohost_addr);
    if(tohost) {
      if(globals::fullsim) {
	uint64_t dev = tohost >> 56;
	uint64_t cmd = (tohost >> 48) & 255;
	uint64_t payload = tohost & ((1UL<<48)-1);
	if(tohost == 1) { /* shutdown */
	  s->brk = 1;
	  return;
	}
	if(dev != 1) {
	  dump_calls();
	  assert(false);
	}
	
	if(cmd == 1) {
	  std::cout << static_cast<char>(payload & 0xff);
	  *reinterpret_cast<uint64_t*>(mem + globals::tohost_addr) = 0;
	  *reinterpret_cast<uint64_t*>(mem + globals::fromhost_addr) = (dev << 56) | (cmd << 48);
	}
	else {
	  assert(false);
	}
      }
      else {
	tohost &= ((1UL<<32)-1);      
	handle_syscall(s, tohost);
      }
    }
  }
  
  lop = (opcode & 3);
  rd = (inst>>7) & 31;

  assert(s->gpr[0] == 0);
  
  if(/*s->priv == 3 and */globals::log) {
    std::cout << std::hex << s->pc << std::dec
	      << " : " << getAsmString(inst, s->pc)
	      << " , raw " << std::hex
	      << inst
	      << std::dec
	      << " , icnt " << s->icnt
	      << " ,  priv " << s->priv
	      << "\n";
  }
  s->last_pc = s->pc;  


  if(lop != 3) { /* compressed instructions generate exceptions */
    int sel = ((inst>>13)&7);
    std::cout << "lop " << lop <<  ", sel " << sel << "\n";
    abort();
    bool ok = false;
    inst &= 65535;
    switch(lop)
      {
      case 0:
	switch(sel)
	  {
	  case 2: { /* c.lw */
	    int rd = ((inst>>2) & 7) + 8;
	    int rs1 = ((inst>>7) & 7) + 8;
	    /* data in rs2, rs1 used for addr */
	    //printf("rs1 = %d, rs2 = %d\n", rs1, rs2);
	    uint32_t uimm26 = (inst>>5)&3;
	    uint32_t uimm53 = (inst>>10)&7;
	    uint32_t disp = (uimm26>>1) | (uimm53 << 1) | ((uimm26&1) << 4);
	    //printf("disp = %u\n", disp);	    
	    disp <<= 2;
	    //printf("disp = %u\n", disp);
	    assert(disp == 0);
	    int64_t ea = disp + s->gpr[rs1];
	    int fault;
	    int64_t pa = s->translate(ea, fault, 4, false);	    
	    if(fault) {
	      except_cause = CAUSE_LOAD_PAGE_FAULT;
	      tval = ea;
	      goto handle_exception;
	    }
	    s->sext_xlen( s->load32(pa), rd);	    
	    ok = true;
	    s->pc += 2;
	    break;
	  }
	
	  case 6: { /* c.sw */
	    int rs2 = ((inst>>2) & 7) + 8;
	    int rs1 = ((inst>>7) & 7) + 8;
	    /* data in rs2, rs1 used for addr */
	    //printf("rs1 = %d, rs2 = %d\n", rs1, rs2);
	    uint32_t uimm26 = (inst>>5)&3;
	    uint32_t uimm53 = (inst>>10)&7;
	    uint32_t disp = (uimm26>>1) | (uimm53 << 1) | ((uimm26&1) << 4);
	    //printf("disp = %u\n", disp);	    
	    disp <<= 2;
	    //printf("disp = %u\n", disp);
	    assert(disp == 0);
	    int64_t ea = disp + s->gpr[rs1];
	    int fault;
	    int64_t pa = s->translate(ea, fault, 4, true);	    
	    if(fault) {
	      except_cause = CAUSE_STORE_PAGE_FAULT;
	      tval = ea;
	      goto handle_exception;
	    }
	    s->store32(pa, s->gpr[rs2]);
	    ok = true;
	    s->pc += 2;
	    break;
	  }
	  default:
	    break;
	  }
	break;
      case 1:
	switch(sel)
	  {
	  case 0: { /* c.addi */
	    int rd = (inst>>7)&31;
	    int simm32 = (((inst >> 12)&1)<<5) | ((inst >> 2) & 31);
	    simm32 = (simm32<<26) >> 26;
	    if(rd) {
	      s->sext_xlen(s->gpr[rd] + static_cast<int64_t>(simm32), rd);
	    }
	    ok = true;
	    s->pc += 2;
	    break;
	  }
	  case 1: { /* c.jal or c.addiw */
	    int rd = (inst>>7)&31;
	    if(rd==0) { /* c.jal */
	      
	    }
	    else { /* c.addiw */
	      int simm32 = (((inst >> 12)&1)<<5) | ((inst >> 2) & 31);
	      simm32 = (simm32<<26) >> 26;
	      int32_t r = simm32 + *reinterpret_cast<int32_t*>(&s->gpr[rd]);
	      s->sext_xlen(r, rd);
	      s->pc += 2;
	      ok = true;
	    }
	    break;
	  }
	  case 2: { /* c.li */
	    int rd = (inst>>7)&31;
	    int simm32 = (((inst >> 12)&1)<<5) | ((inst >> 2) & 31);
	    simm32 = (simm32<<26) >> 26;
	    if(rd) {
	      s->sext_xlen(static_cast<int64_t>(simm32), rd);
	    }
	    s->pc += 2;
	    ok = true;
	    break;
	  }
	  case 5: { /* c.j */
	    /* offset[11|4|9:8|10|6|7|3:1|5 */
	    rvc_t c(inst);
	    int32_t imm = 0;
	    imm = c.j.imm31 << 1;
	    imm |= c.j.imm4 << 4;
	    imm |= c.j.imm5 << 5;
	    imm |= c.j.imm6 << 6;
	    imm |= c.j.imm7 << 7;
	    imm |= c.j.imm98 << 8;
	    imm |= c.j.imm10 << 10;
	    imm |= c.j.imm11 << 11;
	    imm = (imm << 20) >> 20;
	    s->pc += imm;
	    ok = true;
	    break;
	  }
	  default:
	    break;
	  }
	break;
      case 2:
	switch(sel)
	  {
	  case 4: /* c.jr, c.mv, c.ebreak, c.jalr, c.add */{
	    int rs2 = (inst>>2) & 31;
	    int rd = (inst>>7) & 31;
	    if((inst>>12) & 1) {
	      if(rd!=0 && rs2!=0) { /* c.add */
		ok = true;
		s->sext_xlen(s->gpr[rd] + s->gpr[rs2], rd);		
		s->pc += 2;
	      }
	    }
	    else {
	      if(rd!=0 && rs2!=0) { /* reg-reg mv */
		ok = true;
		s->gpr[rd] = s->gpr[rs2];
		s->pc += 2;
	      }
	    }
	    break;
	  }
	  case 7: { /* SDSP */
	    int rs2 = (inst >> 2) & 31;
	    uint32_t ds = (inst >> 7) & 63;
	    uint32_t hi = ds & 7;
	    uint32_t lo = (ds>>3) & 7;
	    uint32_t d = ((hi<<3) | lo)<<3;
	    int64_t ea = d + s->gpr[2];
	    int fault;
	    int64_t pa = s->translate(ea, fault, 8, true);	    
	    if(fault) {
	      except_cause = CAUSE_STORE_PAGE_FAULT;
	      tval = ea;
	      goto handle_exception;
	    }
	    s->store64(pa, s->gpr[rs2]);
	    s->pc += 2;
	    ok = true;
	    break;
	  }
	  default:
	    break;
	  }
	break;
      default:
	break;
      }

    if(ok) {
      goto instruction_complete;
    }
    
    except_cause = CAUSE_ILLEGAL_INSTRUCTION;
    tval = s->pc;
    std::cout << std::hex << s->pc << std::dec
	      << " : " << getAsmString(inst, s->pc)
	      << " , raw " << std::hex
	      << inst
	      << std::dec
	      << " , icnt " << s->icnt
	      << " ,  priv " << s->priv
	      << "\n";
    
    goto handle_exception;
  }

  switch(opcode)
    {
    case 0x3: {
      if(m.l.rd != 0) {
	int32_t disp = m.l.imm11_0;
	if((inst>>31)&1) {
	  disp |= 0xfffff000;
	}
	int64_t disp64 = disp;
	int64_t ea = ((disp64 << 32) >> 32) + s->gpr[m.l.rs1];
	int sz = 1<<(m.s.sel & 3);
	
	int page_fault = 0;
	int64_t pa = s->translate(ea, page_fault, sz);
	  
	if(page_fault) {
	  except_cause = CAUSE_LOAD_PAGE_FAULT;
	  tval = ea;
	  //std::cout << "ea = " << std::hex << ea << std::dec << " causes ld pf\n";
	  //std::cout << "pa = " << std::hex << pa << std::dec << "\n";
	  //std::cout << "pc = " << std::hex << s->pc << std::dec << "\n";
	  goto handle_exception;
	}
	if(useMAV) {
	  s->mlog->addSample((pa >> 12) << 12, 1);
	}
	
	int p = (pa >> 12) & 3;
	int v = (ea >> 12) & 3;
	s->va_track_pa += (v==p);
	s->loads++;

	
	
	switch(m.s.sel)
	  {
	  case 0x0: /* lb */
	    s->gpr[m.l.rd] = s->load8(pa);
	    break;
	  case 0x1: /* lh */
	    s->gpr[m.l.rd] = s->load16(pa);
	    break;
	  case 0x2: /* lw */
	    s->sext_xlen( s->load32(pa), m.l.rd);
	    break;
	  case 0x3: /* ld */
	    s->gpr[m.l.rd] = s->load64(pa);
	    break;
	  case 0x4:/* lbu */
	    s->gpr[m.l.rd] = s->load8u(pa);
	    break;
	  case 0x5: /* lhu */
	    s->gpr[m.l.rd] = s->load16u(pa);	    
	    break;
	  case 0x6: /* lwu */
	    s->gpr[m.l.rd] = s->load32u(pa);	    	    
	    break;
	  default:
	    goto report_unimplemented;
	    assert(0);
	  }

	s->pc += 4;
	break;
      }
    }
    case 0xb: {
      /* davids hacks */
      if(m.r.rd) {
	switch(m.r.special) {
	case 0x2: {/* lwx */
	  int64_t ea = s->gpr[m.r.rs1] + s->gpr[m.r.rs2];
	  int page_fault = 0;
	  int64_t pa = s->translate(ea, page_fault, 4);
	  if(page_fault) {
	    except_cause = CAUSE_LOAD_PAGE_FAULT;
	    tval = ea;
	    //std::cout << "ea = " << std::hex << ea << std::dec << " causes ld pf\n";
	    //std::cout << "pa = " << std::hex << pa << std::dec << "\n";
	    //std::cout << "pc = " << std::hex << s->pc << std::dec << "\n";
	    goto handle_exception;
	  }
	  s->sext_xlen( s->load32(pa), m.r.rd);	  
	  break;
	}
	default:
	  assert(false);
	}
      }
      s->pc += 4;      
      break;
    }
    case 0xf:/* fence - there's a bunch of 'em */
      s->pc += 4;
      break;
    case 0x13: {
      int32_t simm32 = (inst >> 20);

      simm32 |= ((inst>>31)&1) ? 0xfffff000 : 0x0;
      int64_t simm64 = simm32;
      //sign extend!
      simm64 = (simm64 <<32) >> 32;
      uint32_t subop =(inst>>12)&7;
      uint32_t shamt = (inst>>20) & (s->xlen()-1);
      uint32_t sel =  (inst >> 26) & 127;	    
      if(rd != 0) {
	switch(m.i.sel)
	  {
	  case 0: {/* addi */
	    s->sext_xlen(s->gpr[m.i.rs1] + simm64, rd);
	    break;
	  }
	  case 1: /* slli */
	    if((inst>>20) == 0x600) { /* clz */
	      uint64_t u = *reinterpret_cast<uint64_t*>(&s->gpr[m.i.rs1]);
	      s->gpr[m.i.rd] = u==0 ? ~0UL : __builtin_clzl(u);
	    }
	    else if((inst>>20) == 0x601) { /* ctz */
	      uint64_t u = *reinterpret_cast<uint64_t*>(&s->gpr[m.i.rs1]);
	      s->gpr[m.i.rd] = u==0 ? ~0UL : __builtin_ctzl(u);
	    }
	    else if((inst>>20) == 0x602) { /* cpop */
	      uint64_t u = *reinterpret_cast<uint64_t*>(&s->gpr[m.i.rs1]);
	      s->gpr[m.i.rd] =  __builtin_popcountl(u);
	    }	 
	    else if( (inst>>20) == 0x604) { /* sext.b */
	      int64_t z8 = s->gpr[m.i.rs1];
	      int64_t z64 = (z8 << 56) >> 56;
	      s->sext_xlen(z64, rd);
	    }
	    else if( (inst>>20) == 0x605) { /* sext.h */
	      int64_t z16 = s->gpr[m.i.rs1];
	      int64_t z64 = (z16 << 48) >> 48;
	      s->sext_xlen(z64, rd);
	    }
	    else {
	      if(sel == 0) {
		s->sext_xlen((*reinterpret_cast<uint64_t*>(&s->gpr[m.i.rs1])) << shamt, rd);
	      }
	      else {
		std::cout << "WTF at PC " << FMT_HEX(s->pc) << "\n";
		assert(0);
	      }
	    }
	    break;
	  case 2: /* slti */
	    s->gpr[rd] = (s->gpr[m.i.rs1] < simm64);
	    break;
	  case 3: { /* sltiu */
	    uint64_t uimm64 = static_cast<uint64_t>(simm64);
	    uint64_t u_rs1 = *reinterpret_cast<uint64_t*>(&s->gpr[m.i.rs1]);
	    s->gpr[rd] = (u_rs1 < uimm64);
	    break;
	  }
	  case 4: /* xori */
	    s->sext_xlen((s->gpr[m.i.rs1] ^ simm64), rd);
	    break;
	  case 5: { /* srli & srai */
	    if(sel == 0) { /* srli */
	      s->gpr[rd] = (*reinterpret_cast<uint64_t*>(&s->gpr[m.i.rs1]) >> shamt);
	    }
	    else if(sel == 0xa) { /* orcb */
	      uint64_t u = s->get_reg_u64(m.i.rs1), x = 0;
	      x = (u & 255) == 0 ? 0 : 255UL;
	      x |= ((u>>8) & 255) == 0 ? 0 : 255UL<<8;
	      x |= ((u>>16) & 255) == 0 ? 0 : 255UL<<16;
	      x |= ((u>>24) & 255) == 0 ? 0 : 255UL<<24;	      	      	      
	      x |= ((u>>32) & 255) == 0 ? 0 : 255UL<<32;
	      x |= ((u>>40) & 255) == 0 ? 0 : 255UL<<40;
	      x |= ((u>>48) & 255) == 0 ? 0 : 255UL<<48;
	      x |= ((u>>56) & 255) == 0 ? 0 : 255UL<<56;
	      s->gpr[rd] = x;
	    }	    
	    else if(sel == 16) { /* srai */
	      s->gpr[rd] = s->gpr[m.i.rs1] >> shamt;
	    }
	    else if(sel == 0x18) { /* rori */
	      s->gpr[rd] = ror64(s->get_reg_u64(m.i.rs1), shamt);
	    }
	    else if(sel == 0x1a) { /* rev8 */
	      s->gpr[rd] = __builtin_bswap64(s->gpr[m.i.rs1]);
	    }
	    else {
	      assert(0);
	    }
	    break;
	  }
	  case 6: /* ori */
	    s->sext_xlen((s->gpr[m.i.rs1] | simm64), rd);	    
	    break;
	  case 7: /* andi */
	    s->sext_xlen((s->gpr[m.i.rs1] & simm64), rd);
	    break;
	    
	  default:
	    std::cout << "implement case " << subop << "\n";
	    goto report_unimplemented;
	    assert(false);
	  }
      }
      s->pc += 4;
      break;
    }
    case 0x1b: {      
      if(rd != 0) {
	int32_t simm32 = (inst >> 20);
	simm32 |= ((inst>>31)&1) ? 0xfffff000 : 0x0;
	uint32_t shamt = (inst>>20) & 31;
	switch(m.i.sel)
	  {
	  case 0: {
	    int32_t r = simm32 + *reinterpret_cast<int32_t*>(&s->gpr[m.i.rs1]);
	    s->gpr[m.i.rd] = sext(r);
	    break;
	  }
	  case 1: {
	    uint32_t sel = ((inst>>26)&63);
	    if(sel == 0) {  /*SLLIW*/
	      int32_t r = *reinterpret_cast<int32_t*>(&s->gpr[m.i.rs1]) << shamt;
	      s->gpr[m.i.rd] = sext(r);
	    }
	    else if(sel == 2) {  /*SLLIUW*/
	      uint64_t c = static_cast<uint64_t>(s->get_reg_u32(m.i.rs1)) << shamt;
	      s->sext_xlen(c, m.r.rd);	      
	    }
	    else if(sel == 0x18) { /* clzw */
	      uint32_t u = *reinterpret_cast<uint32_t*>(&s->gpr[m.i.rs1]);	      
	      if(u == 0) {
		s->gpr[m.i.rd] = ~0UL;
	      }
	      else {
		switch( (inst>>20)&31 )
		  {
		  case 0: /* clzw */
		    s->gpr[m.i.rd] = __builtin_clz(u);
		    break;
		  case 1: /* ctzw */
		    s->gpr[m.i.rd] = __builtin_ctz(u);		    
		    break;
		  case 2: /* cpopw */
		    s->gpr[m.i.rd] = __builtin_popcount(u);		    
		    break;		    
		  default:
		    assert(0);
		  }
	      }
	    }
	    else {
	      goto report_unimplemented;
	    }
	    break;
	  }
	  case 5: { 
	    uint32_t sel =  (inst >> 25) & 127;
	    if(sel == 0) { /* SRLIW */
	      uint32_t r = *reinterpret_cast<uint32_t*>(&s->gpr[m.i.rs1]) >> shamt;
	      int32_t rr =  *reinterpret_cast<int32_t*>(&r);
	      s->gpr[m.i.rd] = sext(rr);
	    }
	    else if(sel == 32){ /* SRAIW */
	      int32_t r = *reinterpret_cast<int32_t*>(&s->gpr[m.i.rs1]) >> shamt;
	      s->gpr[m.i.rd] = sext(r);
	    }
	    else if(sel == 0x30) { /* roriw */
	      uint32_t x = *reinterpret_cast<uint32_t*>(&s->gpr[m.i.rs1]);
	      uint32_t xx = ror32(x, shamt);
	      s->gpr[m.i.rd] = sext(xx);
	    }
	    else {
	      assert(0);
	    }
	    break;	    
	  }
	  default:
	    std::cout << m.i.sel << "\n";
	    goto report_unimplemented;
	    break;
	  }
      }
      s->pc += 4;
      break;
    }
    case 0x2f: {
      int page_fault = 0;
      uint64_t pa = 0;
      if(m.a.sel == 2) {
	pa = s->translate(s->gpr[m.a.rs1], page_fault, 4, true);
	switch(m.a.hiop)
	  {
	  case 0x0: {/* amoadd.w */
	    pa = s->translate(s->gpr[m.a.rs1], page_fault, 4, true);
	    if(page_fault) {
	      except_cause = CAUSE_STORE_PAGE_FAULT;
	      tval = s->gpr[m.a.rs1];
	      goto handle_exception;
	    }
	    int32_t x = s->load32(pa);
	    s->store32(pa, s->gpr[m.a.rs2] + x);
	    if(m.a.rd != 0) { 
	      s->sext_xlen(x, m.a.rd);
	    }
	    break;
	  }
	  case 0x1: {/* amoswap.w */
	    pa = s->translate(s->gpr[m.a.rs1], page_fault, 4,  true);
	    if(page_fault) {
	      except_cause = CAUSE_STORE_PAGE_FAULT;
	      tval = s->gpr[m.a.rs1];
	      goto handle_exception;
	    }	    
	    int32_t x = s->load32(pa);
	    s->store32(pa, s->gpr[m.a.rs2]);
	    if(m.a.rd != 0) {
	      s->sext_xlen(x, m.a.rd);
	    }
	    break;
	  }
	  case 0x2: { /* lr.w */
	    pa = s->translate(s->gpr[m.a.rs1], page_fault, 4);
	    if(page_fault) {
	      except_cause = CAUSE_LOAD_PAGE_FAULT;
	      tval = s->gpr[m.a.rs1];
	      s->llsc_addr = 0;
	      goto handle_exception;
	    }
	    s->llsc_addr  = pa & (~63UL);	    
	    if(m.a.rd != 0) {
	      s->sext_xlen(s->load32(pa), m.a.rd);
	    }
	    break;
	  }
	  case 0x3 : { /* sc.w */
	    pa = s->translate(s->gpr[m.a.rs1], page_fault, 4, true);
	    if(page_fault) {
	      except_cause = CAUSE_STORE_PAGE_FAULT;
	      tval = s->gpr[m.a.rs1];
	      goto handle_exception;
	    }
	    bool succ = s->llsc_addr == (pa & (~63UL));
	    if(succ) {
	      s->store32(pa, s->gpr[m.a.rs2]);	      
	    }
	    if(m.a.rd != 0) {
	      s->gpr[m.a.rd] = succ ? 0 : 1;
	    }
	    break;
	  }
	  case 0x8: {/* amoor.w */
	    pa = s->translate(s->gpr[m.a.rs1], page_fault, 4, true);
	    if(page_fault) {
	      except_cause = CAUSE_STORE_PAGE_FAULT;
	      tval = s->gpr[m.a.rs1];
	      goto handle_exception;
	    }	    
	    
	    int64_t x = s->load32(pa);
	    s->store32(pa, s->gpr[m.a.rs2] | x);
	    if(m.a.rd != 0) {
	      s->sext_xlen(x, m.a.rd);
	    }
	    break;
	  }
	  case 0xc: {/* amoand.w */
	    pa = s->translate(s->gpr[m.a.rs1], page_fault, 4, true);
	    if(page_fault) {
	      except_cause = CAUSE_STORE_PAGE_FAULT;
	      tval = s->gpr[m.a.rs1];
	      goto handle_exception;
	    }	    
	    int32_t x = s->load32(pa);
	    s->store32(pa, s->gpr[m.a.rs2] & x);
	    if(m.a.rd != 0) { 
	      s->sext_xlen(x, m.a.rd);
	    }
	    break;
	  }
	  case 0x1c: {/* amomaxu.w */
	    pa = s->translate(s->gpr[m.a.rs1], page_fault, 4, true);
	    if(page_fault) {
	      except_cause = CAUSE_STORE_PAGE_FAULT;
	      tval = s->gpr[m.a.rs1];
	      goto handle_exception;
	    }	    
	    uint32_t u0 = s->load32(pa);
	    uint32_t u1 = *reinterpret_cast<uint32_t*>(&s->gpr[m.a.rs2]);
	    s->store32(pa, std::max(u0,u1));
	    if(m.a.rd != 0) {
	      s->sext_xlen(u0, m.a.rd);
	    }
	    break;
	  }
	  default:
	    std::cout << "m.a.hiop " << std::hex << m.a.hiop << std::dec << "\n";
	    goto report_unimplemented;
	  }
      }
      else if(m.a.sel == 3) {
	pa = s->translate(s->gpr[m.a.rs1], page_fault, 8, false);
	switch(m.a.hiop)
	  {
	  case 0x0: {/* amoadd.d */
	    pa = s->translate(s->gpr[m.a.rs1], page_fault, 8, true);
	    if(page_fault) {
	      except_cause = CAUSE_STORE_PAGE_FAULT;
	      tval = s->gpr[m.a.rs1];
	      goto handle_exception;
	    }	    	    	    
	    int64_t x = s->load64(pa);
	    s->store64(pa, s->gpr[m.a.rs2] +x);
	    if(m.a.rd != 0) {
	      s->gpr[m.a.rd] = x;
	    }
	    break;
	  }
	  case 0x1: {/* amoswap.d */
	    pa = s->translate(s->gpr[m.a.rs1], page_fault, 8, true);
	    if(page_fault) {
	      except_cause = CAUSE_STORE_PAGE_FAULT;
	      tval = s->gpr[m.a.rs1];
	      goto handle_exception;
	    }	    	    	    
	    int64_t x = s->load64(pa);
	    s->store64(pa, s->gpr[m.a.rs2]);
	    if(m.a.rd != 0) {
	      s->gpr[m.a.rd] = x;
	    }
	    break;
	  }
	  case 0x2: { /* lr.d */
	    pa = s->translate(s->gpr[m.a.rs1], page_fault, 8);
	    if(page_fault) {
	      except_cause = CAUSE_LOAD_PAGE_FAULT;
	      tval = s->gpr[m.a.rs1];
	      s->llsc_addr = 0;
	      goto handle_exception;
	    }
	    s->llsc_addr  = pa & (~63UL);	    
	    if(m.a.rd != 0) {
	      s->gpr[m.a.rd] = s->load64(pa);
	    }
	    break;
	  }
	  case 0x3 : { /* sc.d */
	    pa = s->translate(s->gpr[m.a.rs1], page_fault, 8,  true);
	    if(page_fault) {
	      except_cause = CAUSE_STORE_PAGE_FAULT;
	      tval = s->gpr[m.a.rs1];
	      goto handle_exception;
	    }
	    bool succ = s->llsc_addr == (pa & (~63UL));
	    if(succ) {
	      s->store64( pa, s->gpr[m.a.rs2]);
	    }
	    if(m.a.rd != 0) {
	      s->gpr[m.a.rd] = succ ? 0 : 1;
	    }
	    break;
	  }
	  case 0x4: {/* amoxor.d */
	    pa = s->translate(s->gpr[m.a.rs1], page_fault, 8, true);
	    if(page_fault) {
	      except_cause = CAUSE_STORE_PAGE_FAULT;
	      tval = s->gpr[m.a.rs1];
	      goto handle_exception;
	    }	    	    	    
	    int64_t x = s->load64(pa);
	    s->store64(pa, s->gpr[m.a.rs2] ^ x);	    
	    if(m.a.rd != 0) {
	      s->gpr[m.a.rd] = x;
	    }
	    break;
	  }	    
	    
	  case 0x8: {/* amoor.d */
	    pa = s->translate(s->gpr[m.a.rs1], page_fault, 8, true);
	    if(page_fault) {
	      except_cause = CAUSE_STORE_PAGE_FAULT;
	      tval = s->gpr[m.a.rs1];
	      goto handle_exception;
	    }	    	    	    
	    int64_t x = s->load64(pa);
	    s->store64(pa, s->gpr[m.a.rs2] | x);	    
	    if(m.a.rd != 0) {
	      s->gpr[m.a.rd] = x;
	    }
	    break;
	  }
	  case 0xc: {/* amoand.d */
	    pa = s->translate(s->gpr[m.a.rs1], page_fault, 8, true);
	    if(page_fault) {
	      except_cause = CAUSE_STORE_PAGE_FAULT;
	      tval = s->gpr[m.a.rs1];
	      goto handle_exception;
	    }	    	    	    
	    
	    int64_t x = s->load64(pa);
	    s->store64(pa, s->gpr[m.a.rs2] & x);
	    if(m.a.rd != 0) {
	      s->gpr[m.a.rd] = x;
	    }
	    break;
	  }
	  case 0x1c: {/* amoand.d */
	    pa = s->translate(s->gpr[m.a.rs1], page_fault, 8, true);
	    if(page_fault) {
	      except_cause = CAUSE_STORE_PAGE_FAULT;
	      tval = s->gpr[m.a.rs1];
	      goto handle_exception;
	    }	    	    	    
	    uint64_t x = s->load64(pa);
	    s->store64(pa, std::max(*reinterpret_cast<uint64_t*>(&s->gpr[m.a.rs2]), x));
	    if(m.a.rd != 0) {
	      s->gpr[m.a.rd] = x;
	    }
	    break;
	  }	    
	  default:
	    std::cout << "m.a.hiop " << std::hex << m.a.hiop << std::dec <<  "\n";
	    goto report_unimplemented;
	  }
      }
      else {	
	assert(false);
      }
      s->pc += 4;
      break;
    }
    case 0x3b: {
      if(m.r.rd != 0) {
	int32_t a = s->get_reg_i32(m.r.rs1), b = s->get_reg_i32(m.r.rs2);
	
	if((m.r.sel == 0) & (m.r.special == 0)) { /* addw */
	  int32_t c = a+b;
	  s->sext_xlen(c, m.r.rd);	 
	}
	else if((m.r.sel == 0) & (m.r.special == 1)) { /* mulw */
	  int32_t c = a*b;
	  s->sext_xlen(c, m.r.rd);
	}
	else if((m.r.sel == 0) & (m.r.special == 4)) { /* add.uw */
	  uint64_t c = static_cast<uint64_t>(s->get_reg_u32(m.r.rs1)) +
	    *reinterpret_cast<uint64_t*>(&s->gpr[m.r.rs2]);
	  s->sext_xlen(c, m.r.rd);
	}
	else if((m.r.sel == 0) & (m.r.special == 32)) { /* subw */
	  int32_t c = a-b;
	  s->sext_xlen(c, m.r.rd);
	} 
	else if((m.r.sel == 1) & (m.r.special == 0)) { /* sllw */
	  int32_t c = a << (s->gpr[m.r.rs2]&31);
	  s->sext_xlen(c, m.r.rd);
	}
	else if((m.r.sel == 1) & (m.r.special == 0x30)) { /* rol.w */
	  uint32_t x = *reinterpret_cast<uint32_t*>(&s->gpr[m.r.rs1]);
	  int amt = (s->gpr[m.r.rs2]&31);
	  uint32_t xx = rol32(x, amt);
	  //printf("rol32 : x = %x, amt = %d, xx = %x\n", x, amt, xx);
	  s->gpr[m.i.rd] = sext(xx);
	}		
	else if((m.r.sel == 4) & (m.r.special == 1)) { /* divw */
	  int32_t c = ~0;
	  if(b != 0) {
	    c = a/b;
	  }
	  s->sext_xlen(c, m.r.rd);
	}
	else if((m.r.sel == 5) & (m.r.special == 0)) { /* srlw */
	  uint32_t c = s->get_reg_u32(m.r.rs1) >> (s->gpr[m.r.rs2]&31);
	  int32_t rr =  *reinterpret_cast<int32_t*>(&c);	  
	  s->sext_xlen(rr, m.r.rd);	  
	}
	else if((m.r.sel == 5) & (m.r.special == 1)) { /* divuw */
	  uint32_t aa = s->get_reg_u32(m.r.rs1);
	  uint32_t bb = s->get_reg_u32(m.r.rs2);
	  uint32_t c = ~0U;
	  if(bb != 0) {
	    c = aa/bb;
	  }
	  int32_t rr =  *reinterpret_cast<int32_t*>(&c);
	  s->sext_xlen(rr, m.r.rd);
	}
	else if((m.r.sel == 2) & (m.r.special == 16)) { /* sh1add.uw */
	  uint64_t bb = static_cast<uint64_t>(s->get_reg_u32(m.r.rs1)) << 1;
	  uint64_t c = bb + *reinterpret_cast<uint64_t*>(&s->gpr[m.r.rs2]);
	  s->sext_xlen(c, m.r.rd);
	}
	else if((m.r.sel == 4) & (m.r.special == 4)) { /* zext.h */
	  int64_t z64 = s->get_reg_u64(m.r.rs1) & ((1L<<16)-1);
	  s->sext_xlen(z64, m.r.rd);
	}
	else if((m.r.sel == 4) & (m.r.special == 16)) { /* sh2add.uw */
	  uint64_t bb = static_cast<uint64_t>(s->get_reg_u32(m.r.rs1)) << 2;
	  uint64_t c = bb + *reinterpret_cast<uint64_t*>(&s->gpr[m.r.rs2]);
	  s->sext_xlen(c, m.r.rd);
	}
	else if((m.r.sel == 5) & (m.r.special == 0x30)) { /* ror.w */
	  uint32_t x = *reinterpret_cast<uint32_t*>(&s->gpr[m.r.rs1]);
	  int amt = (s->gpr[m.r.rs2]&31);
	  uint32_t xx = ror32(x, amt);
	  s->gpr[m.i.rd] = sext(xx);
	}
	else if((m.r.sel == 6) & (m.r.special == 16)) { /* sh3add.uw */
	  uint64_t bb = static_cast<uint64_t>(s->get_reg_u32(m.r.rs1)) << 3;
	  uint64_t c = bb + *reinterpret_cast<uint64_t*>(&s->gpr[m.r.rs2]);
	  s->sext_xlen(c, m.r.rd);
	}
	else if((m.r.sel == 5) & (m.r.special == 32)) { /* sraw */
	  int32_t c = a >> (s->gpr[m.r.rs2]&31);
	  s->sext_xlen(c, m.r.rd);	  
	}
	else if((m.r.sel == 6) & (m.r.special == 1)) { /* remw */
	  int32_t c = ~0;
	  if (b != 0) {
	    c= a % b;
	  }
	  s->sext_xlen(c, m.r.rd);
	}
	else if((m.r.sel == 7) & (m.r.special == 1)) { /* remuw */
	  uint32_t aa = s->get_reg_u32(m.r.rs1);
	  uint32_t bb = s->get_reg_u32(m.r.rs2);
	  uint32_t c = ~0;
	  if(bb != 0) {
	    c = aa%bb;
	  }
	  int32_t rr =  *reinterpret_cast<int32_t*>(&c);
	  s->sext_xlen(rr, m.r.rd);
	}
	else {
	  std::cout << "special = " << m.r.special << "\n";
	  std::cout << "sel = " << m.r.sel << "\n";
	  assert(false);
	  //disassemble(std::cout, inst, s->pc);
	  //exit(-1);
	  goto report_unimplemented;
	}
	
      }
      s->pc += 4;
      break;
    }      
    case 0x23: {
      int32_t disp = m.s.imm4_0 | (m.s.imm11_5 << 5);
      disp |= ((inst>>31)&1) ? 0xfffff000 : 0x0;
      int64_t disp64 = disp;
      int64_t ea = ((disp64 << 32) >> 32) + s->gpr[m.s.rs1];
      int fault;

      int sz = 1<<(m.s.sel);
      int64_t pa = s->translate(ea, fault, sz, true);
      
      if(fault) {
	except_cause = CAUSE_STORE_PAGE_FAULT;
	tval = ea;
	goto handle_exception;
      }
      if(useMAV) {
	s->mlog->addSample((pa >> 12) << 12, 1);
      }
      
      switch(m.s.sel)
	{
	case 0x0: /* sb */
	  s->store8(pa, s->gpr[m.s.rs2]);
	  break;
	case 0x1: /* sh */
	  s->store16(pa, s->gpr[m.s.rs2]);
	  break;
	case 0x2: /* sw */
	  s->store32(pa, s->gpr[m.s.rs2]);
	  break;
	case 0x3: /* sd */
	  s->store64(pa, s->gpr[m.s.rs2]);
	  break;
	default:
	  assert(0);
	}
      s->pc += 4;
      break;
    }

      
      //imm[31:12] rd 011 0111 LUI
    case 0x37:
      if(rd != 0) {
	int32_t imm32 = inst & 0xfffff000;
	s->sext_xlen(imm32, rd);
      }
      s->pc += 4;
      break;
      //imm[31:12] rd 0010111 AUIPC
    case 0x17: /* is this sign extended */
      if(rd != 0) {
	int64_t imm = inst & (~4095);
	imm = (imm << 32) >> 32;
	int64_t y = s->pc + imm;
	s->sext_xlen(y, rd);
      }
      s->pc += 4;
      break;
      
      //imm[11:0] rs1 000 rd 1100111 JALR
    case 0x67: {
      int32_t tgt = m.jj.imm11_0;
      tgt |= ((inst>>31)&1) ? 0xfffff000 : 0x0;
      int64_t tgt64 = tgt;
      tgt64 = (tgt64<<32)>>32;
      tgt64 += s->gpr[m.jj.rs1];
      tgt64 &= ~(1UL);
      uint64_t bpu_idx = 0;
      
      branch_predictor::br_type ty = branch_predictor::br_type::call;
	
      if(globals::bpred) {
	globals::bpred->predict(s->pc, bpu_idx);
      }
      
      if(m.jj.rd != 0) {
	s->gpr[m.jj.rd] = s->pc + 4;
      }
      //std::cout << "target = " << std::hex << tgt64 << std::dec << "\n";
      s->last_call = s->pc;

      
      bool rs1_is_link = m.jj.rs1==1 or m.jj.rs1==5;
      bool rd_is_link = m.jj.rd==1 or m.jj.rd==5;
      
      if((m.jj.rd == 0) and rs1_is_link) {
	if(globals::branch_tracer) {
	  globals::branch_tracer->add(s->pc, tgt64, true, false, false, true);
	}	
	
	if(!calls.empty())
	  calls.pop();
      }
      
      if(rd_is_link) {
	calls.push(s->pc);
	if(globals::branch_tracer) {
	  globals::branch_tracer->add(s->pc, tgt64, true, false, true, false);
	}	
	
      }
      if(globals::bpred) {
	globals::bpred->update(s->pc, bpu_idx, true, true, ty);
      }      
      if(useBBV) {
	int ff = -1;
	s->bblog->addSample(s->translate(s->pc, ff, 4, false, true), s->bbsz);
	s->bbsz = 0;
      }
      s->pc = tgt64;
      break;
    }
      
      //imm[20|10:1|11|19:12] rd 1101111 JAL
    case 0x6f: {
      int32_t jaddr32 =
	(m.j.imm10_1 << 1)   |
	(m.j.imm11 << 11)    |
	(m.j.imm19_12 << 12) |
	(m.j.imm20 << 20);
      jaddr32 |= ((inst>>31)&1) ? 0xffe00000 : 0x0;
      int64_t jaddr = jaddr32;
      jaddr = (jaddr << 32) >> 32;
      if(rd != 0) {
	s->gpr[rd] = s->pc + 4;
      }
      uint64_t bpu_idx = 0;
      if(globals::bpred) {
	globals::bpred->predict(s->pc, bpu_idx);
      }      
      s->last_call = s->pc;
      bool rd_is_link = rd==1 or rd==5;      
      if(rd_is_link) {
	calls.push(s->pc);
	if(globals::branch_tracer) {
	  globals::branch_tracer->add(s->pc, s->pc + jaddr, true, true, false, false);
	}		
      }
      if(globals::bpred) {
	globals::bpred->update(s->pc, bpu_idx, true, true,
			       rd==0 ? branch_predictor::br_type::direct_br : branch_predictor::br_type::call);
      }      
      if(useBBV) {
	int ff = -1;
	s->bblog->addSample(s->translate(s->pc, ff, 4, false, true), s->bbsz);
	s->bbsz = 0;	
      }
      s->pc += jaddr;      
      break;
    }
    case 0x33: {      
      if(m.r.rd != 0) {
	uint64_t u_rs1 = *reinterpret_cast<uint64_t*>(&s->gpr[m.r.rs1]);
	uint64_t u_rs2 = *reinterpret_cast<uint64_t*>(&s->gpr[m.r.rs2]);
	switch(m.r.sel)
	  {
	  case 0x0: /* add & sub */
	    switch(m.r.special)
	      {
	      case 0x0: /* add */
		s->sext_xlen(s->gpr[m.r.rs1] + s->gpr[m.r.rs2], m.r.rd);
		break;
	      case 0x1: /* mul */
		s->gpr[m.r.rd] = s->gpr[m.r.rs1] * s->gpr[m.r.rs2];
		break;
	      case 0x4:
		assert(false);
	      case 0x20: /* sub */
		s->sext_xlen(s->gpr[m.r.rs1] - s->gpr[m.r.rs2], m.r.rd);		
		break;
	      default:
		std::cout << "sel = " << m.r.sel << ", special = " << m.r.special << "\n";
		assert(0);
	      }
	    break;
	  case 0x1: /* sll */
	    switch(m.r.special)
	      {
	      case 0x0:
		s->gpr[m.r.rd] = s->gpr[m.r.rs1] << (s->gpr[m.r.rs2] & (s->xlen()-1));
		break;
	      case 0x1: { /* MULH */
		__int128 t = static_cast<__int128>(s->gpr[m.r.rs1]) * static_cast<__int128>(s->gpr[m.r.rs2]);
		s->gpr[m.r.rd] = (t>>64);
		break;
	      }
	      case 0x30: /* rol */
		s->gpr[rd] = rol64(s->get_reg_u64(m.i.rs1), s->gpr[m.r.rs2] & (s->xlen()-1));		
		break;	
	      default:
		std::cout << "sel = " << m.r.sel << ", special = " << m.r.special << "\n";
		std::cout << std::hex << s->pc << std::dec << "\n";
		assert(0);
	      }
	    break;
	  case 0x2: /* slt */
	    switch(m.r.special)
	      {
	      case 0x0:
		s->gpr[m.r.rd] = s->gpr[m.r.rs1] < s->gpr[m.r.rs2];
		break;
	      case 0x10: /* sh1add */
		s->sext_xlen(((s->gpr[m.r.rs1]<<1) + s->gpr[m.r.rs2]), m.r.rd);
		break;
	      default:
		std::cout << "sel = " << m.r.sel << ", special = " << m.r.special << "\n";
		assert(0);		
	      }
	    break;
	  case 0x3: /* sltu */
	    switch(m.r.special)
	      {
	      case 0x0:
		s->gpr[m.r.rd] = u_rs1 < u_rs2;
		break;
	      case 0x1: {/* MULHU */
		__uint128_t t = static_cast<__uint128_t>(u_rs1) * static_cast<__uint128_t>(u_rs2);
		*reinterpret_cast<uint64_t*>(&s->gpr[m.r.rd]) = (t>>64);
		break;
	      }
	      default:
		std::cout << "sel = " << m.r.sel << ", special = " << m.r.special << "\n";
		std::cout << "pc = " << std::hex << s->pc << std::dec << "\n";
		assert(0);		
	      }
	    break;
	  case 0x4:
	    switch(m.r.special)
	      {
	      case 0x0:
		s->gpr[m.r.rd] = s->gpr[m.r.rs1] ^ s->gpr[m.r.rs2];
		break;
	      case 0x1:
		s->gpr[m.r.rd] =s->gpr[m.r.rs2]==0 ? ~0L : s->gpr[m.r.rs1] / s->gpr[m.r.rs2];
		break;
	      case 0x5: /* min */
		s->gpr[m.r.rd] = std::min(s->gpr[m.r.rs1], s->gpr[m.r.rs2]);	
		break;
	      case 0x10: /* sh2add */
		s->sext_xlen(((s->gpr[m.r.rs1]<<2) + s->gpr[m.r.rs2]), m.r.rd);
		break;
	      case 0x20: /* xnor */
		s->gpr[m.r.rd] = ~(s->gpr[m.r.rs1] ^ s->gpr[m.r.rs2]);
		break;
	      default:
		std::cout << "sel = " << m.r.sel << ", special = " << m.r.special << "\n";
		assert(0);		
	      }
	    break;		
	  case 0x5: /* srl & sra */
	    switch(m.r.special)
	      {
	      case 0x0: /* srl */
		s->gpr[rd] = (*reinterpret_cast<uint64_t*>(&s->gpr[m.r.rs1]) >> (s->gpr[m.r.rs2] & (s->xlen()-1)));
		break;
	      case 0x1:
		*reinterpret_cast<uint64_t*>(&s->gpr[m.r.rd]) = u_rs2==0 ? (~0UL) : (u_rs1 / u_rs2);
		break;
	      case 0x5: /* minu */
		*reinterpret_cast<uint64_t*>(&s->gpr[m.r.rd]) = std::min(u_rs1, u_rs2);
		break;
	      case 0x7: 
		s->gpr[m.r.rd] = s->gpr[m.r.rs2]==0 ? 0 : s->gpr[m.r.rs1];
		break;
	      case 0x20: /* sra */
		s->gpr[rd] = s->gpr[m.r.rs1] >> (s->gpr[m.r.rs2] & (s->xlen()-1));
		break;
	      case 0x30: /* ror */
		s->gpr[rd] = ror64(s->get_reg_u64(m.i.rs1), s->gpr[m.r.rs2] & (s->xlen()-1));
		break;
	      default:
		std::cout << "sel = " << m.r.sel << ", special = " << m.r.special << "\n";
		assert(0);				
	      }
	    break;
	  case 0x6:
	    switch(m.r.special)
	      {
	      case 0x0:
		s->gpr[m.r.rd] = s->gpr[m.r.rs1] | s->gpr[m.r.rs2];
		if((m.r.rd == m.r.rs1) and (m.r.rs1 == m.r.rs2)) {
		  // if(tracing_armed) {
		  //   s->brk = 1;
		  //   std::cout << "ran for " << (s->icnt - tracing_start_icnt) << " insns\n";
		  // }
		  // tracing_armed = true;
		  // tracing_start_icnt = s->icnt;
		  // std::cout << ">>>> or with all regs equal\n";
		  //globals::log = true;
		  //exit(-1);
		}
		break;
	      case 0x1:
		if(s->gpr[m.r.rs2] == 0) {
		  s->gpr[m.r.rd] = ~(0L);
		}
		else {
		  s->gpr[m.r.rd] = s->gpr[m.r.rs1] % s->gpr[m.r.rs2];
		}
		break;
	      case 0x5: /* max */
		s->gpr[m.r.rd] = std::max(s->gpr[m.r.rs1], s->gpr[m.r.rs2]);	
		break;
	      case 0x10: /* sh3add */
		s->sext_xlen(((s->gpr[m.r.rs1]<<3) + s->gpr[m.r.rs2]), m.r.rd);		
		break;
	      case 0x20: /* orn */
		s->gpr[m.r.rd] = s->gpr[m.r.rs1] | (~s->gpr[m.r.rs2]);
		break;	
	      default:
		std::cout << "sel = " << m.r.sel << ", special = " << m.r.special << "\n";
		std::cout << "pc = " << std::hex << s->pc << std::dec << "\n";
		assert(0);
	      }
	    break;
	  case 0x7:
	    switch(m.r.special)
	      {
	      case 0x0:
		s->gpr[m.r.rd] = s->gpr[m.r.rs1] & s->gpr[m.r.rs2];
		break;
	      case 0x1: { /* remu */
		*reinterpret_cast<uint64_t*>(&s->gpr[m.r.rd]) = u_rs1 % u_rs2;
		break;
	      }
	      case 0x5: /* maxu */
		*reinterpret_cast<uint64_t*>(&s->gpr[m.r.rd]) = std::max(u_rs1, u_rs2);
		break;	
	      case 0x7: 
		s->gpr[m.r.rd] = s->gpr[m.r.rs2]!=0 ? 0 : s->gpr[m.r.rs1];
		break;
	      case 0x20: /* andn */
		s->gpr[m.r.rd] = s->gpr[m.r.rs1] & (~s->gpr[m.r.rs2]);
		break;
		
	      default:
		std::cout << "sel = " << m.r.sel << ", special = " << m.r.special << "\n";
		assert(0);
	      }
	    break;
	  default:
	    std::cout << "implement = " << m.r.sel << "\n";
	    assert(0);
	  }
      }
      s->pc += 4;
      break;
    }
#if 0
    imm[12|10:5] rs2 rs1 000 imm[4:1|11] 1100011 BEQ
    imm[12|10:5] rs2 rs1 001 imm[4:1|11] 1100011 BNE
    imm[12|10:5] rs2 rs1 100 imm[4:1|11] 1100011 BLT
    imm[12|10:5] rs2 rs1 101 imm[4:1|11] 1100011 BGE
    imm[12|10:5] rs2 rs1 110 imm[4:1|11] 1100011 BLTU
    imm[12|10:5] rs2 rs1 111 imm[4:1|11] 1100011 BGEU
#endif
    case 0x63: {
      int32_t disp32 =
	(m.b.imm4_1 << 1)  |
	(m.b.imm10_5 << 5) |	
        (m.b.imm11 << 11)  |
        (m.b.imm12 << 12);
      disp32 |= m.b.imm12 ? 0xffffe000 : 0x0;
      int64_t disp = disp32;
      disp = (disp << 32) >> 32;
      bool takeBranch = false;
      uint64_t u_rs1 = *reinterpret_cast<uint64_t*>(&s->gpr[m.b.rs1]);
      uint64_t u_rs2 = *reinterpret_cast<uint64_t*>(&s->gpr[m.b.rs2]);
      uint64_t bpu_idx = 0;
      bool bpu_pred = false;
      if(globals::bpred) {
	bpu_pred = globals::bpred->predict(s->pc, bpu_idx);
      }
      switch(m.b.sel)
	{
	case 0: /* beq */
	  takeBranch = s->gpr[m.b.rs1] == s->gpr[m.b.rs2];
	  break;
	case 1: /* bne */
	  takeBranch = s->gpr[m.b.rs1] != s->gpr[m.b.rs2];
	  break;
	case 4: /* blt */
	  takeBranch = s->gpr[m.b.rs1] < s->gpr[m.b.rs2];
	  break;
	case 5: /* bge */
	  takeBranch = s->gpr[m.b.rs1] >= s->gpr[m.b.rs2];	  
	  break;
	case 6: /* bltu */
	  takeBranch = u_rs1 < u_rs2;
	  break;
	case 7: /* bgeu */
	  takeBranch = u_rs1 >= u_rs2;
	  //std::cout << "s->pc " << std::hex << s->pc << ", rs1 " << u_rs1 << ", rs2 "
	  //<< u_rs2 << std::dec
	  //	    << ", takeBranch " << takeBranch
	  //<< "\n";

	  break;
	default:
	  std::cout << "implement case " << m.b.sel << "\n";
	  assert(0);
	}
      //assert(not(takeBranch));
      if(globals::branch_tracer) {
	globals::branch_tracer->add(s->pc, (disp+s->pc), takeBranch);
      }
      if(globals::bpred) {
	globals::bpred->update(s->pc, bpu_idx, bpu_pred, takeBranch,
			       branch_predictor::br_type::cond);
      }
      if(useBBV) {
	int ff;
	s->bblog->addSample(s->translate(s->pc, ff, 4, false, true), s->bbsz);
	s->bbsz = 0;	
      }
      s->pc = takeBranch ? disp + s->pc : s->pc + 4;
      break;
    }

    case 0x73: {
      uint32_t csr_id = (inst>>20);
      bool is_ecall = ((inst >> 7) == 0);
      bool is_ebreak = ((inst>>7) == 0x2000);
      bool bits19to7z = (((inst >> 7) & 8191) == 0);
      uint64_t upper7 = (inst>>25);
      if(is_ecall) { /* ecall and ebreak dont increment the retired instruction count */
	if(not(globals::fullsim)) {
	  s->brk = 1;
	}
	else {
	  except_cause = CAUSE_USER_ECALL + static_cast<int>(s->priv);
	  goto handle_exception;
	}
      }
      else if(upper7 == 9 && ((inst & (16384-1)) == 0x73 )) {
	//std::cout << "warn : got sfence\n";
	//printf("tlb had %lu entries\n", tlb.size());	
	clear_tlb();
      }
      else if(bits19to7z and (csr_id == 0x105)) {  /* wfi */
	//globals::log = 1;
	s->pc += 4;
	break;
      }
      else if(bits19to7z and (csr_id == 0x002)) {  /* uret */
	assert(false);
      }
      else if(bits19to7z and (csr_id == 0x102)) {  /* sret */
	/* stolen from tinyemu */
	//printf("executing sret at priv %d for pc %lx, icnt %lu\n", s->priv, s->pc, s->icnt);
	if(s->priv == 0) {
	  except_cause = CAUSE_ILLEGAL_INSTRUCTION;
	  tval = s->pc;
	  goto handle_exception;	  
	}
	assert( ((s->mstatus >> MSTATUS_UXL_SHIFT) & 3) == 2);
	assert( ((s->mstatus >> MSTATUS_SXL_SHIFT) & 3) == 2);
	
	int spp = (s->mstatus >> MSTATUS_SPP_SHIFT) & 1;
	/* set the IE state to previous IE state */
	int spie = (s->mstatus >> MSTATUS_SPIE_SHIFT) & 1;
	s->mstatus = (s->mstatus & ~(1 << spp)) | (spie << spp);
	/* set SPIE to 1 */
	s->mstatus |= MSTATUS_SPIE;
	/* set SPP to U */
	s->mstatus &= ~MSTATUS_SPP;
	set_priv(s, spp);
	s->pc = s->sepc;
	//globals::log = true;
	break;
      }
      else if(bits19to7z and (csr_id == 0x202)) {  /* hret */
	assert(false);
      }            
      else if(bits19to7z and (csr_id == 0x302)) {  /* mret */
	/* stolen from tinyemu */
	//auto m0 =  csr_t(s->mstatus).mstatus;
	if(s->priv < 3) {
	  except_cause = CAUSE_ILLEGAL_INSTRUCTION;
	  tval = s->pc;
	  goto handle_exception;	  
	}
	int mpp = (s->mstatus >> MSTATUS_MPP_SHIFT) & 3;
	/* set the IE state to previous IE state */
	int mpie = (s->mstatus >> MSTATUS_MPIE_SHIFT) & 1;
	s->mstatus = (s->mstatus & ~(1 << mpp)) |(mpie << mpp);
	/* set MPIE to 1 */
	s->mstatus |= MSTATUS_MPIE;
	/* set MPP to U */
	s->mstatus &= ~MSTATUS_MPP;
	set_priv(s, mpp);
	s->pc = s->mepc;

	assert( ((s->mstatus >> MSTATUS_UXL_SHIFT) & 3) == 2);
	assert( ((s->mstatus >> MSTATUS_SXL_SHIFT) & 3) == 2);
	break;
      }
      else if(is_ebreak) {
	if(globals::fullsim) {
	  except_cause = CAUSE_BREAKPOINT;
	  goto handle_exception;
	}
      }
      else {
	int rd = (inst>>7) & 31;
        int rs = (inst >> 15) & 31;
	bool undef=false;
	      
	switch((inst>>12) & 7)
	  {	    
	  case 1: { /* CSRRW */
	    int64_t v = 0;
	    if(rd != 0) {
	      v = read_csr(csr_id, s, undef);
	      if(undef) goto report_unimplemented;
	    }
	    write_csr(csr_id, s, s->gpr[rs], undef);
	    if(undef) goto report_unimplemented;
	    if(rd != 0) {
	      s->gpr[rd] = v;
	    }
	    break;
	  }
	  case 2: {/* CSRRS */
	    int64_t t = read_csr(csr_id, s, undef);
	    if(undef) goto report_unimplemented;
	    if(rs != 0) {
	      write_csr(csr_id, s, t | s->gpr[rs], undef);
	      if(undef) goto report_unimplemented;
	    }
	    if(rd != 0) {
	      s->gpr[rd] = t;
	    }
	    break;
	  }
	  case 3: {/* CSRRC */
	    int64_t t = read_csr(csr_id, s,undef);
	    if(undef) goto report_unimplemented;	    
	    if(rs != 0) {
	      write_csr(csr_id, s, t & (~s->gpr[rs]), undef);
	      if(undef) goto report_unimplemented;	      
	    }
	    if(rd != 0) {
	      s->gpr[rd] = t;
	    }
	    break;
	  }
	  case 5: {/* CSRRWI */
	    int64_t t = 0;
	    if(rd != 0) {
	      t = read_csr(csr_id, s, undef);
	      if(undef) goto report_unimplemented;
	    }
	    write_csr(csr_id, s, rs, undef);
	    if(undef) goto report_unimplemented;
	    if(rd != 0) {
	      s->gpr[rd] = t;
	    }
	    break;
	  }
	  case 6:{ /* CSRRSI */
	    int64_t t = read_csr(csr_id,s,undef);
	    if(undef) goto report_unimplemented;
	    if(rs != 0) {
	      write_csr(csr_id, s, t | rs, undef);
	      if(undef) goto report_unimplemented;
	    }
	    if(rd != 0) {
	      s->gpr[rd] = t;
	    }
	    break;
	  }
	  case 7: {/* CSRRCI */
	    int64_t t = read_csr(csr_id, s, undef);
	    if(undef) goto report_unimplemented;	    
	    if(rs != 0) {
	      write_csr(csr_id, s, t & (~rs), undef);
	      if(undef) goto report_unimplemented;	      
	    }
	    if(rd != 0) {
	      s->gpr[rd] = t;
	    }
	    break;
	  }
	  default:
	    goto report_unimplemented;	    
	  }
      }
      s->pc += 4;
      break;
    }
      
    default:
      std::cout << std::hex << s->pc << std::dec
		<< " : " << getAsmString(inst, s->pc)
		<< " , opcode "
		<< std::hex
		<< opcode
		<< " , insn "
		<< inst
		<< std::dec
		<< " , icnt " << s->icnt
		<< "\n";
      std::cout << *s << "\n";
      dump_calls();      
      exit(-1);
      break;
    }

 instruction_complete:
  s->icnt++;
  return;
  
 handle_exception: {
    bool delegate = false;
    s->last_phys_pc = 0;
    if(s->priv == priv_user || s->priv == priv_supervisor) {
      if(except_cause & CAUSE_INTERRUPT) {
	uint32_t cc = (except_cause & 0x7fffffffUL);
	delegate = ((s->mideleg) >> cc) & 1;
      }
      else {
	delegate = (s->medeleg >> except_cause) & 1;
      }
    }

    uint64_t cause = (except_cause & 0x7fffffffUL);
    if(except_cause & CAUSE_INTERRUPT) {
      cause |= 1UL<<63;
    }
    
    if( /*(cause != 9 and cause < 16)*/ globals::log) {
      std::cout << "--> took fault at pc "
		<< std::hex << s->pc
		<< ", tval " << tval
		<< std::dec
		<< ", icnt " << s->icnt
		<< ", cause " << std::hex
		<< cause << std::dec << " : ";
      if(cause < 16) {
	std::cout << cause_reasons.at(cause);
	dump_calls();	
      }
      else {
	std::cout << "irq " << (cause & 31);
      }
      std::cout << ", delegate " << delegate
		<< ", priv "
		<< s->priv << "\n";
    }
    
    if(delegate) {
      s->scause = cause;
      s->sepc = s->pc;
      s->stval = tval;
      s->mstatus = (s->mstatus & ~MSTATUS_SPIE) |
	(((s->mstatus >> s->priv) & 1) << MSTATUS_SPIE_SHIFT);
      s->mstatus = (s->mstatus & ~MSTATUS_SPP) |
	(s->priv << MSTATUS_SPP_SHIFT);
      s->mstatus &= ~MSTATUS_SIE;
      set_priv(s, priv_supervisor);
      s->pc = s->stvec;
    }
    else {

      s->mcause = cause;
      s->mepc = s->pc;
      s->mtval = tval;
      s->mstatus = (s->mstatus & ~MSTATUS_MPIE) |
	(((s->mstatus >> s->priv) & 1) << MSTATUS_MPIE_SHIFT);
      s->mstatus = (s->mstatus & ~MSTATUS_MPP) |
	(s->priv << MSTATUS_MPP_SHIFT);
      s->mstatus &= ~MSTATUS_MIE;
      set_priv(s, priv_machine);
      s->pc = s->mtvec;
    }
  }
  return;
  
 report_unimplemented:
  std::cout << std::hex << s->pc << std::dec
	    << " : " << getAsmString(inst, s->pc)
	    << " , raw " << std::hex
	    << inst
	    << std::dec
	    << " , icnt " << s->icnt
	    << "\n";  
  assert(false);
  
  
}

void runRiscvSimPoint(state_t *s) {
  bool keep_going = (s->brk==0) and
    (s->icnt < s->maxicnt);
  
  if(not(keep_going))
    return;

  do {
    execRiscv_<false,false,true,true>(s);
    keep_going = (s->brk==0) and
      (s->icnt < s->maxicnt);
  } while(keep_going);  
}


void runRiscv(state_t *s, uint64_t dumpIcnt) {
  bool keep_going = (s->brk==0) and
    (s->icnt < s->maxicnt) and
    (s->icnt < dumpIcnt);
  if(not(keep_going))
    return;

  if(s->icache and s->dcache) {
    do {
      execRiscv_<true,true,false>(s);
      bool dump = (s->icnt >= dumpIcnt) /*and (s->priv == 0)*/;
      keep_going = (s->brk==0) and
	(s->icnt < s->maxicnt) and
	not(dump);
    } while(keep_going);        
  }
  else if(s->icache and s->dcache == nullptr) {
    do {
      execRiscv_<true,false,false>(s);
      bool dump = (s->icnt >= dumpIcnt) /*and (s->priv == 0)*/;
      keep_going = (s->brk==0) and
	(s->icnt < s->maxicnt) and
	not(dump);
    } while(keep_going);        
  }
  else if(s->icache==nullptr and s->dcache) {
    do {
      execRiscv_<false,true,false>(s);
      bool dump = (s->icnt >= dumpIcnt) /*and (s->priv == 0)*/;
      keep_going = (s->brk==0) and
	(s->icnt < s->maxicnt) and
	not(dump);
    } while(keep_going);    
  }
  else {
    do {
      execRiscv_<false,false,false>(s);
      bool dump = (s->icnt >= dumpIcnt) /*and (s->priv == 0)*/;
      keep_going = (s->brk==0) and
	(s->icnt < s->maxicnt) and
	not(dump);
    } while(keep_going);
  }
}

void execRiscv(state_t *s) {
  execRiscv_<false,false,false>(s);
}

void runInteractiveRiscv(state_t *s) {
  bool run = true;
  int fault = 0;
  while(run) {
    int64_t steps = 0;
    std::cout << "enter steps\n";
    std::cin >> steps;
    
    if(steps == 0) {
      continue;
    }

    std::cout << "running for " << steps <<  " steps\n";
    
    while(steps) {
      execRiscv_<false,false,false>(s);
      run = s->brk==0 and (s->icnt < s->maxicnt);
      if(not(run))
	break;
      --steps;
    }
    uint64_t phys_pc = s->translate(s->pc, fault, 8, false, false);
    uint32_t insn = s->load32(phys_pc);
    std::cout << "priv " << s->priv << " vpc "
	      << std::hex
	      << s->pc
	      << " ppc "
	      << phys_pc
	      << " : "
	      << std::dec
	      << getAsmString(insn,s->pc)
	      << "\n";

    //dump_calls();
  }
}

