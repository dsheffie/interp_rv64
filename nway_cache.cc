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

#include "nway_cache.hh"
#include <ostream>
#include <fstream>

direct_mapped_cache::direct_mapped_cache(size_t lg2_lines) :
  cache(1, lg2_lines)  {
  tags = new addr_t[(1UL<<lg2_lines)];
  memset(tags, 0, sizeof(addr_t)*(1UL<<lg2_lines));
  
}
direct_mapped_cache::~direct_mapped_cache() {
  delete [] tags;
}

void direct_mapped_cache::access(addr_t ea, uint64_t icnt, uint64_t pc) {
  ea &= MASK;
  size_t idx = (ea >> CL_LEN) & ((1U<<lg2_lines)-1);
  accesses++;
  access_distribution[idx]++;
  
  if(tags[idx] == ea) {
    hits++;
  }

}

tlb::tlb(size_t entries) : entries(entries), hits(0), accesses(0) {}

void tlb::add(uint64_t page, uint64_t mask) {
  //printf("adding page %lx with mask %lx\n", page, mask);
  if(lru.size() == entries) {
    lru.pop_back();
  }
  lru.emplace_front(page, mask);
}

bool tlb::access(uint64_t ea) {
  auto it = lru.begin(), E = lru.end();
  bool found = false;
  accesses++;
  while(it != E) {
    uint64_t mask = it->second, page = it->first;
    if((ea & (~mask)) == page) {
      found = true;
      lru.erase(it);
      lru.emplace_front(page, mask);
      hits++;
      break;
    }
    it++;
  }
  return found;
}

nway_cache::nway_cache(size_t nways, size_t lg2_lines) :
  cache(nways, lg2_lines), hit_mru(0)  {
    ways = new way*[(1UL<<lg2_lines)];
    for(size_t l = 0; l < (1UL<<lg2_lines); l++) {
      ways[l] = new way(nways);
    }
}

void nway_cache::access(addr_t ea,  uint64_t icnt, uint64_t pc) {
  ea &= MASK;
  size_t idx = (ea >> CL_LEN) & ((1U<<lg2_lines)-1);
  accesses++;
  assert(idx < ( (1UL<<lg2_lines) ));
  bool mru = false;
  bool h = ways[idx]->access(ea, icnt, mru);
  //printf("search for addr %lx, pc %lx was hit %d\n", ea, pc, h);
  access_distribution[idx]++;	   
  if(h) {
    hits++;
    if(mru) hit_mru++;
  }
}


way::way(size_t ways) :
  entries(nullptr), freelist(nullptr), lrulist(nullptr), ways(ways) {
  entries = new entry[ways];
  memset(entries,0,sizeof(entry)*ways);
  
  entry *c = freelist = &entries[0];
  for(size_t i = 1; i < ways; i++) {
    c->next = &entries[i];
    entries[i].prev = c;
    c = &entries[i];
  }
}

bool way::access(uint64_t ea,  uint64_t icnt, bool &mru) {
  bool found = false;
  entry *p = lrulist, *l = nullptr;
  mru = false;      
  while(p) {
    if(p->addr == ea) {
      if(p == lrulist) {
	mru = true;
      }
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


void cache::dump_set_distribution() const {
  std::ofstream out("dcache_sets.csv");
  for(size_t i = 0; i < (1UL<<lg2_lines); i++) {
    out << i << "," << access_distribution[i] << "\n";
  }
}
