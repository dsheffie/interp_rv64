#ifndef __GLOBALSH__
#define __GLOBALSH__

namespace globals {
  extern uint32_t tohost_addr;
  extern uint32_t fromhost_addr;
  extern bool enClockFuncts;
  extern int sysArgc;
  extern char **sysArgv;
  extern bool isMipsEL;
  extern uint64_t icountMIPS;
  extern bool silent;
  extern std::map<uint32_t, uint64_t> execHisto;
  extern std::map<std::string, uint32_t> symtab;
};

#endif
