#ifndef __INTERPRET_HH__
#define __INTERPRET_HH__

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <ostream>

#define MARGS 20

struct state_t{
  uint32_t pc;
  uint32_t last_pc;
  int32_t gpr[32];
  uint8_t *mem;
  uint8_t brk;
  uint8_t bad_addr;
  uint32_t epc;
  uint64_t maxicnt;
  uint64_t icnt;
};

static inline int32_t checksum_gprs(const state_t *s) {
  int32_t h = 0;
  for(int i = 0; i < 32; i++) {
    h ^= s->gpr[i];
  }
  return h;
}


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

union riscv_t {
  rtype_t r;
  itype_t i;
  utype_t u;
  jal_t j;
  jalr_t jj;
  branch_t b;
  store_t s;
  load_t l;
  uint32_t raw;
  riscv_t(uint32_t x) : raw(x) {}
};

void initState(state_t *s);
void execRiscv(state_t *s);

void mkMonitorVectors(state_t *s);
std::ostream &operator<<(std::ostream &out, const state_t & s);
#endif
