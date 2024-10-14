#ifndef __NAC_HH__
#define __NAC_HH__

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <cassert>
#include <map>



class cache {
protected:
  static const uint32_t CL_LEN = 4;
  static const uint32_t MASK = ~((1U<<CL_LEN)-1);
  typedef uint64_t addr_t;
  size_t nways,lg2_lines;
  uint64_t hits, accesses;
  uint64_t dead_time,total_time;
public:
  cache(size_t nways, size_t lg2_lines) :
    nways(nways), lg2_lines(lg2_lines),
    hits(0), accesses(0),
    dead_time(0), total_time(0) {}

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
  
  virtual ~cache() {}
  virtual void access(addr_t ea,  uint64_t icnt, uint64_t pc) = 0;
};

class direct_mapped_cache : public cache {
private:
  uint32_t vb_x;
  uint32_t lg_vb_sz;
  addr_t *tags;
  addr_t *vb_tags;
  uint64_t *cnts;
  uint64_t *first_accessed;
  uint64_t *last_accessed;
public:
  direct_mapped_cache(size_t lg2_lines); 
  ~direct_mapped_cache();
  uint64_t get_mru_hits() const override {
    return hits;
  }
  void access(addr_t ea, uint64_t icnt, uint64_t pc) override ;
};


class nway_cache : public cache{
private:
  struct way {
    struct entry {
      entry *next;
      entry *prev;
      addr_t addr;
    };
    size_t ways;    
    entry *entries;
    entry *freelist;  
    entry *lrulist;
    way(size_t ways);
    bool access(addr_t ea,  uint64_t icnt, bool &mru);
    ~way() {
      delete [] entries;
    }
  };
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
