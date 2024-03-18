#include <cstdio>
#include <iostream>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <cxxabi.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <cstring>
#include <cassert>
#include <map>
#include <fstream>

#include "elf.hh"
#include "helper.hh"
#include "disassemble.hh"
#include "interpret.hh"
#include "globals.hh"

extern const char* githash;

void load_raw(const char* fn, state_t *ms, uint64_t where = 0x80000000);

uint64_t globals::tohost_addr = 0;
uint64_t globals::fromhost_addr = 0;
bool globals::log = false;
std::map<std::string, uint64_t> globals::symtab;
char **globals::sysArgv = nullptr;
int globals::sysArgc = 0;
bool globals::silent = true;
bool globals::fullsim = true;

static state_t *s = nullptr;

void *__dso_handle = nullptr;

int main(int argc, char *argv[]) {
  std::string sysArgs, filename;
  uint64_t maxinsns = ~(0UL), dumpIcnt = ~(0UL);
  bool hash = false, raw = false;
  std::string tohost, fromhost;


  raw = true;
  filename = "bbl.bin";
  
  std::cerr << KGRN
	    << "RISCV64 ISS : built "
	    << __DATE__ << " " << __TIME__  "\n"
	    << "git hash=" << githash
	    << KNRM << "\n";

  
  if(filename.size()==0) {
    std::cerr << argv[0] << ": no file\n";
    return -1;
  }

  /* Build argc and argv */
  int rc = 0;
  s = new state_t;
  initState(s);
  s->maxicnt = maxinsns;

  load_raw(filename.c_str(), s);

  std::cout << "loaded binaries from disk\n";

  runRiscv(s,dumpIcnt);


  std::cerr << KGRN << "INTERP: "
	    << s->icnt << " ins executed, "
	    << KNRM  << "\n";
  std::cerr << "final pc " << std::hex << s->pc << std::dec << "\n";

  

  delete s;

  return 0;
}


