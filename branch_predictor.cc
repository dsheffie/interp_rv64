#define KEEP_BPRED_IMPL_IMPL
#include "branch_predictor.hh"
#include "globals.hh"
#include <sstream>

branch_predictor::branch_predictor(uint64_t &icnt):
  icnt(icnt), bhr(nullptr), n_branches(0), n_mispredicts(0), old_gbl_hist(0) {}

void branch_predictor::get_stats(uint64_t &n_br,
				 uint64_t &n_mis,
				 uint64_t &n_insns) const {
  n_br = n_branches;
  n_mis = n_mispredicts;
  n_insns = icnt;
}

branch_predictor::~branch_predictor() {
  if(bhr != nullptr) {
    delete bhr;
  }
}

void branch_predictor::update_bhr(bool t) {
  if(bhr) {
    bhr->shift_left(1);
    if(t) {
      bhr->set_bit(0);
    }
  }
}

void branch_predictor::update(uint64_t addr, uint64_t idx, bool prediction, bool taken) {
  update_(addr, idx, prediction, taken);
  update_bhr(taken);
}

uberhistory::uberhistory(uint64_t &icnt, uint32_t lg_history_entries) :
  branch_predictor(icnt) {
}

uberhistory::~uberhistory() {
  size_t used = pht.size();
  std::cout << used << " valid entries in history table\n";
}

bool uberhistory::predict(uint64_t addr, uint64_t &idx)  {
  idx = 0;
  sim_bitvec &h = *bhr;
  std::stringstream ss;
  ss << std::hex << (addr>>2) << std::dec;
  
  sidx = ss.str() + h.as_string();
  
  return pht[sidx] > 1;
}

void uberhistory::update_(uint64_t addr, uint64_t idx, bool prediction, bool taken) {
  bool mispredict = prediction != taken;

  int32_t h = static_cast<int32_t>(pht[sidx]);
  h = taken ? h+1 : h-1;
  h = std::min(3, h);
  h = std::max(0, h);
  pht[sidx] = h;
  n_branches++;
  if(mispredict) {
    n_mispredicts++;
    mispredict_map[addr]++;
  }
}

gshare::gshare(uint64_t &icnt, uint32_t lg_pht_entries, uint32_t pc_shift) :
  branch_predictor(icnt),
  lg_pht_entries(lg_pht_entries),
  pc_shift(pc_shift) {
  pht = new twobit_counter_array(1U<<lg_pht_entries);
  bhr = new sim_bitvec;
}

gshare::~gshare() {
  delete pht;
}

tage::tage(uint64_t &icnt, uint32_t lg_pht_entries) :
  branch_predictor(icnt),
  lg_pht_entries(lg_pht_entries){

  pht = new twobit_counter_array(1U<<lg_pht_entries);
  
  for(int h = 0; h < tage::n_tables; h++) {
    tage_tables[h] = new tage_entry[1U<<lg_pht_entries];
    for(size_t i = 0; i < (1U<<lg_pht_entries); i++) {
      tage_tables[h][i].clear();
    }
  }
  
}

tage::~tage() {
  delete pht;
  for(int h = 0; h < tage::n_tables; h++) {
    delete [] tage_tables[h];
  }
  for(int h = 0; h <= tage::n_tables; h++) {
    double f = (static_cast<double>(corr_pred_table[h]) / pred_table[h]) * 100.0;
    std::cout << f << " percent correct "
	      << pred_table[h] << " predictions, "
	      << corr_pred_table[h] << " correct from table len "
	      << ((h==0) ? 0 : table_lengths[h-1]) << "\n";
  }
}

bool tage::predict(uint64_t addr, uint64_t & idx) {
  bool hit = false, prediction = false;

  uint32_t addr_hash = pc_hash(addr);
  //std::cout << bhr->as_string() << "\n";
  
  for(int h = 0; h < tage::n_tables; h++) {
    uint64_t hash = 0;
    hash = bhr->xor_fold(tage::table_lengths[h]);
    //hash = bhr->hash(tage::table_lengths[h]);
    hash ^= (addr << 2);
    hash &= (1UL << lg_pht_entries) - 1;
    hashes[h] = hash;
    pred_valid[h] = false;
  }


  for(int h = 0; h < tage::n_tables; h++)  {
    if(tage_tables[h][hashes[h]].tag == addr_hash) {
      pred[h] = (tage_tables[h][hashes[h]].pred > 1);
    }
  }
  
  for(int h = 0; h < tage::n_tables; h++)  {
    bool tag_match = tage_tables[h][hashes[h]].tag == addr_hash;
    if(tag_match) {
      idx = (static_cast<uint64_t>(h+1) << 32) | hashes[h];
      prediction = (tage_tables[h][hashes[h]].pred > 1);
      hit = true;
      break;
    }
  }
  
  //missed tagged tables, provide prediction from pht
  if(hit == false) {
    idx = addr_hash & ((1UL << lg_pht_entries) - 1);
    prediction = pht->get_value(idx) > 1;
  }
  
  return prediction;
}

void tage::update_(uint64_t addr, uint64_t idx, bool prediction, bool taken) {
  uint32_t n_pht_entries = 1U<<lg_pht_entries;
			       
  uint64_t table = idx >> 32;
  uint32_t entry = (idx & (static_cast<uint64_t>(1) << 32) - 1);

  bool correct_pred = prediction == taken;
  
  assert(entry < n_pht_entries);
  pred_table[table]++;
  corr_pred_table[table] += correct_pred;
  
  if(table == 0) {
    pht->update(entry, taken);
  }
  else {
    int t = table - 1;
    int p = tage_tables[t][entry].pred;
    p = taken ? p+1 : p-1;
    p = std::max(0, p);
    p = std::min(3, p);
    tage_tables[t][entry].pred = p;

    for(int i = 0; i < tage::n_tables; i++) {
      if(pred_valid[i] && (i != t) && (pred[i] != prediction)) {
	int u = tage_tables[t][entry].useful;
	u = correct_pred ? u + 1 : u - 1;
	u = (u > 3) ? 3 : u;
	u = (u < 0) ? 0 : u;
	tage_tables[t][entry].useful = u;	
	break;
      }
    }
  }


  
  if(!correct_pred) {
    bool alloc = false;
    int tagged_table = table - 1;
    
    //if(table == 1) printf("longest table mispredicted\n");
    
    for(int t = tagged_table+1; t < tage::n_tables; t++) {
      
      if(pred_valid[t] && (pred[t] == taken)) {
	std::cout << "prediction from table " << table << " was incorrect but " << t << " was correct\n";
	break;
      }
      
    }
    
    for(int t = table; t < tage::n_tables; t++) {
      if(tage_tables[t][hashes[t]].useful == 0) {
	//std::cout << "allocate from table " << t << " for index " << entry << "\n";
	alloc = true;
	tage_tables[t][hashes[t]].pred = 1;
	tage_tables[t][hashes[t]].tag = pc_hash(addr);
	break;
      }
    }

    
    if(!alloc) {
      for(int t = table; t < tage::n_tables; t++) {
	int u = tage_tables[t][hashes[t]].useful;
	u = (u == 0) ? 0 : (u-1);
	tage_tables[t][hashes[t]].useful = u;
      }
    }
  }

  n_branches++;
  if((n_branches & ((1<<20)-1)) == 0) {
    for(int t = table; t < tage::n_tables; t++) {
      tage_tables[t][hashes[t]].useful = 0;
    }
  }
  n_mispredicts += !correct_pred;

  //std::cout << "prediction came from table " << table << "\n";
}




bool gshare::predict(uint64_t addr, uint64_t &idx) {
  uint64_t fold_bhr = bhr->to_integer();
  old_gbl_hist = fold_bhr;
  
  fold_bhr = (fold_bhr >> 32) ^ (fold_bhr & ((1UL<<32)-1));
  fold_bhr = (fold_bhr >> 16) ^ (fold_bhr & ((1UL<<16)-1));

  idx =  (addr << pc_shift) ^ fold_bhr;


  // if(addr == 0x21fd0) {
    // std::cout << std::hex << addr
    // 	      << " " << *bhr << ", idx = " << idx
    // 	      << " gbl hist = " << old_gbl_hist
    // 	      << std::dec
    // 	      << ", clamped idx = "
    // 	      << (idx & ((1UL << lg_pht_entries) - 1))  
    // 	      << "\n";
  // }

  idx &= (1UL << lg_pht_entries) - 1;  
  return pht->get_value(idx) > 1;
}

void gshare::update_(uint64_t addr, uint64_t idx, bool prediction, bool taken) {
  
  uint64_t fold_bhr = old_gbl_hist;
  fold_bhr = (fold_bhr >> 32) ^ (fold_bhr & ((1UL<<32)-1));
  fold_bhr = (fold_bhr >> 16) ^ (fold_bhr & ((1UL<<16)-1));

  
  // std::cout << std::hex << addr << std::dec << ":" << fold_bhr << ", idx = " << idx
  // 	    << ", mispred = " << (prediction != taken)
  // 	    << ", pred = " << prediction
  // 	    << ", taken = " << taken
  // 	    << ", gbl hist = " << std::hex << old_gbl_hist
  // 	    << ", addr << pc_shift = " << (addr << pc_shift)
  // 	    << ",xor'd = " << ((addr << pc_shift)^fold_bhr)
  // 	    << std::dec 
  // 	    <<"\n";
  //}

  pht->update(idx, taken);
  n_branches++;

  if(prediction != taken) {
    n_mispredicts++;
    mispredict_map[addr]++;
  }
}

gtagged::gtagged(uint64_t &icnt) :
  branch_predictor(icnt) {}
gtagged::~gtagged() {}

bool gtagged::predict(uint64_t addr, uint64_t &idx) {
  uint64_t hbits = static_cast<uint64_t>(bhr->to_integer());
  hbits &= ((1UL<<32)-1);
  hbits <<= 32;
  idx = (addr>>2) | hbits;
  const auto it = pht.find(idx);
  if(it == pht.cend()) {
    return false;
  }
  return (it->second > 1);
}

void gtagged::update_(uint64_t addr, uint64_t idx, bool prediction, bool taken) {
  uint8_t &e = pht[idx];  
  if(taken) {
    e = (e==3) ? 3 : (e + 1);
  }
  else {
    e = (e==0) ? 0 : e-1;
  }
  n_branches++;
  n_mispredicts += (prediction != taken);
}



bimodal::bimodal(uint64_t &icnt, uint32_t lg_c_pht_entries, uint32_t lg_pht_entries) :
  branch_predictor(icnt), lg_c_pht_entries(lg_c_pht_entries), lg_pht_entries(lg_pht_entries) {
  c_pht = new twobit_counter_array(1U<<lg_c_pht_entries);
  nt_pht = new twobit_counter_array(1U<<lg_pht_entries);
  t_pht = new twobit_counter_array(1U<<lg_pht_entries);
}

bimodal::~bimodal() {
  double x = static_cast<double>(c_pht->count_valid()) / static_cast<double>(c_pht->get_nentries());
  std::cout << (100.0*x) << "% of choice pht entries valid\n";
  double y = static_cast<double>(nt_pht->count_valid()) / static_cast<double>(nt_pht->get_nentries());
  std::cout << (100.0*y) << "% of not taken pht entries valid\n";
  double z = static_cast<double>(t_pht->count_valid()) / static_cast<double>(t_pht->get_nentries());
  std::cout << (100.0*z) << "% of taken pht entries valid\n";

  delete c_pht;
  delete nt_pht;
  delete t_pht;
}

bool bimodal::predict(uint64_t addr, uint64_t &idx) {
  uint32_t c_idx = (addr>>2) & ((1U<<lg_c_pht_entries)-1);
  idx = ((addr>>2) ^ bhr->to_integer()) & ((1U<<lg_pht_entries)-1);
  if(c_pht->get_value(c_idx) < 2) {
    return nt_pht->get_value(idx)>1;
  }
  return t_pht->get_value(idx)>1;
}

void bimodal::update_(uint64_t addr, uint64_t idx, bool prediction, bool taken) {
  uint32_t c_idx = (addr>>2) & ((1U<<lg_c_pht_entries)-1);
  if(c_pht->get_value(c_idx) < 2) {
    if(not(taken and (nt_pht->get_value(idx) > 1))) {
      c_pht->update(c_idx, taken);
    }
    nt_pht->update(idx, taken);
  }
  else {
    if(not(not(taken) and (t_pht->get_value(idx) < 2))) {
      c_pht->update(c_idx, taken);
    }
    t_pht->update(idx,taken);
  }
  n_branches++;
  n_mispredicts += (prediction != taken);
}


std::ostream &operator<<(std::ostream &out, const branch_predictor& bp) {
  uint64_t n_br=0,n_mis=0, icnt = 0;
  bp.get_stats(n_br,n_mis,icnt);
  double br_r = static_cast<double>(n_br-n_mis) / n_br;
  out << bp.getTypeString() << "\n";
  out << (100.0*br_r) << "\% of branches predicted correctly\n";
  out << 1000.0 * (static_cast<double>(n_mis) / icnt)
      << " mispredicts per kilo insn\n";
  return out;
}


branch_predictor::bpred_impl branch_predictor::lookup_impl(const std::string& impl_name) {
  auto it = bpred_impl_map.find(impl_name);
  if(it == bpred_impl_map.end()) {
    return branch_predictor::bpred_impl::unknown;
  }
  return it->second;
}

#define PAIR(X) {#X, branch_predictor::bpred_impl::X},
const std::map<std::string, branch_predictor::bpred_impl> branch_predictor::bpred_impl_map = {
  BPRED_IMPL_LIST(PAIR)
};
#undef PAIR
