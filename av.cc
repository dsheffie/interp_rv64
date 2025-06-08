#include "av.hh"
#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <fstream>

void av::addSample(uint64_t tgt, uint64_t bbsz) {
  if(avs.empty()) {
    avs.push_back(std::map<uint64_t, uint64_t>());
  }
  std::map<uint64_t, uint64_t> &m = avs.at(avs.size() - 1);
  //if(bbsz > 100) {
  //printf("very large basicblock of size %lu\n", bbsz);
  //exit(-1);
  //}
  m[tgt] += bbsz;
}


void av::nextSample(uint64_t icnt) {
  if((icnt % interval) != 0) {
    return;
  }
  if(not(avs.empty())) {
    std::map<uint64_t, uint64_t> &m = avs.at(avs.size() - 1);
  }
  avs.push_back(std::map<uint64_t, uint64_t>());
  //printf("new bbl for interval %lu\n", icnt);
}

void av::dumpAVs(const std::string &fname) const {
  std::ofstream out(fname);
  std::map<uint64_t, uint64_t> remap;
  uint64_t c = 1;
  for(const auto & v :  avs) {
    for(const auto &p : v) {
      uint64_t pc = p.first;
      if (remap.find(pc) != remap.end()) {
	continue;
      }
      remap[pc] = c;
      c++;
    }
  }
  for(const auto & v :  avs) {
    out << "T";
    //size_t n_pairs = v.size(), i = 0;
    
    for(const auto &p : v) {
      uint64_t pc = p.first;
      uint64_t cnt = p.second;
      out << ":" << remap.at(pc) << ":" << cnt;
      //if(i != (n_pairs-1)) {
      out << " ";
      //}
      //i++;
    }
    out << "\n";
  }
}
