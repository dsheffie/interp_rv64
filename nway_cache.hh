#ifndef __NAC_HH__
#define __NAC_HH__

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <cassert>
#include <map>
#include <list>


class cache {
protected:
  static const uint32_t CL_LEN = 4;
  static const uint32_t MASK = ~((1U<<CL_LEN)-1);
  typedef uint64_t addr_t;
  size_t nways,lg2_lines;
  uint64_t hits, accesses;
  uint64_t dead_time,total_time;
  uint64_t *access_distribution;
public:
  cache(size_t nways, size_t lg2_lines) :
    nways(nways), lg2_lines(lg2_lines),
    hits(0), accesses(0),
    dead_time(0), total_time(0) {
    access_distribution = new uint64_t[1UL<<lg2_lines];
    memset(access_distribution, 0, sizeof(uint64_t)*(1UL<<lg2_lines));
  }
  void dump_set_distribution() const;
  size_t get_assoc() const {
    return nways;
  }
  constexpr uint64_t get_line_size() const {
    return 1UL<<CL_LEN;
  }
  uint64_t get_hits() const {
    return hits;
  }
  virtual uint64_t get_mru_hits() const = 0;
  uint64_t get_accesses() const {
    return accesses;
  }
  uint64_t get_size() const {
    return (1UL<<lg2_lines) * nways * ((~MASK)+1);
  }
  uint64_t get_total_time() const {
    return total_time;
  }
  uint64_t get_dead_time() const {
    return dead_time;
  }
  uint64_t get_live_time() const {
    return total_time-dead_time;
  }
  
  virtual ~cache() {
    delete [] access_distribution;
  }
  virtual void access(addr_t ea,  uint64_t icnt, uint64_t pc) = 0;
};

class direct_mapped_cache : public cache {
private:
  addr_t *tags;
public:
  direct_mapped_cache(size_t lg2_lines); 
  ~direct_mapped_cache();
  uint64_t get_mru_hits() const override {
    return hits;
  }
  void access(addr_t ea, uint64_t icnt, uint64_t pc) override ;
};

struct way {
  struct entry {
    entry *next;
    entry *prev;
    uint64_t addr;
  };
  size_t ways;    
  entry *entries;
  entry *freelist;  
  entry *lrulist;
  way(size_t ways);
  bool access(uint64_t ea,  uint64_t icnt, bool &mru);
  ~way() {
    delete [] entries;
  }
};

class tlb {
  std::list<std::pair<uint64_t, uint64_t>> lru;
  uint64_t entries, hits, accesses;
public:
  tlb(size_t entries);
  ~tlb() {}
  uint64_t get_entries() const {
    return entries;
  }
  uint64_t get_hits() const {
    return hits;
  }
  uint64_t get_accesses() const {
    return accesses;
  }
  bool access(uint64_t ea);
  void add(uint64_t page, uint64_t mask);
};

class nway_cache : public cache{
private:
 
  uint64_t hit_mru;
  way **ways;
  
public:
  nway_cache(size_t nways, size_t lg2_lines);
  ~nway_cache() {
    delete [] ways;
  }
  uint64_t get_mru_hits() const override {
    return hit_mru;
  }  
  void access(addr_t ea,  uint64_t icnt, uint64_t pc) override;
};

static inline cache* make_cache(int n_ways, int lg2_lines) {
  if(n_ways == 1) {
    return new direct_mapped_cache(lg2_lines);
  } 
  return new nway_cache(n_ways, lg2_lines);
}

#endif
