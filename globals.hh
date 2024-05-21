#ifndef __GLOBALSH__
#define __GLOBALSH__

namespace globals {
  extern uint64_t tohost_addr;
  extern uint64_t fromhost_addr;
  extern int sysArgc;
  extern char **sysArgv;
  extern bool silent;
  extern bool log;
  extern bool fullsim;
  extern bool uart;
  extern std::map<std::string, uint64_t> symtab;
};

#endif
