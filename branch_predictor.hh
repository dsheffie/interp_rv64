#ifndef __bpred_hh__
#define __bpred_hh__

#include <cstdint>
#include <ostream>
#include <map>
#include <string>
#include <array>
#include "counter2b.hh"
#include "sim_bitvec.hh"

#define BPRED_IMPL_LIST(BA) \
  BA(unknown)		    \
  BA(gshare)		    \
  BA(bimodal)		    \
  BA(gtagged)		    \
  BA(uberhistory)	    \
  BA(tage)

class branch_predictor {
public:
  enum class br_type { direct_br, cond, call, ret, indirect_call, indirect_br};
#define ITEM(X) X,
  enum class bpred_impl {
    BPRED_IMPL_LIST(ITEM)
  };
#undef ITEM
  static const std::map<std::string, bpred_impl> bpred_impl_map;
protected:
  uint64_t &icnt;
  sim_bitvec* bhr;    
  uint64_t n_branches, n_mispredicts;
  uint64_t old_gbl_hist;
  std::array<uint64_t,6> branch_ty_cnt, branch_ty_mispred_cnt;  
  std::map<uint32_t, uint64_t> mispredict_map;
  static int br_type_idx(const br_type &ty) {
    int idx = 0;
    switch(ty)
      {
      case br_type::direct_br:
	idx = 0;
	break;
      case br_type::cond:
	idx = 1;
	break;
      case br_type::call:
	idx = 2;
	break;
      case br_type::ret:
	idx = 3;
	break;
      case br_type::indirect_call:
	idx = 4;
	break;		
      case br_type::indirect_br:
	idx = 5;
	break;	
      default:
	break;
      }
    return idx;
  }
  void update_bhr(bool);
public:
  branch_predictor(uint64_t &icnt);
  virtual ~branch_predictor();
  virtual void get_stats(uint64_t &n_br, uint64_t &n_mis, uint64_t &n_inst) const;
  virtual bool predict(uint64_t, uint64_t &)  = 0;
  virtual void update_(uint64_t, uint64_t, bool, bool) = 0;
  virtual int needed_history_length() const { return 0; }
  virtual const char* getTypeString() const =  0;
  static bpred_impl lookup_impl(const std::string& impl_name);
  const std::map<uint32_t, uint64_t> &getMap() const {
    return mispredict_map;
  }
  std::map<uint32_t, uint64_t> &getMap() {
    return mispredict_map;
  }
  void update(uint64_t, uint64_t, bool, bool, br_type);
};

class gshare : public branch_predictor {
protected:
  constexpr static const char* typeString = "gshare";  
  uint32_t lg_pht_entries = 0;
  uint32_t pc_shift = 0;
  twobit_counter_array *pht = nullptr;
public:
  gshare(uint64_t & icnt, uint32_t lg_pht_entries, uint32_t pc_shift = 3);
  ~gshare();
  const char* getTypeString() const override {
    return typeString;
  }
  bool predict(uint64_t, uint64_t &) override;
  void update_(uint64_t addr, uint64_t idx, bool prediction, bool taken) override;
};


class tage : public branch_predictor {
public:
  static const uint64_t TAG_LEN = 12;
  static const uint64_t TAG_MASK = (1UL<<TAG_LEN)-1;
protected:
  template<typename T, typename std::enable_if<std::is_integral<T>::value, T>::type* = nullptr>
  struct te {
    T pred : 2;
    T useful : 2;
    T tag : TAG_LEN;
    void clear() {
      pred = 0;
      useful = 0;
      tag = 0;
    }
  };
  typedef te<uint64_t> tage_entry;
protected:
  constexpr static const char* typeString = "tage";

  static const int n_tables = 3;  
  const int table_lengths[n_tables] =  {256,32};  
  
  tage_entry *tage_tables[n_tables] = {nullptr};
  uint64_t hashes[n_tables] = {0};
  bool pred[n_tables] = {false};
  bool pred_valid[n_tables] = {false};
  
  //statistics
  uint64_t pred_table[n_tables+1] = {0};
  uint64_t corr_pred_table[n_tables+1] = {0};
  
  uint32_t lg_pht_entries = 0;
  twobit_counter_array *pht = nullptr;

  
public:
  tage(uint64_t & icnt, uint32_t lg_pht_entries);
  ~tage();
  const char* getTypeString() const override {
    return typeString;
  }
  bool predict(uint64_t, uint64_t &) override;
  void update_(uint64_t addr, uint64_t idx, bool prediction, bool taken) override;
  int needed_history_length() const override {
    return table_lengths[0];
  }
};


class gtagged : public branch_predictor {
protected:
  constexpr static const char* typeString = "gtagged";  
  std::map<uint64_t, uint8_t> pht;
public:
  gtagged(uint64_t &);
  ~gtagged();
  const char* getTypeString() const override {
    return typeString;
  }  
  bool predict(uint64_t, uint64_t &) override;
  void update_(uint64_t addr, uint64_t idx, bool prediction, bool taken) override;
};


class bimodal : public branch_predictor {
protected:
  constexpr static const char* typeString = "bimodal";
  uint32_t lg_c_pht_entries = 0;
  uint32_t lg_pht_entries = 0 ;
  twobit_counter_array *c_pht = nullptr;
  twobit_counter_array *t_pht = nullptr;
  twobit_counter_array *nt_pht = nullptr;
public:
  bimodal(uint64_t &,uint32_t,uint32_t);
  ~bimodal();
  const char *getTypeString() const override {
    return typeString;    
  }
  bool predict(uint64_t, uint64_t &) override;
  void update_(uint64_t addr, uint64_t idx, bool prediction, bool taken) override;
};

class uberhistory : public branch_predictor {
protected:
  constexpr static const char* typeString = "uberhistory";
  std::map<std::string, uint8_t> pht;
  std::string sidx;
public:
  uberhistory(uint64_t &, uint32_t);
  ~uberhistory();
  const char* getTypeString() const override {
    return typeString;
  }
  bool predict(uint64_t, uint64_t &) override;
  void update_(uint64_t addr, uint64_t idx, bool prediction, bool taken) override;
};

std::ostream &operator<<(std::ostream &, const branch_predictor&);

#ifndef KEEP_BPRED_IMPL_IMPL
#undef BPRED_IMPL_IMPL
#endif

#endif
