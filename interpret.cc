#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>

#include "interpret.hh"
#include "disassemble.hh"
#include "helper.hh"
#include "globals.hh"

/* stolen from tinyemu */
#define CAUSE_MISALIGNED_FETCH    0x0
#define CAUSE_FAULT_FETCH         0x1
#define CAUSE_ILLEGAL_INSTRUCTION 0x2
#define CAUSE_BREAKPOINT          0x3
#define CAUSE_MISALIGNED_LOAD     0x4
#define CAUSE_FAULT_LOAD          0x5
#define CAUSE_MISALIGNED_STORE    0x6
#define CAUSE_FAULT_STORE         0x7
#define CAUSE_USER_ECALL          0x8
#define CAUSE_SUPERVISOR_ECALL    0x9
#define CAUSE_HYPERVISOR_ECALL    0xa
#define CAUSE_MACHINE_ECALL       0xb
#define CAUSE_FETCH_PAGE_FAULT    0xc
#define CAUSE_LOAD_PAGE_FAULT     0xd
#define CAUSE_STORE_PAGE_FAULT    0xf
/* Note: converted to correct bit position at runtime */
#define CAUSE_INTERRUPT  ((uint32_t)1 << 31)


#define MSTATUS_SPIE_SHIFT 5
#define MSTATUS_MPIE_SHIFT 7
#define MSTATUS_SPP_SHIFT 8
#define MSTATUS_MPP_SHIFT 11
#define MSTATUS_FS_SHIFT 13
#define MSTATUS_UXL_SHIFT 32
#define MSTATUS_SXL_SHIFT 34
#define MSTATUS_UIE (1 << 0)
#define MSTATUS_SIE (1 << 1)
#define MSTATUS_HIE (1 << 2)
#define MSTATUS_MIE (1 << 3)
#define MSTATUS_UPIE (1 << 4)
#define MSTATUS_SPIE (1 << MSTATUS_SPIE_SHIFT)
#define MSTATUS_HPIE (1 << 6)
#define MSTATUS_MPIE (1 << MSTATUS_MPIE_SHIFT)
#define MSTATUS_SPP (1 << MSTATUS_SPP_SHIFT)
#define MSTATUS_HPP (3 << 9)
#define MSTATUS_MPP (3 << MSTATUS_MPP_SHIFT)
#define MSTATUS_FS (3 << MSTATUS_FS_SHIFT)
#define MSTATUS_XS (3 << 15)
#define MSTATUS_MPRV (1 << 17)
#define MSTATUS_SUM (1 << 18)
#define MSTATUS_MXR (1 << 19)
#define MSTATUS_UXL_MASK ((uint64_t)3 << MSTATUS_UXL_SHIFT)
#define MSTATUS_SXL_MASK ((uint64_t)3 << MSTATUS_SXL_SHIFT)

#define MSTATUS_MASK (MSTATUS_UIE | MSTATUS_SIE | MSTATUS_MIE |		\
                      MSTATUS_UPIE | MSTATUS_SPIE | MSTATUS_MPIE |	\
                      MSTATUS_SPP | MSTATUS_MPP |			\
                      MSTATUS_FS |					\
                      MSTATUS_MPRV | MSTATUS_SUM | MSTATUS_MXR )


static std::set<int> written_csrs;

void initState(state_t *s) {
  memset(s, 0, sizeof(state_t));
  s->misa = 0x800000000014112dL;
  s->priv = priv_machine;
  s->mstatus = ((uint64_t)2 << MSTATUS_UXL_SHIFT) |((uint64_t)2 << MSTATUS_SXL_SHIFT);
  
  written_csrs.insert(0x301);
  written_csrs.insert(0xf14);
}


uint64_t state_t::translate(uint64_t ea, int &fault, bool store) const {
  fault = false;
  csr_t c(satp);
  if((c.satp.mode == 0) or
     (priv == priv_hypervisor) or
     (priv == priv_machine)) {
    return ea;
  }
  //std::cout << std::hex << "ea = " << ea << std::dec << "\n";
  assert(c.satp.mode == 8);
  uint64_t vpn0 = (ea >> 12) & 511;
  uint64_t vpn1 = (ea >> 21) & 511;
  uint64_t vpn2 = (ea >> 30) & 511;
  uint64_t a, u;
  int mask_bits = -1;
  pte_t r(0);  
  //std::cout << "page table root = "
  //	    << std::hex
  //<< (c.satp.ppn << 12)
  //<< std::dec
  //<< "\n";
  
  a = (c.satp.ppn * 4096) + (vpn2*8);
  //std::cout << std::hex << "L2 " << a << std::dec << "\n";  
  u = *reinterpret_cast<uint64_t*>(mem + a);
  if((u&1) == 0) {
    fault = 1;
    return 2;
  }  
  r.r = u;
  if(r.sv39.x || r.sv39.w || r.sv39.r) {
    mask_bits = 30;
    goto translation_complete;
  }
  
  a = (r.sv39.ppn * 4096) + (vpn1*8);
  //std::cout << std::hex << "L1 " << a << std::dec << "\n";
  
  u = *reinterpret_cast<uint64_t*>(mem + a);
  if((u&1) == 0) {
    fault = 1;
    return 1;
  }  
  r.r = u;
  if(r.sv39.x || r.sv39.w || r.sv39.r) {
    mask_bits = 21;
    goto translation_complete;
  }
  
  a = (r.sv39.ppn * 4096) + (vpn0*8);
  //std::cout << std::hex << "L0 " << a << std::dec << "\n";
  
  u = *reinterpret_cast<uint64_t*>(mem + a);  
  if((u&1) == 0) {
    fault = 1;
    return 0;
  }
  assert(r.sv39.x || r.sv39.w || r.sv39.r);
  mask_bits = 12;
  
 translation_complete:
  assert(mask_bits != -1);
  if(r.sv39.a == 0) {
    r.sv39.a = 1;
    *reinterpret_cast<uint64_t*>(mem + a) = r.r;
  }
  if((r.sv39.d == 0) && store) {
    r.sv39.d = 1;
    *reinterpret_cast<uint64_t*>(mem + a) = r.r;    
  }
  int64_t m = ((1L << mask_bits) - 1);
  int64_t pa = ((r.sv39.ppn * 4096) & (~m)) | (ea & m);

  //std::cout << std::hex << ea << " mapped to " << pa << std::dec << "\n";

  //assert(pa == (ea & ((1UL<<32) - 1)));
  return pa;
}

static void set_priv(state_t *s, int priv) {
  if (s->priv != priv) {
    //tlb_flush_all(s);
    int mxl;
    if (priv == priv_supervisor) {
      mxl = (s->mstatus >> MSTATUS_SXL_SHIFT) & 3;
      assert(mxl == 2);
    }
    else if (priv == priv_user) {
      mxl = (s->mstatus >> MSTATUS_UXL_SHIFT) & 3;
      assert(mxl == 2);
    }
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

static int64_t read_csr(int csr_id, state_t *s) {
  switch(csr_id)
    {
    case 0x100:
      return s->sstatus;
    case 0x104:
      return s->sie;
    case 0x105:
      return s->stvec;
    case 0x144:
      return s->sip;
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
    case 0xf14:
      return s->mhartid;      
    default:
      printf("rd csr id 0x%x unimplemented\n", csr_id);
      assert(false);
      break;
    }
  return 0;
}

static void write_csr(int csr_id, state_t *s, int64_t v) {
  csr_t c(v);
  switch(csr_id)
    {
    case 0x100:
      s->sstatus = v;
      break;
    case 0x104:
      s->sie = v;
      break;
    case 0x105:
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
      s->stvec = v;
      break;
    case 0x144:
      s->sip = v;
      break;
    case 0x180:
      std::cout << "attempting to set mode to " << c.satp.mode << "\n";
      if(c.satp.mode == 8) {
	std::cout << "set mode to " << c.satp.mode << "\n";
	//assert(c.satp.mode==0);
	s->satp = v;
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
      s->pmpaddr0 = v;
      break;
    case 0x3b1:
      s->pmpaddr1 = v;
      break;
    case 0x3b2:
      s->pmpaddr2 = v;
      break;
    case 0x3b3:
      s->pmpaddr3 = v;
      break;                  
    default:
      printf("wr csr id 0x%x unimplemented\n", csr_id);
      assert(false);
      break;
    }
}

uint64_t last_tval = 0;

void execRiscv(state_t *s) {
  uint8_t *mem = s->mem;
  int fetch_fault = 0, except_cause = -1, tval = -1;
  uint64_t tohost = 0;
  uint64_t phys_pc = s->translate(s->pc, fetch_fault);
  uint32_t inst = 0, opcode = 0, rd = 0, lop = 0;
  riscv_t m(0);
  if(fetch_fault) {
    except_cause = CAUSE_FETCH_PAGE_FAULT;
    tval = s->pc;
    std::cout << "taking iside page fault for "
	      << std::hex << tval << std::dec
	      << " at icnt "
	      << s->icnt
	      << "\n";
    if(tval == last_tval) {
      abort();
    }
    last_tval = tval;
    //std::cout << csr_t(s->mstatus).mstatus << "\n";
    goto handle_exception;
  }
  assert(phys_pc < (1UL << 32));
  // assert(!fetch_fault);
  
  inst = *reinterpret_cast<uint32_t*>(mem + phys_pc);
  m.raw = inst;
  opcode = inst & 127;
  
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
      assert(dev == 1);
      if(cmd == 1) {
	std::cout << static_cast<char>(payload & 0xff);
	*reinterpret_cast<uint64_t*>(mem + globals::tohost_addr) = 0;
	*reinterpret_cast<uint64_t*>(mem + globals::fromhost_addr) = (dev << 56) | (cmd << 48);
      }
      else {
	abort();
      }
    }
    else {
      tohost &= ((1UL<<32)-1);      
      handle_syscall(s, tohost);
    }
  }
  lop = (opcode & 3);
  rd = (inst>>7) & 31;


  
  if(globals::log) {
    std::cout << std::hex << s->pc << std::dec
	      << " : " << getAsmString(inst, s->pc)
	      << " , raw " << std::hex
	      << inst
	      << std::dec
	      << " , icnt " << s->icnt
	      << "\n";
  }
  s->last_pc = s->pc;  


  //#define OLD_GPR
  
#ifdef OLD_GPR
  int64_t old_gpr[32];
  uint64_t old_pc = s->pc;
  memcpy(old_gpr, s->gpr, sizeof(old_gpr));
#endif

  assert(lop == 3);
  if(lop != 3) {
    uint16_t cinst = *reinterpret_cast<uint16_t*>(mem + s->pc);
    std::cout << std::hex <<cinst << std::dec << "\n";
    uint16_t cop = cinst >> 13;
    uint16_t oix = (cop << 2) | lop;
    uint16_t rd = (cinst >> 7) & 31;
    uint16_t rs2 = ((cinst >> 2) & 31);
    switch(oix)
      {
      case 1: /* nop or addi*/
	if(cinst != 1) {
	  int64_t simm64 = ((cinst >> 2) & 31) | ((cinst & 4096) >> 7);
	  simm64 = (simm64 << 58)>>58;
	  s->sext_xlen(s->gpr[rd] + simm64, rd);
	}
	s->pc += 2;
	break;
      case 0xd:
	if(rd == 2) {
	  assert(0);
	}
	else if(rd != 0) { /* lui */
	  assert(0);
	}
	s->pc += 2;	
	break;
      case 0x12: /* jr, mv, ebreak, jalr, or add */
	if((cinst>>12) & 1) { /* ebreak, jalr, add */
	  assert(0);
	}
	else { /* jr or mv */
	  if( rs2 == 0 ) { /* jalr */
	    assert(0);
	  }
	  else { /* add */
	    if(rd != 0) {
	      s->sext_xlen(s->gpr[rs2] + s->gpr[rd], rd);
	    }
	    s->pc += 2;
	  }
	}
	break;
      case 0x1e:{ /* SDSP */
	uint16_t off = (cinst>>7) & 63;
	uint16_t disp = off & 0x38;
	disp |= (off & 7) << 6;
	int64_t ea = s->gpr[2] + disp;
	*(reinterpret_cast<int64_t*>(s->mem + ea)) = s->gpr[rs2];
	s->pc += 2;
	break;
      }
      default:
	std::cout << std::hex << lop << std::dec << "\n";
	std::cout << std::hex << cop << std::dec << "\n";
	std::cout << std::hex << oix << std::dec << "\n";
	assert(0);
      }
    return;
  }

  assert(lop == 3);
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
	int page_fault = 0;
	int64_t pa = s->translate(ea, page_fault);
	if(page_fault) {
	  except_cause = CAUSE_LOAD_PAGE_FAULT;
	  tval = ea;
	  std::cout << "load page fault for   " << std::hex << ea << " at pc " << s->pc << std::dec << "\n";
	  std::cout << "load page fault depth " << std::hex << pa << std::dec << "\n";
	  if(ea == 0) {
	    abort();
	  }
	  goto handle_exception;
	}
	
	switch(m.s.sel)
	  {
	  case 0x0: /* lb */
	    s->gpr[m.l.rd] = static_cast<int32_t>(*(reinterpret_cast<int8_t*>(s->mem + pa)));	 
	    break;
	  case 0x1: /* lh */
	    s->gpr[m.l.rd] = static_cast<int32_t>(*(reinterpret_cast<int16_t*>(s->mem + pa)));	 
	    break;
	  case 0x2: /* lw */
	    s->sext_xlen( *reinterpret_cast<int32_t*>(s->mem + pa), m.l.rd);
	    break;
	  case 0x3: /* ld */
	    s->sext_xlen( *reinterpret_cast<int64_t*>(s->mem + pa), m.l.rd);	    
	    break;
	  case 0x4: {/* lbu */
	    uint32_t b = s->mem[pa];
	    *reinterpret_cast<uint64_t*>(&s->gpr[m.l.rd]) = b;
	    break;
	  }
	  case 0x5: { /* lhu */
	    uint16_t b = *reinterpret_cast<uint16_t*>(s->mem + pa);
	    *reinterpret_cast<uint64_t*>(&s->gpr[m.l.rd]) = b;
	    break;
	  }
	  case 0x6: { /* lwu */
	    uint32_t b = *reinterpret_cast<uint32_t*>(s->mem + pa);
	    *reinterpret_cast<uint64_t*>(&s->gpr[m.l.rd]) = b;
	    break;
	  }	    
	  default:
	    assert(0);
	  }
	s->pc += 4;
	break;
      }
    }
    case 0xf: { /* fence - there's a bunch of 'em */
      s->pc += 4;
      break;
    }
#if 0
    imm[11:0] rs1 000 rd 0010011 ADDI
    imm[11:0] rs1 010 rd 0010011 SLTI
    imm[11:0] rs1 011 rd 0010011 SLTIU
    imm[11:0] rs1 100 rd 0010011 XORI
    imm[11:0] rs1 110 rd 0010011 ORI
    imm[11:0] rs1 111 rd 0010011 ANDI
    0000000 shamt rs1 001 rd 0010011 SLLI
    0000000 shamt rs1 101 rd 0010011 SRLI
    0100000 shamt rs1 101 rd 0010011 SRAI
#endif
    case 0x13: {
      int32_t simm32 = (inst >> 20);

      simm32 |= ((inst>>31)&1) ? 0xfffff000 : 0x0;
      int64_t simm64 = simm32;
      //sign extend!
      simm64 = (simm64 <<32) >> 32;
      uint32_t subop =(inst>>12)&7;
      uint32_t shamt = (inst>>20) & (s->xlen()-1);

      if(rd != 0) {
	switch(m.i.sel)
	  {
	  case 0: {/* addi */
	    s->sext_xlen(s->gpr[m.i.rs1] + simm64, rd);
	    break;
	  }
	  case 1: /* slli */
	    s->sext_xlen((*reinterpret_cast<uint64_t*>(&s->gpr[m.i.rs1])) << shamt, rd);
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
	    uint32_t sel =  (inst >> 26) & 127;	    
	    if(sel == 0) { /* srli */
	      s->gpr[rd] = (*reinterpret_cast<uint64_t*>(&s->gpr[m.i.rs1]) >> shamt);
	    }
	    else if(sel == 16) { /* srai */
	      s->gpr[rd] = s->gpr[m.i.rs1] >> shamt;
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
	    //std::cout << std::hex << "pc     = " << s->pc << std::dec << "\n";
	    //std::cout << std::hex << "simm64 = " << simm64 << std::dec << "\n";
	    //std::cout << std::hex << "srcA   = " << s->gpr[m.i.rs1] << std::dec << "\n";
	    s->sext_xlen((s->gpr[m.i.rs1] & simm64), rd);
	    break;
	    
	  default:
	    std::cout << "implement case " << subop << "\n";
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
	    //std::cout << std::hex << simm32 << std::dec << "\n";
	    //std::cout << std::hex << s->gpr[m.i.rs1] << std::dec << "\n";
	    //std::cout << std::hex << r << std::dec << "\n";
	    s->sext_xlen(r, rd);
	    break;
	  }
	  case 1: { /*SLLIW*/
	    int32_t r = *reinterpret_cast<int32_t*>(&s->gpr[m.i.rs1]) << shamt;
	    s->sext_xlen(r, rd);
	    break;
	  }
	  case 5: { 
	    uint32_t sel =  (inst >> 25) & 127;
	    if(sel == 0) { /* SRLIW */
	      uint32_t r = *reinterpret_cast<uint32_t*>(&s->gpr[m.i.rs1]) >> shamt;
	      int32_t rr =  *reinterpret_cast<int32_t*>(&r);
	      //std::cout << std::hex << *reinterpret_cast<uint32_t*>(&s->gpr[m.i.rs1])
	      //<< std::dec << "\n";
	      //std::cout << "rr = " << std::hex << rr << std::dec << "\n";
	      s->sext_xlen(rr, rd);
	    }
	    else if(sel == 32){ /* SRAIW */
	      int32_t r = *reinterpret_cast<int32_t*>(&s->gpr[m.i.rs1]) >> shamt;
	      s->sext_xlen(r, rd);	      
	    }
	    else {
	      assert(0);
	    }
	    break;	    
	  }
	  default:
	    std::cout << m.i.sel << "\n";
	    assert(0);
	    break;
	  }
      }
      s->pc += 4;
      break;
    }
    case 0x2f: {
      if(m.a.sel == 2) {
	switch(m.a.hiop)
	  {
	  case 0x1: {/* amoswap.w */
	    int page_fault = 0;
	    uint64_t ea = s->translate(s->gpr[m.a.rs1], page_fault, true);
	    assert(!page_fault);
	    int32_t x = *reinterpret_cast<int32_t*>(s->mem + ea);
	    *reinterpret_cast<int32_t*>(s->mem + ea) = s->gpr[m.a.rs2];
	    s->sext_xlen(x, m.a.rd);
	    break;
	  }
	  default:
	    std::cout << "m.a.hiop " << m.a.hiop << "\n";
	    assert(false);
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
	else if((m.r.sel == 0) & (m.r.special == 32)) { /* subw */
	  int32_t c = a-b;
	  s->sext_xlen(c, m.r.rd);
	} 
	else if((m.r.sel == 1) & (m.r.special == 0)) { /* sllw */
	  int32_t c = a << (s->gpr[m.r.rs2]&31);
	  s->sext_xlen(c, m.r.rd);
	}
	else if((m.r.sel == 4) & (m.r.special == 1)) { /* divw */
	  int32_t c = a/b;
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
	  uint32_t c = aa/bb;
	  int32_t rr =  *reinterpret_cast<int32_t*>(&c);
	  s->sext_xlen(rr, m.r.rd);
	}
	else if((m.r.sel == 5) & (m.r.special == 32)) { /* sraw */
	  int32_t c = a >> (s->gpr[m.r.rs2]&31);
	  s->sext_xlen(c, m.r.rd);	  
	}
	else if((m.r.sel == 6) & (m.r.special == 1)) { /* remw */
	  int32_t c = a % b;
	  s->sext_xlen(c, m.r.rd);
	}
	else if((m.r.sel == 7) & (m.r.special == 1)) { /* remuw */
	  uint32_t aa = s->get_reg_u32(m.r.rs1);
	  uint32_t bb = s->get_reg_u32(m.r.rs2);
	  uint32_t c = aa%bb;
	  int32_t rr =  *reinterpret_cast<int32_t*>(&c);
	  s->sext_xlen(rr, m.r.rd);
	}
	else {
	  std::cout << "special = " << m.r.special << "\n";
	  std::cout << "sel = " << m.r.sel << "\n";
	  assert(0);
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
      ea = s->translate(ea, fault, true);
      assert(!fault);
      
      switch(m.s.sel)
	{
	case 0x0: /* sb */
	  s->mem[ea] = *reinterpret_cast<uint8_t*>(&s->gpr[m.s.rs2]);
	  break;
	case 0x1: /* sh */
	  *(reinterpret_cast<uint16_t*>(s->mem + ea)) = *reinterpret_cast<uint16_t*>(&s->gpr[m.s.rs2]);
	  break;
	case 0x2: /* sw */
	  *(reinterpret_cast<int32_t*>(s->mem + ea)) = s->gpr[m.s.rs2];
	  break;
	case 0x3: /* sd */
	  *(reinterpret_cast<int64_t*>(s->mem + ea)) = s->gpr[m.s.rs2];
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
      if(m.jj.rd != 0) {
	s->gpr[m.jj.rd] = s->pc + 4;
      }
      //std::cout << "target = " << std::hex << tgt64 << std::dec << "\n";
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
		s->gpr[m.r.rd] = s->gpr[m.r.rs1] / s->gpr[m.r.rs2];
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
	      case 0x1: {
		*reinterpret_cast<uint64_t*>(&s->gpr[m.r.rd]) = u_rs1 / u_rs2;
		break;
	      }
	      case 0x20: /* sra */
		s->gpr[rd] = s->gpr[m.r.rs1] >> (s->gpr[m.r.rs2] & (s->xlen()-1));
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
		break;
	      case 0x1:
		s->gpr[m.r.rd] = s->gpr[m.r.rs1] % s->gpr[m.r.rs2];
		break;		
	      default:
		std::cout << "sel = " << m.r.sel << ", special = " << m.r.special << "\n";
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
		//std::cout << std::hex << u_rs1 << std::dec << "\n";
		//std::cout << std::hex << u_rs2 << std::dec << "\n";
		//std::cout << std::hex << (u_rs1 % u_rs2) << std::dec << "\n";
		break;
	      }
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
	std::cout << "warn : got sfence\n";
      }
      else if(bits19to7z and (csr_id == 0x002)) {  /* uret */
	assert(false);
      }
      else if(bits19to7z and (csr_id == 0x102)) {  /* sret */
	assert(false);
      }
      else if(bits19to7z and (csr_id == 0x202)) {  /* hret */
	assert(false);
      }            
      else if(bits19to7z and (csr_id == 0x302)) {  /* mret */
	/* stolen from tinyemu */
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
	std::cout << "mret jump to " << std::hex << s->pc << std::dec << "\n";
	break;
      }
      else if(is_ebreak) {
	/* used as monitor in RTL */
      }
      else {
	int rd = (inst>>7) & 31;
        int rs = (inst >> 15) & 31;
	switch((inst>>12) & 7)
	  {
	  case 1: { /* CSRRW */
	    int64_t v = 0;
	    if(rd != 0) {
	      v = read_csr(csr_id, s);
	    }
	    write_csr(csr_id, s, s->gpr[rs]);
	    if(rd != 0) {
	      s->gpr[rd] = v;
	    }
	    break;
	  }
	  case 2: /* CSRRS */
	    assert(rs == 0);
	    if(rd != 0) {
	      s->gpr[rd] = read_csr(csr_id, s);
	    }
	    //std::cout << "read " << std::hex << s->gpr[rd]  << std::dec << "\n";
	    break;
	  case 3: {/* CSRRC */
	    int64_t t = read_csr(csr_id, s);
	    if(rs != 0) {
	      write_csr(csr_id, s, t & (~s->gpr[rs]));
	    }
	    s->gpr[rd] = t;
	    break;
	  }
	  case 5: {/* CSRRWI */
	    int64_t t = 0;
	    if(rd != 0) {
	      t = read_csr(csr_id, s);
	    }
	    write_csr(csr_id, s, rs);
	    if(rd != 0) {
	      s->gpr[rd] = t;
	    }
	    break;
	  }
	    
	  case 6: /* CSRRSI */
	  case 7: /* CSRRCI */
	  default:
	    assert(false);
	    break;
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
      exit(-1);
      break;
    }

  s->icnt++;
  return;
  
 handle_exception: {
    bool delegate = false;
    
    if(s->priv == priv_user || s->priv == priv_supervisor) {
      if(except_cause & CAUSE_INTERRUPT) {
	assert(false);
      }
      else {
	delegate = (s->medeleg >> except_cause) & 1;
	
      }
    }
    
    if(delegate) {
      s->scause = except_cause & 0x7fffffff;
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
      s->mcause = except_cause & 0x7fffffff;
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
  
#ifdef OLD_GPR
  for(int i = 0; i < 32; i++){
    if(old_gpr[i] != s->gpr[i]) {
      std::cout << "\t" << getGPRName(i) << " changed from "
		<< std::hex
		<< old_gpr[i]
		<< " to "
		<< s->gpr[i]	
		<< " at pc "
		<< old_pc
		<< std::dec
		<< "\n";
    }
  }
#endif
  
}

void runRiscv(state_t *s, uint64_t dumpIcnt) {
  while(s->brk==0 and (s->icnt < s->maxicnt) and (s->icnt < dumpIcnt)) {
    execRiscv(s);
  }
}

