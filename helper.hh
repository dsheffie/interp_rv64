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

#ifndef __HELPERFUNCTS__
#define __HELPERFUNCTS__

#include <string>
#include <sstream>
#include <vector>
#include <cstdint>
#include <iostream>

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"

#define FMT_HEX(x) std::hex << x << std::dec

template <typename T, typename std::enable_if<std::is_integral<T>::value,T>::type* = nullptr>
T ln2(T x) {
  T y = 1, l =0;
  while(y < x) {
    l++;
    y *= 2;
  }
  return l;
}

template <typename T, typename std::enable_if<std::is_integral<T>::value,T>::type* = nullptr>
std::string as_bitvec(const T & x) {
  std::stringstream s;
  const size_t len = sizeof(T)*8;
  for(size_t i = 0; i < len; i++) {
    s << ((x >> i) & 1) ;
  }
  return s.str();
}


void dbt_backtrace();

#ifndef UNREACHABLE
#define UNREACHABLE() {				\
    __builtin_unreachable();			\
  }
#endif

#define die() {								\
    std::cerr << __PRETTY_FUNCTION__ << " @ " << __FILE__ << ":"	\
	      << __LINE__ << " called die\n";				\
    dbt_backtrace();							\
    abort();								\
  }


#ifndef print_var
#define print_var(x) std::cout <<  #x << " = " << x << "\n";
#endif

double timestamp();

uint32_t update_crc(uint32_t crc, uint8_t *buf, size_t len);
uint32_t crc32(uint8_t *buf, size_t len);

int32_t remapIOFlags(int32_t flags);

template <class T>
std::string toString(T x) {
  return std::to_string(x);
}

template <class T>
std::string toStringHex(T x) {
  std::stringstream ss;
  ss << std::hex << x;
  return ss.str();
}

template <typename T, typename std::enable_if<std::is_integral<T>::value, T>::type* = nullptr>
bool extractBit(T x, uint32_t b) {
  return (x >> b) & 0x1;
}

template <typename T, typename std::enable_if<std::is_integral<T>::value, T>::type* = nullptr>
T setBit(T x, T v, uint32_t b) {
  T t = static_cast<T>(1) << b;
  T tt = (~t) & x;
  t  &= ~(v-1);
  return (tt | t);
}

#define INTEGRAL_ENABLE_IF(SZ,T) typename std::enable_if<std::is_integral<T>::value and (sizeof(T)==SZ),T>::type* = nullptr

template <typename T, INTEGRAL_ENABLE_IF(1,T)>
T bswap(T x) {
  return x;
}

template <bool EL, typename T, INTEGRAL_ENABLE_IF(2,T)> 
T bswap(T x) {
  static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "must be little endian machine");
  if(EL) 
    return x;
  else
  return  __builtin_bswap16(x);
}

template <bool EL, typename T, INTEGRAL_ENABLE_IF(4,T)>
T bswap(T x) {
  static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "must be little endian machine");
  if(EL)
    return x;
  else 
    return  __builtin_bswap32(x);
}

template <bool EL, typename T, INTEGRAL_ENABLE_IF(8,T)> 
T bswap(T x) {
  static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "must be little endian machine");
  if(EL)
    return x;
  else 
    return  __builtin_bswap64(x);
}

#undef INTEGRAL_ENABLE_IF



template <class T> bool isPow2(T x) {
  return (((x-1)&x) == 0);
}


#endif
