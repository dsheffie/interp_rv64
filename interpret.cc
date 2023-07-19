#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <map>
#include <stack>

#include "interpret.hh"
#include "disassemble.hh"
#include "helper.hh"
#include "globals.hh"

//#define CALLSTACK_DEBUG


template <bool EL> void execRiscv(state_t *s);


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

void initState(state_t *s) {
  memset(s, 0, sizeof(state_t));
  s->gpr[2] = 0x80000000;
}


void execRiscv(state_t *s) {
  uint8_t *mem = s->mem;

  uint32_t inst = *reinterpret_cast<uint32_t*>(mem + s->pc);
  uint32_t opcode = inst & 127;

  uint64_t tohost = *reinterpret_cast<uint64_t*>(mem + globals::tohost_addr);
  
  if(tohost) {
    std::cout << "tohost = " << std::hex << tohost << std::dec << "\n";
    uint64_t *buf = reinterpret_cast<uint64_t*>(mem + tohost);
    switch(buf[0])
      {
      case SYS_write: /* int write(int file, char *ptr, int len) */
	buf[0] = (int32_t)write(buf[1], (void*)(s->mem + buf[2]), buf[3]);
	if(buf[1]==1)
	  fflush(stdout);
	else if(buf[1]==2)
	  fflush(stderr);
	break;
      default:
	std::cout << "syscall " << buf[0] << " unsupported\n";
	exit(-1);
      }
    //ack
    *reinterpret_cast<uint64_t*>(mem + globals::tohost_addr) = 0;
    *reinterpret_cast<uint64_t*>(mem + globals::fromhost_addr) = 1;
  }

  
  std::cout << std::hex << s->pc << std::dec
	    << " : " << getAsmString(inst, s->pc)
	    << " , opcode " << std::hex
	    << opcode
	    << std::dec
	    << " , icnt " << s->icnt
	    << "\n";
  
  s->last_pc = s->pc;  

  uint32_t rd = (inst>>7) & 31;
  riscv_t m(inst);

  int32_t old_gpr[32];
  memcpy(old_gpr, s->gpr, 4*32);

  
  switch(opcode)
    {
    case 0x3: {
      if(m.l.rd != 0) {
	int32_t disp = m.l.imm11_0;
	disp |= ((inst>>31)&1) ? 0xfffff000 : 0x0;
	uint32_t ea = disp + s->gpr[m.l.rs1];
	switch(m.s.sel)
	  {
	  case 0x0: /* lb */
	    s->gpr[m.l.rd] = static_cast<int32_t>(*(reinterpret_cast<int8_t*>(s->mem + ea)));	 
	    break;
	  case 0x1: /* lh */
	    s->gpr[m.l.rd] = static_cast<int32_t>(*(reinterpret_cast<int16_t*>(s->mem + ea)));	 
	    break;
	  case 0x2: /* lw */
	    s->gpr[m.l.rd] = *(reinterpret_cast<int32_t*>(s->mem + ea));
	    break;
	  case 0x4: {/* lbu */
	    uint32_t b = s->mem[ea];
	    *reinterpret_cast<uint32_t*>(&s->gpr[m.l.rd]) = b;
	    break;
	  }
	  case 0x5: { /* lhu */
	    uint16_t b = *reinterpret_cast<uint16_t*>(s->mem + ea);
	    *reinterpret_cast<uint32_t*>(&s->gpr[m.l.rd]) = b;
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
      uint32_t rs1 = (inst >> 15) & 31;
      int32_t simm32 = (inst >> 20);
      simm32 |= ((inst>>31)&1) ? 0xfffff000 : 0x0;
      uint32_t subop =(inst>>12)&7;
      uint32_t shamt = (inst>>20) & 31;

      std::cout << "subop " << subop << "\n";
      if(rd != 0) {
	switch(subop)
	  {
	  case 0: /* addi */
	    s->gpr[rd] = s->gpr[rs1] + simm32;
	    break;
	  case 1: /* slli */
	    s->gpr[rd] = (*reinterpret_cast<uint32_t*>(&s->gpr[rs1])) << shamt;
	    break;
	  case 2: /* slti */
	    s->gpr[rd] = (s->gpr[rs1] < simm32);
	    break;
	  case 3: /* sltiu */
	    s->gpr[rd] = (s->gpr[rs1] < (inst>>20));
	    break;
	  case 4: /* xori */
	    s->gpr[rd] = s->gpr[rs1] ^ simm32;
	    break;
	  case 5: { /* srli & srai */
	    uint32_t sel =  (inst >> 25) & 127;	    
	    if(sel == 0) { /* srli */
	      s->gpr[rd] = (*reinterpret_cast<uint32_t*>(&s->gpr[rs1]) >> shamt);
	    }
	    else if(sel == 32) { /* srai */
	      s->gpr[rd] = s->gpr[rs1] >> shamt;
	    }
	    else {
	      std::cout << "sel = " << sel << "\n";
	      assert(0);
	    }
	    break;
	  }
	  case 6: /* ori */
	    s->gpr[rd] = s->gpr[rs1] | simm32;
	    break;
	  case 7: /* andi */
	    s->gpr[rd] = s->gpr[rs1] & simm32;
	    break;
	    
	  default:
	    std::cout << "implement case " << subop << "\n";
	    assert(false);
	  }
      }
      s->pc += 4;
      break;
    }
    case 0x23: {
      int32_t disp = m.s.imm4_0 | (m.s.imm11_5 << 5);
      disp |= ((inst>>31)&1) ? 0xfffff000 : 0x0;
      uint32_t ea = disp + s->gpr[m.s.rs1];
      std::cout << "STORE EA " << std::hex << ea << std::dec << "\n";      
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
	default:
	  assert(0);
	}
      s->pc += 4;
      break;
    }
      
      //imm[31:12] rd 011 0111 LUI
    case 0x37:
      if(rd != 0) {
	s->gpr[rd] = inst & 0xfffff000;
      }
      s->pc += 4;
      break;
      //imm[31:12] rd 0010111 AUIPC
    case 0x17: /* is this sign extended */
      if(rd != 0) {
	uint32_t imm = inst & (~4095U);
	uint32_t u = static_cast<uint32_t>(s->pc) + imm;
	*reinterpret_cast<uint32_t*>(&s->gpr[rd]) = u;
	//std::cout << "u = " << std::hex << u << std::dec << "\n";
	//if(s->pc == 0x80000084) exit(-1);
      }
      s->pc += 4;
      break;
      
      //imm[11:0] rs1 000 rd 1100111 JALR
    case 0x67: {
      int32_t tgt = m.jj.imm11_0;
      tgt |= ((inst>>31)&1) ? 0xfffff000 : 0x0;
      tgt += s->gpr[m.jj.rs1];
      tgt &= ~(1U);
      if(m.jj.rd != 0) {
	s->gpr[m.jj.rd] = s->pc + 4;
      }
      s->pc = tgt;
      break;
    }

      
      //imm[20|10:1|11|19:12] rd 1101111 JAL
    case 0x6f: {
      int32_t jaddr =
	(m.j.imm10_1 << 1)   |
	(m.j.imm11 << 11)    |
	(m.j.imm19_12 << 12) |
	(m.j.imm20 << 20);
      jaddr |= ((inst>>31)&1) ? 0xffe00000 : 0x0;
      if(rd != 0) {
	s->gpr[rd] = s->pc + 4;
      }
      s->pc += jaddr;
      break;
    }
    case 0x33: {
      if(m.r.rd != 0) {
	switch(m.r.sel)
	  {
	  case 0x0: /* add & sub */
	    if( (inst>>30) & 1 ) { /* sub */
	      s->gpr[m.r.rd] = s->gpr[m.r.rs1] - s->gpr[m.r.rs2];
	    }
	    else { /* add */
	      s->gpr[m.r.rd] = s->gpr[m.r.rs1] + s->gpr[m.r.rs2];
	    }
	    break;
	  case 0x1: /* sll */
	    s->gpr[m.r.rd] = s->gpr[m.r.rs1] << (s->gpr[m.r.rs2] & 31);
	    break;
	  case 0x3: /* sltu */
	    s->gpr[m.r.rd] = *reinterpret_cast<uint32_t*>(&s->gpr[m.r.rs1]) < *reinterpret_cast<uint32_t*>(&s->gpr[m.r.rs2]);
	    break;
	  case 0x4:
	    s->gpr[m.r.rd] = s->gpr[m.r.rs1] ^ s->gpr[m.r.rs2];
	    break;
	  case 0x6:
	    s->gpr[m.r.rd] = s->gpr[m.r.rs1] | s->gpr[m.r.rs2];
	    break;
	  case 0x7:
	    s->gpr[m.r.rd] = s->gpr[m.r.rs1] & s->gpr[m.r.rs2];
	    break;
	  default:
	    std::cout << "implement = " << m.r.sel << "\n";
	    assert(0);
	  }
      }
#if 0
    0000000 rs2 rs1 000 rd 0110011 ADD
    0100000 rs2 rs1 000 rd 0110011 SUB
    0000000 rs2 rs1 001 rd 0110011 SLL
    0000000 rs2 rs1 010 rd 0110011 SLT
    0000000 rs2 rs1 011 rd 0110011 SLTU
    0000000 rs2 rs1 100 rd 0110011 XOR
    0000000 rs2 rs1 101 rd 0110011 SRL
    0100000 rs2 rs1 101 rd 0110011 SRA
    0000000 rs2 rs1 110 rd 0110011 OR
    0000000 rs2 rs1 111 rd 0110011 AND
#endif
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
      int32_t disp =
	(m.b.imm4_1 << 1)  |
	(m.b.imm10_5 << 5) |	
        (m.b.imm11 << 11)  |
        (m.b.imm12 << 12);
      disp |= m.b.imm12 ? 0xffffe000 : 0x0;
      bool takeBranch = false;
      uint32_t u_rs1 = *reinterpret_cast<uint32_t*>(&s->gpr[m.b.rs1]);
      uint32_t u_rs2 = *reinterpret_cast<uint32_t*>(&s->gpr[m.b.rs2]);
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
	  break;
	default:
	  std::cout << "implement case " << m.b.sel << "\n";
	  assert(0);
	}
      //assert(not(takeBranch));
      s->pc = takeBranch ? disp + s->pc : s->pc + 4;
      break;
    }

    case 0x73:
      s->pc += 4;
      break;
    
    default:
      std::cout << "opcode " << std::hex << opcode << std::dec << "\n";
      std::cout << *s << "\n";
      exit(-1);
      break;
    }

  s->icnt++;

  for(int i = 0; i < 32; i++){
    if(old_gpr[i] != s->gpr[i]) {
      std::cout << "\t" << getGPRName(i) << " changed from "
		<< std::hex
		<< old_gpr[i]
		<< " to "
		<< s->gpr[i]
		<< std::dec
		<< "\n";
    }
  }
}
