#ifndef __NAC_HH__
#define __NAC_HH__

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <cassert>

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
  uint64_t get_line_size() const {
    return 1UL<<CL_LEN;
  }
  uint64_t get_hits() const {
    return hits;
  }
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
  virtual void access(addr_t ea,  uint64_t icnt) = 0;
};

class direct_mapped_cache : public cache {
private:
  addr_t *tags;
  uint64_t *first_accessed;
  uint64_t *last_accessed;
public:
  direct_mapped_cache(size_t lg2_lines) : cache(1, lg2_lines)  {
    tags = new addr_t[(1UL<<lg2_lines)];
    first_accessed = new uint64_t[(1UL<<lg2_lines)];
    last_accessed = new uint64_t[(1UL<<lg2_lines)];
    memset(tags, 0, sizeof(addr_t)*(1UL<<lg2_lines));
    memset(first_accessed, 0, sizeof(uint64_t)*(1UL<<lg2_lines));
    memset(last_accessed, 0, sizeof(uint64_t)*(1UL<<lg2_lines));
    
  }
  ~direct_mapped_cache() {
    delete [] tags;
    delete [] first_accessed;
    delete [] last_accessed;
  }
  void access(addr_t ea, uint64_t icnt) override {
    ea &= MASK;
    size_t idx = (ea >> CL_LEN) & ((1U<<lg2_lines)-1);
    accesses++;
    if(tags[idx] == ea) {
      hits++;
      last_accessed[idx] = icnt;
    }
    else {
      tags[idx] = ea;
      dead_time += (icnt-last_accessed[idx]);
      total_time += (icnt-first_accessed[idx]);
      first_accessed[idx] = last_accessed[idx] = icnt;
    }
  }
};

class nway_cache : public cache{
private:
  struct entry {
    entry *next;
    entry *prev;
    addr_t addr;
  };

  struct way {
    size_t ways;    
    entry *entries;
    entry *freelist;  
    entry *lrulist;
    
    way(size_t ways) : entries(nullptr), freelist(nullptr), lrulist(nullptr), ways(ways) {
      entries = new entry[ways];
      memset(entries,0,sizeof(entry)*ways);
      
      entry *c = freelist = &entries[0];
      for(size_t i = 1; i < ways; i++) {
	c->next = &entries[i];
	entries[i].prev = c;
	c = &entries[i];
      }
    }
    ~way() {
      delete [] entries;
    }
    bool access(addr_t ea,  uint64_t icnt) {    
      bool found = false;
      entry *p = lrulist, *l = nullptr;
      while(p) {
	if(p->addr == ea) {
	  found = true;
	  break;
	}
	l = p;
	p = p->next;
      }
      if(not(found)) {
	if(freelist != nullptr) {
	  l = freelist;
	  l->addr = ea;
	  freelist = freelist->next;
	  l->prev = nullptr;
	  l->next = nullptr;
	  if(lrulist == nullptr) {
	    lrulist = l;
	  }
	  else {
	    lrulist->prev = l;
	    l->next = lrulist;
	    lrulist = l;
	  }
	}
	else {
	  assert(l);
	  assert(l->next == nullptr);	
	  /* remove this node */
	  l->prev->next = nullptr;
	  l->prev = nullptr;
	  lrulist->prev = l;
	  l->next = lrulist;
	  lrulist = l;
	  l->addr = ea;
	}
      }
      else {
	if(p != lrulist) {
	  if(p->next) {
	    p->next->prev = p->prev;
	  }
	  if(p->prev) {
	    p->prev->next = p->next;
	  }
	  p->prev = nullptr;
	  p->next = lrulist;
	  lrulist->prev = p;
	  lrulist = p;
	  //print(lrulist);
	}
      }
      return found;
    }
    
    
  };
  
  way **ways;


  
public:
  nway_cache(size_t nways, size_t lg2_lines) : cache(nways, lg2_lines)  {
    ways = new way*[(1UL<<lg2_lines)];
    for(size_t l = 0; l < (1UL<<lg2_lines); l++) {
      ways[l] = new way(nways);
    }
  }
  ~nway_cache() {
    delete [] ways;
  }
  void access(addr_t ea,  uint64_t icnt) override {
    ea &= MASK;
    size_t idx = (ea >> CL_LEN) & ((1U<<lg2_lines)-1);
    accesses++;
    assert(idx < ( (1UL<<lg2_lines) ));
    bool h = ways[idx]->access(ea, icnt);
    if(h) {
      hits++;
    }
  }
};

static inline cache* make_cache(int n_ways, int lg2_lines) {
  if(n_ways == 1) {
    return new direct_mapped_cache(lg2_lines);
  } 
  return new nway_cache(n_ways, lg2_lines);
}

#endif
