#ifndef __avhh__
#define __avhh__

#include <map>
#include <vector>
#include <cstdint>
#include <string>

class av {
private:
  uint64_t interval;
  std::vector<std::map<uint64_t,uint64_t>> avs;
public:
  av(uint64_t interval = 10*1000*1000) : interval(interval) {}
  void addSample(uint64_t tgt, uint64_t bbsz);
  void nextSample(uint64_t icnt);
  void dumpAVs(const std::string &prefix) const;
};

#endif
