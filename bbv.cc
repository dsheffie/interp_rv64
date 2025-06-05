#include "bbv.hh"
#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <fstream>

void bbv::addSample(uint64_t tgt, uint64_t bbsz) {
  if(bbvs.empty()) {
    bbvs.push_back(std::map<uint64_t, uint64_t>());
  }
  std::map<uint64_t, uint64_t> &m = bbvs.at(bbvs.size() - 1);
  //if(bbsz > 100) {
  //printf("very large basicblock of size %lu\n", bbsz);
  //exit(-1);
  //}
  m[tgt] += bbsz;
}


void bbv::nextSample(uint64_t icnt) {
  if((icnt % interval) != 0) {
    return;
  }
  if(not(bbvs.empty())) {
    std::map<uint64_t, uint64_t> &m = bbvs.at(bbvs.size() - 1);
  }
  bbvs.push_back(std::map<uint64_t, uint64_t>());
  //printf("new bbl for interval %lu\n", icnt);
}

void bbv::dumpBBVs(const std::string &prefix) const {
  std::ofstream out(prefix + std::string(".bbv"));
  std::map<uint64_t, uint64_t> remap;
  uint64_t c = 1;
  for(const auto & v :  bbvs) {
    for(const auto &p : v) {
      uint64_t pc = p.first;
      if (remap.find(pc) != remap.end()) {
	continue;
      }
      remap[pc] = c;
      c++;
    }
  }
  for(const auto & v :  bbvs) {
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
