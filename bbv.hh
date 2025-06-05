#ifndef __bbvhh__
#define __bbvhh__

#include <map>
#include <vector>
#include <cstdint>

class bbv {
private:
  uint64_t interval;
  std::vector<std::map<uint64_t,uint64_t>> bbvs;
public:
  bbv(uint64_t interval) : interval(interval) {}
  void addSample(uint64_t tgt, uint64_t bbsz);
  void nextSample(uint64_t icnt);
  void dumpBBVs() const;
};

#endif
