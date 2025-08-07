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
  virtual void access(addr_t ea,  uint64_t icnt, uint64_t pc, bool wr=false) = 0;
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
  void access(addr_t ea, uint64_t icnt, uint64_t pc, bool wr=false) override ;
};

class store_to_load_tracker : public cache {
private:
  std::map<uint64_t, std::pair<uint64_t, uint64_t> > last_write_map;
  std::map<uint64_t, uint64_t> histo;
public:
  store_to_load_tracker(); 
  ~store_to_load_tracker();
  uint64_t get_mru_hits() const override {
    return 0;
  }
  void access(addr_t ea, uint64_t icnt, uint64_t pc, bool wr=false) override ;
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
  void access(addr_t ea,  uint64_t icnt, uint64_t pc, bool wr=false) override;
};

static inline cache* make_cache(int n_ways, int lg2_lines) {
  if(n_ways == 1) {
    return new direct_mapped_cache(lg2_lines);
  } 
  return new nway_cache(n_ways, lg2_lines);
}

#endif
