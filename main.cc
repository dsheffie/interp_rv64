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
#include "virtio.hh"
#include "uart.hh"
#include "trace.hh"
#include "branch_predictor.hh"

extern const char* githash;

void load_raw(const char* fn, state_t *ms);

uint64_t globals::tlb_accesses = 0;
uint64_t globals::tlb_hits = 0;
uint64_t globals::tohost_addr = 0;
uint64_t globals::fromhost_addr = 0;
bool globals::log = false;
std::map<std::string, uint64_t> globals::symtab;
char **globals::sysArgv = nullptr;
int globals::sysArgc = 0;
bool globals::silent = true;
bool globals::fullsim = true;
bool globals::fdt_uart = false;
bool globals::interactive = false;
bool globals::svnapot = true;
uint64_t globals::fdt_ram_size = 1UL<<24;
uint64_t globals::fw_start_addr = 1UL<<21;
uint64_t globals::fdt_addr = (1UL<<16) + 64;
uint64_t globals::ram_phys_start = 0x0;
trace* globals::tracer = nullptr;
branch_trace* globals::branch_tracer = nullptr;
std::ofstream* globals::console_log = nullptr;
branch_predictor *globals::bpred = nullptr;
bool globals::extract_kernel = false;
bool globals::enable_zbb = true;
std::map<uint64_t, std::map<uint64_t, uint64_t>> globals::insn_histo;

static state_t *s = nullptr;
static double starttime = 0.0;

static const uint64_t disk_addr = (384+32)*1024UL*1024UL;

void catchUnixSignal(int n) {
  if(s) {
    double runtime = timestamp()-starttime;
    double mips = (static_cast<double>(s->icnt)/runtime)*1e-6;
    std::cout << "caught SIGINT at icnt "
	      << s->icnt << "\n";
    printf("loads = %ld\n", s->loads);
    printf("match = %ld\n", s->va_track_pa);
    printf("simulator mips = %g\n", mips);
    if(globals::bpred) {
      uint64_t n_br = 0, n_mis=0, n_inst=0;      
      globals::bpred->get_stats(n_br,n_mis,n_inst);
      printf("bpu %g mpki\n", 1000.0*(static_cast<double>(n_mis) / n_inst));
    }
    // uint8_t *buf = &s->mem[disk_addr];
    // int fd = ::open("disk.img", O_RDWR|O_CREAT|O_TRUNC, 0600);
    // write(fd, buf, 16*1024*1024);
    // close(fd);        
  }
  exit(-1);
}

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
  std::string sysArgs, filename, tracename;
  uint64_t maxinsns = ~(0UL), dumpIcnt = ~(0UL);
  bool raw = false, load_dump = false, take_checkpoints = false;
  std::string tohost, fromhost;
  int lg2_icache_lines, lg2_dcache_lines;
  int icache_ways, dcache_ways, dtlb_entries;
  uint64_t init_icnt = 0;
  try {
    po::options_description desc("Options");
    desc.add_options() 
      ("help,h", "Print help messages") 
      ("args,a", po::value<std::string>(&sysArgs), "arguments to riscv binary") 
      ("file,f", po::value<std::string>(&filename), "rv32 binary")
      ("maxicnt,m", po::value<uint64_t>(&maxinsns)->default_value(~(0UL)), "max instructions to execute")
      ("dump", po::value<uint64_t>(&dumpIcnt)->default_value(~(0UL)), "dump after n instructions")
      ("fullsim", po::value<bool>(&globals::fullsim)->default_value(true), "full ssystem simulation")
      ("checkpoints", po::value<bool>(&take_checkpoints)->default_value(false), "take checkpoints at dump icnt internal")
      ("silent,s", po::value<bool>(&globals::silent)->default_value(true), "no interpret messages")
      ("load_dump", po::value<bool>(&load_dump)->default_value(false), "load a dump")
      ("log,l", po::value<bool>(&globals::log)->default_value(false), "log instructions")
      ("raw,r", po::value<bool>(&raw)->default_value(false), "load raw binary")
      ("tohost", po::value<std::string>(&tohost)->default_value("0"), "to host address")
      ("romhost", po::value<std::string>(&fromhost)->default_value("0"), "from host address")
      ("uart", po::value<bool>(&globals::fdt_uart)->default_value(false), "enable uart in fdt")
      ("ram_size", po::value<uint64_t>(&globals::fdt_ram_size)->default_value(1UL<<30), "fdt ram size")
      ("phys_start", po::value<uint64_t>(&globals::ram_phys_start)->default_value(1UL<<21), "start address for physical memory")
      ("interactive", po::value<bool>(&globals::interactive)->default_value(false), "interactive shell")
      ("lg2_icache_lines", po::value<int>(&lg2_icache_lines)->default_value(0), "number of icache lines")
      ("lg2_dcache_lines", po::value<int>(&lg2_dcache_lines)->default_value(0), "number of dcache lines")
      ("dtlb_entries", po::value<int>(&dtlb_entries)->default_value(0), "number of dtlb entries")
      ("icache_ways", po::value<int>(&icache_ways)->default_value(1), "number of icache ways")
      ("dcache_ways", po::value<int>(&dcache_ways)->default_value(1), "number of dcache ways")
      ("tracename", po::value<std::string>(&tracename), "tracename")
      ("svnapot", po::value<bool>(&globals::svnapot)->default_value(true), "enable svnapot")
      ("extract_kernel,k", po::value<bool>(&globals::extract_kernel)->default_value(false), "extract kernel.bin")
      ; 
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm); 
  }
  catch(po::error &e) {
    std::cerr << KRED << "command-line error : " << e.what() << KNRM << "\n";
    return -1;
  }
  
  if(filename.size()==0) {
    std::cerr << argv[0] << ": no file\n";
    return -1;
  }

  /* Build argc and argv */
  globals::sysArgc = buildArgcArgv(filename.c_str(),sysArgs,globals::sysArgv);

  int rc = posix_memalign((void**)&s, pgSize, pgSize); 
  initState(s);
  if(globals::extract_kernel) {
    s->maxicnt = maxinsns = 0;
  }
  else {
    s->maxicnt = maxinsns;
  }
  
  if(lg2_dcache_lines != 0) {
    s->dcache = make_cache(dcache_ways, lg2_dcache_lines);
  }
  if(lg2_icache_lines != 0) {
    s->icache = make_cache(icache_ways, lg2_icache_lines);
  }

  if(dtlb_entries) {
    s->dtlb = new tlb(dtlb_entries);
  }
  
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

  bool fileIsDump = false;
  if(not(filename.empty())) {
    fileIsDump = isDump(filename);
  }

  //globals::bpred = new gshare(s->icnt, 16);
  
  if(s->mem == nullptr) {
    std::cerr << "INTERP : couldn't allocate backing memory!\n";
    exit(-1);
  }
  if(raw) {
    load_raw(filename.c_str(), s);
    globals::tohost_addr = strtol(tohost.c_str(), nullptr, 16);
    globals::fromhost_addr = strtol(fromhost.c_str(), nullptr, 16);
  }
  else if(load_dump or fileIsDump) {
    loadState(*s, filename.c_str());
    globals::tohost_addr = strtol(tohost.c_str(), nullptr, 16);
    globals::fromhost_addr = strtol(fromhost.c_str(), nullptr, 16);
    if(s->maxicnt != (~(0UL))) {
      s->maxicnt += s->icnt;      
    }
    init_icnt = s->icnt;
  }
  else {
    load_elf(filename.c_str(), s);
  }
  initCapstone();

  if(not(tracename.empty())) {
    globals::tracer = new trace(tracename);
  }

  // if(0) { /* load initial disk image */
  //   struct stat st;
  //   int fd = open("diskimage.img", O_RDONLY);
  //   fstat(fd, &st);
  //   char * buf = (char*)mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  //   memcpy(&s->mem[disk_addr], buf, st.st_size);
  //   munmap(buf, st.st_size);
  //   close(fd);
  // }
  
  s->va_track_pa = s->loads = 0;
  
  //globals::branch_tracer = new branch_trace("branches.trc");
  signal(SIGINT, catchUnixSignal);

  starttime = timestamp();
  if(take_checkpoints) {
    while((s->icnt < s->maxicnt) and not(s->brk)) {
      bool take_cp = ((s->icnt % dumpIcnt) == 0);
      if((s->icnt % dumpIcnt) == 0) {
	std::stringstream ss;
	ss << filename << s->icnt << ".rv64.chpt";
	//std::cout << "dumping at icnt " << s->icnt << "\n";
	dumpState(*s, ss.str());
      }
      execRiscv(s);
    }
  }
  else if(not(globals::interactive)) {
    if(dumpIcnt != 0) {
      runRiscv(s,dumpIcnt);
    }
    if( s->icnt >= dumpIcnt ) {
      std::stringstream ss;
      ss << filename << s->icnt << ".rv64.chpt";
      if(not(globals::silent)) {
	std::cout << "dumping at icnt " << s->icnt << "\n";
      }
      std::cout << "creating dump at priv " << s->priv << "\n";
      dumpState(*s, ss.str());
    }
  }
  else {
    runInteractiveRiscv(s);
  }
  double runtime = timestamp()-starttime;

  if(not(globals::silent)) {
    std::cerr << KGRN << "INTERP: "
	      << runtime << " sec, "
	      << s->icnt << " ins executed, "
	      << std::round((s->icnt/runtime)*1e-6) << " mips "
	      << KNRM  << "\n";
    std::cerr << "final pc " << std::hex << s->pc << std::dec << "\n";
    for(int i = 0; i < 3; i++) {
      if((s->ipgszcnt[i] == 0) and (s->dpgszcnt[i]==0))
	continue;
      uint64_t pgsz = 0;
      switch(i)
	{
	case 0:
	  pgsz = 1UL<<30;
	  break;
	case 1:
	  pgsz = 1UL<<21;
	  break;
	case 2:
	  pgsz = 1UL<<12;
	  break;
	case 3:
	  pgsz = 1UL<<16;
	  break;
	}
      printf("%lu pg sizes : iside %lu accesses, dside %lu accesses\n",
	     pgsz,
	     s->ipgszcnt[i],
	     s->dpgszcnt[i]
	     );
      std::cout << "tlb_accesses = " << globals::tlb_accesses << "\n";
      std::cout << "tlb_hits     = " << globals::tlb_hits << "\n";
    }
    if(globals::bpred) {
      uint64_t n_br = 0, n_mis=0, n_inst=0;      
      globals::bpred->get_stats(n_br,n_mis,n_inst);
      printf("bpu %g mpki\n", 1000.0*(static_cast<double>(n_mis) / n_inst));
    }    
  }

  // {
  //   uint8_t *buf = &s->mem[disk_addr];
  //   int fd = ::open("disk.img", O_RDWR|O_CREAT|O_TRUNC, 0600);
  //   write(fd, buf, 16*1024*1024);
  //   close(fd);
  // }
  
  munmap(mempt, 1UL<<32);
  if(globals::sysArgv) {
    for(int i = 0; i < globals::sysArgc; i++) {
      delete [] globals::sysArgv[i];
    }
    delete [] globals::sysArgv;
  }
  if(s->dcache) {
    double mpki = s->dcache->get_accesses() -
      s->dcache->get_hits();
    mpki *= (1000.0 / (s->icnt - init_icnt));
    printf("dcache: %lu bytes, %lu hits, %lu total, %g mpki\n",
	   s->dcache->get_size(),
	   s->dcache->get_hits(),
	   s->dcache->get_accesses(),
	   mpki);
    delete s->dcache;
  }
  if(s->icache) {
    double mpki = s->icache->get_accesses() -
      s->icache->get_hits();
    mpki *= (1000.0 / (s->icnt - init_icnt));
    printf("icache: %lu bytes, %lu hits, %lu total, %g mpki\n",
	   s->icache->get_size(),
	   s->icache->get_hits(),
	   s->icache->get_accesses(),
	   mpki);
    
    delete s->icache;
  }
  if(s->dtlb) {
    double mpki = s->dtlb->get_accesses() -
      s->dtlb->get_hits();
    mpki *= (1000.0 / (s->icnt - init_icnt));
    std::cout << "dtlb: " << s->dtlb->get_entries() << " entries, " << s->dtlb->get_hits() << " hits, " << s->dtlb->get_accesses() << " total, "
	      << static_cast<double>(s->dtlb->get_hits()) / static_cast<double>(s->dtlb->get_accesses()) << " hit rate,"
	      << mpki << " mpki\n";
    delete s->dtlb;
  }
  if(s->vio) {
    delete s->vio;
  }
  if(s->u8250) {
    delete s->u8250;
  }
  free(s);
  stopCapstone();

  if(globals::tracer) {
    delete globals::tracer;
  }
  if(globals::branch_tracer) {
    delete globals::branch_tracer;
  }
  if(globals::bpred) {
    delete globals::bpred;
  }

  for(const auto &p : globals::insn_histo) {
    const auto &m = p.second;
    uint64_t cnt = 0;
    std::stringstream ss;
    ss << "region-" << std::hex << p.first
       << std::dec << ".txt";
    std::ofstream hh(ss.str());
    for(const auto &e : p.second) {
      hh << std::hex << e.first << std::dec
	 << "," << e.second << "\n";
      cnt += e.second;
    }
    std::cout << "pgtbl "
	      << std::hex
	      << p.first
	      << std::dec
	      << ", " << cnt << "\n";
  }
  
  return 0;
}


