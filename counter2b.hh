#ifndef __counter2b_hh__
#define __counter2b_hh__

#include <cstdint>
#include <cassert>
#include "sim_bitvec.hh"

class twobit_counter_array {
private:
  struct entry {
    uint8_t x : 2;
    uint8_t y : 2;
    uint8_t z : 2;
    uint8_t w : 2;
  };
  uint64_t n_entries, n_elems;
  entry *arr = nullptr;
  sim_bitvec valid;
public:
  twobit_counter_array(uint64_t n_entries) :
    n_entries(n_entries) {
    n_elems = (n_entries + 3) / 4;
    arr = new entry[n_elems];
    for(uint64_t i = 0; i < n_elems; i++) {
      /* initialize as weakly not-taken */
      arr[i].x = arr[i].y = arr[i].z = arr[i].w = 1;
    }
    valid.clear_and_resize(n_entries);
  }
  ~twobit_counter_array() {
    delete [] arr;
  }
  uint8_t get_value(uint64_t idx) const {
    assert(idx < n_entries);
    uint8_t v = 0;
    switch(idx&0x3)
      {
      case 0:
	v = arr[idx/4].x;
	break;
      case 1:
	v = arr[idx/4].y;
	break;
      case 2:
	v = arr[idx/4].z;
	break;
      case 3:
	v = arr[idx/4].w;
	break;
      }
    return v;
  }
  void update(uint64_t idx, bool incr) {
    assert(idx < n_entries);
    valid.set_bit(idx);
    uint8_t v = get_value(idx);
    if(incr) {
      v = (v > 2) ? v : (v+1);
    }
    else {
      v = (v==0) ? v : (v-1);
    }
    switch(idx&0x3)
      {
      case 0:
        arr[idx/4].x = v;
	break;
      case 1:
	arr[idx/4].y = v;
	break;
      case 2:
	arr[idx/4].z = v;
	break;
      case 3:
	arr[idx/4].w = v;
	break;
      }
  }
  uint64_t get_nentries() const {
    return n_entries;
  }
  uint64_t count_valid() const {
    return valid.popcount();
  }
};

#endif
