#include "trace.hh"
#include "nway_cache.hh"

#include <array>
#include <cassert>
#include <string>
#include <set>

static const uint64_t ISIDE = 0x1;
static const uint64_t DSIDE = 0x2;
static const uint64_t PGWALK = 0x4;
static const uint64_t BOGUS = 0x4;

constexpr static const std::array<uint64_t,4> TXN_TYPES =
  {ISIDE, DSIDE, PGWALK, BOGUS};

void trace::write_to_disk() {
  fwrite(buf, sizeof(entry), pos, fp);
  pos = 0;  
}

void trace::add(uint64_t vaddr,
		uint64_t paddr,
		int type) {

  if(type != DSIDE) {
    return;
  }
  
  buf[pos].vaddr = vaddr;
  buf[pos].paddr = paddr;
  buf[pos].attr = TXN_TYPES[type & 3];
  
  pos++;
  if(pos == BUFLEN) {
    write_to_disk();
  }
}

void trace::simulate(cache *c) {
  int rc;
  uint64_t n_entries = 0;
  entry e;
  std::set<uint64_t> unique_paddrs;
  rc = fseek(fp, 0, SEEK_END );
  assert(rc == 0);
  auto end = ftell(fp);
  rewind(fp);
  n_entries = end/sizeof(entry);
  printf("%lu transactions in trace\n", n_entries);
  for(uint64_t i = 0; i < n_entries; i++) {
    size_t sz = fread(&e, sizeof(entry), 1, fp);
    assert(sz == 1);
    unique_paddrs.insert(e.paddr & (~15UL));
    c->access(e.paddr, i);
  }
  double hitrate = static_cast<double>(c->get_hits()) /
    c->get_accesses();
  printf("%lu byte cache, %zu way assoc, %lu byte lines, %g hit rate\n",
	 c->get_size(),
	 c->get_assoc(),
	 c->get_line_size(),
	 hitrate);
  printf("%zu unique cachelines\n", unique_paddrs.size());
  printf("%g roofline hitrate\n", static_cast<double>(n_entries-unique_paddrs.size()) / n_entries);
}


#ifdef STANDALONE
#include <boost/program_options.hpp>

int main(int argc, char *argv[]) {
  namespace po = boost::program_options;
  std::string tracename;
  int lg2_cache_lines, cache_ways;
  try {
    po::options_description desc("Options");
    desc.add_options() 
      ("help,h", "Print help messages") 
      ("lg2_cache_lines", po::value<int>(&lg2_cache_lines)->default_value(4), "number of cache lines")
      ("cache_ways", po::value<int>(&cache_ways)->default_value(2), "number of cache ways")
      ("tracename", po::value<std::string>(&tracename), "tracename")
      ; 
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm); 
  }
  catch(po::error &e) {
    std::cerr << "command-line error : " << e.what() << "\n";
    return -1;
  }
  
  trace *t = new trace(tracename.c_str(), true);
  cache *c = nullptr;
  if(cache_ways != 1) {
    c = new nway_cache(cache_ways, lg2_cache_lines);
  }
  else {
    c = new direct_mapped_cache(lg2_cache_lines);
  }
  printf("ways %d, lines %lu -> %lu bytes\n", cache_ways, 1UL<<lg2_cache_lines, c->get_size());
  t->simulate(c);
  
  delete t;
  delete c;
  return 0;
}

#endif
