#ifndef __sim_bitvec_hh__
#define __sim_bitvec_hh__

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <cstring>
#include <string>
#include <boost/functional/hash.hpp>
#include "helper.hh"

template <typename E>
class sim_bitvec_template {
public:
  typedef E ET;
private:
  static const size_t bpw = 8*sizeof(E);
  static const E all_ones = ~static_cast<E>(0);
  uint64_t n_bits = 0, n_words = 0, ln2_bits;
  uint64_t last_bit = 0;
  E *arr = nullptr;
public:
  sim_bitvec_template(uint64_t n_bits = 64) :
    n_bits(n_bits),
    n_words((n_bits + bpw - 1) / bpw),
    ln2_bits(ln2(n_bits)),
    last_bit(n_bits-1) {
    arr = new E[n_words];
    memset(arr, 0, sizeof(E)*n_words);
  }
  ~sim_bitvec_template() {
    delete [] arr;
  }
  void clear() {
    memset(arr, 0, sizeof(E)*n_words);
  }
  //methods needed to hash
  bool operator==(const sim_bitvec_template& other) const {
    if(n_bits != other.n_bits)
      return false;
    int rc =  memcmp(reinterpret_cast<void*>(arr),
		     reinterpret_cast<void*>(other.arr),
		     sizeof(E)*n_words);
    return (rc == 0);
  }

  std::size_t hash() const noexcept {
    std::size_t seed = 0;
    for(size_t w = 0; w < n_words; ++w) {
      boost::hash_combine(seed, arr[w]);
    }
    return seed;
  }
  std::size_t hash(uint64_t bits, uint64_t pc) const noexcept {
    bits = (bits == 0) || (bits > n_bits) ? n_bits : bits;    
    std::size_t seed = pc;
    int64_t words = static_cast<int64_t>((bits + bpw - 1) / bpw);
    uint64_t leftover = bits % bpw;
    E mask = (static_cast<E>(1) << leftover) - 1;
    for(size_t w = 0; w < (words-1); ++w) {
      boost::hash_combine(seed, arr[w]);
    }
    boost::hash_combine(seed, (arr[words-1] & mask));
    return seed;
  }
  E xor_fold(uint64_t bits = 0) const noexcept {
    bits = (bits == 0) || (bits > n_bits) ? n_bits : bits;    
    E h = 0;
    int64_t words = static_cast<int64_t>((bits + bpw - 1) / bpw);
    uint64_t leftover = bits % bpw;
    E mask = (static_cast<E>(1) << leftover) - 1;    
    for(int64_t w = 0; w < (words-1); w++) {
      h ^= arr[w];
    }
    h ^= (arr[words-1] & mask);
    //std::cout << "mask = " << std::hex << mask << std::dec << "\n";
    //std::cout << "leftover = " << leftover << "\n";
    //std::cout << "h = " << h << "\n";
    return h;
  }
  std::size_t operator()() const noexcept {
    return hash();
  }
  //other stuff
  size_t ln2_size() const {
    return static_cast<size_t>(ln2_bits);
  }
  size_t size() const {
    return static_cast<size_t>(n_bits);
  }
  void clear_and_resize(uint64_t n_bits) {
    delete [] arr;
    this->n_bits = n_bits;
    this->n_words = (n_bits + bpw - 1) / bpw;
    this->ln2_bits = ln2(n_bits);
    last_bit = n_bits-1;
    arr = new E[n_words];
    memset(arr, 0, sizeof(E)*n_words);
  }
  bool get_bit(size_t idx) const {
    uint64_t w_idx = idx / bpw;
    uint64_t b_idx = idx % bpw;
    return (arr[w_idx] >> b_idx)&0x1;
  }
  std::string as_string() const {
    std::string s;
    for(ssize_t i = (n_bits-1); i >= 0; i--) {
      s += get_bit(i) ? "1" : "0";
    }
    return s;
  }
  bool operator[](size_t idx) const {
    return get_bit(idx);
  }
  void set_bit(size_t idx) {
    if(idx >= n_bits) {
      std::cerr << "fatal : trying to access bit "
		<< idx << " array of length "
		<< n_bits << "!\n";
      die();
    }
    uint64_t w_idx = idx / bpw;
    uint64_t b_idx = idx % bpw;
    arr[w_idx] |= (1UL << b_idx);
  }
  void clear_bit(size_t idx) {
    assert(idx < n_bits);
    uint64_t w_idx = idx / bpw;
    uint64_t b_idx = idx % bpw;
    arr[w_idx] &= ~(1UL << b_idx);
  }
  uint64_t popcount() const {
    uint64_t c = 0;
    if((bpw*n_words) != n_bits) {
      die();
    }
    else {
      for(uint64_t w = 0; w < n_words; w++) {
	c += __builtin_popcountll(arr[w]);
      }
    }
    return c;
  }
  uint64_t num_free() const {
    return n_bits - popcount();
  }
  int64_t find_first_unset_rr() const {
    for(uint64_t i = 0, b = last_bit; i < n_bits; i++) {
      if(get_bit(b)==false) {
	return b;
      }
      b = (b+1) % n_bits;
    }
    return -1;
  }
 int64_t find_first_unset_pair() const {
    for(uint64_t w = 0; w < n_words; w++) {
      if(arr[w] == all_ones)
	continue;
      else if(arr[w]==0) {
	return bpw*w;
      }
      else {
	uint64_t idx = (__builtin_ffsl(~arr[w])-1);
	idx &= (~1UL);
	for(uint64_t b = idx; b < bpw; b+=2) {
	  uint64_t p = bpw*w + b;
	  if(p >= n_bits) {
	    return -1;
	  }
	  if( ((arr[w] >> b) & 0x3) == 0x0) {
	    return p;
	  }
	}
      }
    }
   return -1;
 }
  int64_t find_first_unset() const {
    for(uint64_t w = 0; w < n_words; w++) {
      if(arr[w] == all_ones)
	continue;
      else if(arr[w]==0) {
	return bpw*w;
      }
      else {
	uint64_t x = ~arr[w];
	uint64_t idx = bpw*w + (__builtin_ffsl(x)-1);
	if(idx < n_bits)
	  return idx;
      }
    }
    return -1;    
  }
  int64_t find_first_set() const {
    for(uint64_t w = 0; w < n_words; w++) {
      if(arr[w] == 0)
	continue;
      uint64_t idx = bpw*w + (__builtin_ffsl(arr[w])-1);
      return (idx < n_bits) ? idx : -1;
    }
    return -1;
  }
  
  int64_t find_next_set(int64_t idx) const {
    idx++;
    uint64_t w_idx = idx / bpw;
    uint64_t b_idx = idx % bpw;
    //check current word
    E ww =  (arr[w_idx] >> b_idx) << b_idx;
    if(ww != 0) {
      return w_idx*bpw + (__builtin_ffsl(ww)-1);
    }
    for(uint64_t w = w_idx+1; w < n_words; w++) {
      if(arr[w] == 0)
	continue;
      else {
	uint64_t idx = bpw*w + (__builtin_ffsl(arr[w])-1);
	if(idx < n_bits) {
	  return idx;
	}
	break;
      }
    }
    return -1;    
  }
  void shift_left(uint32_t amt) {
    /* shift off top bits */
    arr[n_words-1] <<= amt;
    for(int i = (n_words-2); i >= 0; i--) {
      E wrap = arr[i] >> (bpw - amt);
      arr[i+1] |= wrap;
      arr[i] <<= amt;
    }
  }
  E to_integer() const {
    
    return arr[0];
  }
  friend std::ostream & operator<<(std::ostream &out, const sim_bitvec_template<E> &bv) {
    for(size_t i = 0; i < bv.size(); i++) {
      out <<  bv.get_bit(i);
    }
    return out;
  }
};

typedef sim_bitvec_template<uint16_t> sim_bitvec;

#endif
