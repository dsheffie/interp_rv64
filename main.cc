#include <cstdio>
#include <iostream>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <cxxabi.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <cstring>
#include <cassert>
#include <map>
#include <fstream>
#include <boost/program_options.hpp>

#include "elf.hh"
#include "helper.hh"
#include "disassemble.hh"
#include "interpret.hh"
#include "saveState.hh"
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

static state_t *s = nullptr;


static int buildArgcArgv(const char *filename, const std::string &sysArgs, char **&argv){
  int cnt = 0;
  std::vector<std::string> args;
  char **largs = 0;
  args.push_back(std::string(filename));

  char *ptr = nullptr;
  char *c_str = strdup(sysArgs.c_str());
  if(sysArgs.size() != 0)
    ptr = strtok(c_str, " ");

  while(ptr && (cnt<MARGS)) {
    args.push_back(std::string(ptr));
    ptr = strtok(nullptr, " ");
    cnt++;
  }
  largs = new char*[args.size()];
  for(size_t i = 0; i < args.size(); i++) {
    const std::string & s = args[i];
    size_t l = strlen(s.c_str());
    largs[i] = new char[l+1];
    memset(largs[i],0,sizeof(char)*(l+1));
    memcpy(largs[i],s.c_str(),sizeof(char)*l);
  }
  argv = largs;
  free(c_str);
  return (int)args.size();
}

int main(int argc, char *argv[]) {
  namespace po = boost::program_options; 
  size_t pgSize = getpagesize();
  std::string sysArgs, filename;
  uint64_t maxinsns = ~(0UL), dumpIcnt = ~(0UL);
  bool hash = false, raw = false;

  try {
    po::options_description desc("Options");
    desc.add_options() 
      ("help,h", "Print help messages") 
      ("args,a", po::value<std::string>(&sysArgs), "arguments to riscv binary") 
      ("hash", po::value<bool>(&hash)->default_value(false), "hash memory at end of execution")
      ("file,f", po::value<std::string>(&filename), "rv32 binary")
      ("max,m", po::value<uint64_t>(&maxinsns)->default_value(~(0UL)), "max instructions to execute")
      ("dump", po::value<uint64_t>(&dumpIcnt)->default_value(~(0UL)), "dump after n instructions")
      ("silent,s", po::value<bool>(&globals::silent)->default_value(true), "no interpret messages")
      ("log,l", po::value<bool>(&globals::log)->default_value(false), "log instructions")
      ("raw,r", po::value<bool>(&raw)->default_value(false), "load raw binary")
      ; 
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm); 
  }
  catch(po::error &e) {
    std::cerr << KRED << "command-line error : " << e.what() << KNRM << "\n";
    return -1;
  }

  
  if(not(globals::silent)) {
    std::cerr << KGRN
	      << "RISCV64 ISS : built "
	      << __DATE__ << " " << __TIME__
	      << ",pid="<< getpid() << "\n"
	      << "git hash=" << githash
	      << KNRM << "\n";
  }
  
  if(filename.size()==0) {
    std::cerr << argv[0] << ": no file\n";
    return -1;
  }

  /* Build argc and argv */
  globals::sysArgc = buildArgcArgv(filename.c_str(),sysArgs,globals::sysArgv);

  int rc = posix_memalign((void**)&s, pgSize, pgSize); 
  initState(s);
  s->maxicnt = maxinsns;
#ifdef __linux__
  void* mempt = mmap(nullptr, 1UL<<32, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
#else
  void* mempt = mmap(nullptr, 1UL<<32, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS , -1, 0);
#endif
  assert(mempt != reinterpret_cast<void*>(-1));
  assert(madvise(mempt, 1UL<<32, MADV_DONTNEED)==0);
  s->mem = reinterpret_cast<uint8_t*>(mempt);
  if(s->mem == nullptr) {
    std::cerr << "INTERP : couldn't allocate backing memory!\n";
    exit(-1);
  }
  if(raw) {
    load_raw(filename.c_str(), s);
  }
  else {
    load_elf(filename.c_str(), s);
  }
  initCapstone();

  double runtime = timestamp();
  runRiscv(s,dumpIcnt);
  runtime = timestamp()-runtime;

  if( s->icnt >= dumpIcnt ) {
    std::stringstream ss;
    ss << filename << s->icnt << ".bin";
    if(not(globals::silent)) {
      std::cout << "dumping at icnt " << s->icnt << "\n";
    }
    dumpState(*s, ss.str());
  }
  
  if(hash) {
    std::fflush(nullptr);
    /* std::cerr << *s << "\n"; */
    std::cerr << "crc32=" << std::hex
	      << crc32(s->mem, 1UL<<32)<<std::dec
	      << "\n";
  }  


  if(not(globals::silent)) {
    std::cerr << KGRN << "INTERP: "
	      << runtime << " sec, "
	      << s->icnt << " ins executed, "
	      << std::round((s->icnt/runtime)*1e-6) << " megains / sec "
	      << KNRM  << "\n";
  }

  
  munmap(mempt, 1UL<<32);
  if(globals::sysArgv) {
    for(int i = 0; i < globals::sysArgc; i++) {
      delete [] globals::sysArgv[i];
    }
    delete [] globals::sysArgv;
  }
  free(s);
  stopCapstone();

  return 0;
}


