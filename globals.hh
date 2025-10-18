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

#ifndef __GLOBALSH__
#define __GLOBALSH__

struct trace;
struct branch_trace;
struct branch_predictor;
#include <iostream>

namespace globals {
  extern uint64_t tlb_accesses;
  extern uint64_t tlb_hits;  
  extern uint64_t tohost_addr;
  extern uint64_t fromhost_addr;
  extern int sysArgc;
  extern char **sysArgv;
  extern bool silent;
  extern bool log;
  extern bool fullsim;
  extern bool interactive;
  extern bool fdt_uart;
  extern bool svnapot;
  extern uint64_t fdt_ram_size;
  extern uint64_t ram_phys_start;
  extern uint64_t fdt_addr;
  extern uint64_t fw_start_addr;
  extern uint32_t cpu_freq;
  extern trace *tracer;
  extern branch_trace *branch_tracer;
  extern std::map<std::string, uint64_t> symtab;
  extern std::ofstream *console_log;
  extern branch_predictor *bpred;
  extern bool enable_zbb;
  extern std::map<uint64_t, std::map<uint64_t, uint64_t>> insn_histo;
  extern bool extract_kernel;
  extern int disk_fd;
  extern bool hacky_fp32;
  extern int disk_fd;
  extern uint64_t disk_rd_bytes;
  extern uint64_t disk_wr_bytes;
  extern uint64_t fb_phys_addr;
};

#endif
