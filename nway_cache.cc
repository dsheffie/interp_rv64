#include "nway_cache.hh"

direct_mapped_cache::direct_mapped_cache(size_t lg2_lines) :
  cache(1, lg2_lines), vb_x(1), lg_vb_sz(6)  {
  tags = new addr_t[(1UL<<lg2_lines)];
  cnts = new uint64_t[(1UL<<lg2_lines)];
  vb_tags = new addr_t[1UL<<lg_vb_sz];
  memset(vb_tags, 0, sizeof(addr_t)*(1UL<<lg_vb_sz));
  memset(cnts, 0, sizeof(uint64_t)*(1UL<<lg2_lines));
  first_accessed = new uint64_t[(1UL<<lg2_lines)];
  last_accessed = new uint64_t[(1UL<<lg2_lines)];
  memset(tags, 0, sizeof(addr_t)*(1UL<<lg2_lines));
  memset(first_accessed, 0, sizeof(uint64_t)*(1UL<<lg2_lines));
  memset(last_accessed, 0, sizeof(uint64_t)*(1UL<<lg2_lines));
  
}
direct_mapped_cache::~direct_mapped_cache() {
  delete [] tags;
  delete [] cnts;
  delete [] vb_tags;
  delete [] first_accessed;
  delete [] last_accessed;
}

void direct_mapped_cache::access(addr_t ea, uint64_t icnt, uint64_t pc) {
    ea &= MASK;
    size_t idx = (ea >> CL_LEN) & ((1U<<lg2_lines)-1);
    accesses++;
    cnts[idx]++;

    conflicts[idx][ea & (~(get_line_size()-1))]++;
    
    if(tags[idx] == ea) {
      hits++;
      last_accessed[idx] = icnt;
    }
    else {
      /* save addr in victim buffer */
      vb_tags[vb_x & ((1U<<lg_vb_sz)-1)] = tags[idx];

      bool found_in_vb = false;
      for(uint32_t i = 0; i < (1U<<lg_vb_sz); i++) {
	if(vb_tags[i] == ea) {
	  found_in_vb = true;
	  break;
	}
      }
      if(found_in_vb) hits++;
      tags[idx] = ea;
	
      vb_x ^= vb_x << 13;
      vb_x ^= vb_x >> 17;
      vb_x ^= vb_x << 5;
      
      dead_time += (icnt-last_accessed[idx]);
      total_time += (icnt-first_accessed[idx]);
      first_accessed[idx] = last_accessed[idx] = icnt;
    }
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
	   
  if(h) {
    hits++;
    if(mru) hit_mru++;
  }
}


nway_cache::way::way(size_t ways) :
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

bool nway_cache::way::access(addr_t ea,  uint64_t icnt, bool &mru) {
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
