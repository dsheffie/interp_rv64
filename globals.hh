#ifndef __GLOBALSH__
#define __GLOBALSH__

struct trace;
struct branch_trace;

namespace globals {
  extern uint64_t tohost_addr;
  extern uint64_t fromhost_addr;
  extern int sysArgc;
  extern char **sysArgv;
  extern bool silent;
  extern bool log;
  extern bool fullsim;
  extern bool interactive;
  extern bool fdt_uart;
  extern uint64_t fdt_ram_size;
  extern uint64_t ram_phys_start;
  extern uint64_t fw_start_addr;
  extern trace *tracer;
  extern branch_trace *branch_tracer;
  extern std::map<std::string, uint64_t> symtab;
};

#endif
