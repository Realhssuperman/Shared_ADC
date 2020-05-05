//
// Created by s117 on 5/2/19.
//

#ifndef P2_UARCHSIM_TRACE_CACHE_H_
#define P2_UARCHSIM_TRACE_CACHE_H_

#include "DataDefine.h"
#include "cache.h"
#include "payload.h"
#include "mmu.h"
#include <list>

#define UNKNOWN_PC_ADDR UINT64_MAX

class trace_cache {
 public:
  class tc_insn_iterator;

  class tc_entry_t {
    friend trace_cache;
   public:
    reg_t start_pc;
    size_t num_insn;
    size_t num_br;
    size_t br_direction_vec;
    reg_t fall_thru_addr;
    reg_t target_addr;

    bool end_with_br;
    bool valid;
    bool filling;

    tc_insn_iterator get_insn_iterator();

    std::string disasm_trace();

   protected:
    trace_cache *tc;
    size_t lru_cnt;
  };

  class tc_insn_iterator {
   public:
    tc_insn_iterator(tc_entry_t *entry, mmu_t *mmu);

    bool next(insn_t *i, bool do_term_check = true);

    reg_t get_next_pc();

    bool end();

    void rewind();

   private:
    mmu_t *mmu;
    tc_entry_t *tc_entry;
    size_t next_br_idx;
    size_t next_insn_idx;
    reg_t next_pc;
  };

  enum TerminateHeuristic {
    NONE = 0,
    BACKWARD_BRANCH,
    INVALID_HEURISTIC
  };

  size_t perf_access;
  size_t perf_hit;
  size_t perf_fill;
  float perf_avg_hit_trace_length;
  size_t perf_fill_success;

  trace_cache(
    bool enable,
    size_t m, size_t n, size_t trace_capacity, size_t assoc,
    size_t max_ongoing_fill, bool en_non_block_fill, bool index_with_pred_bit,
    size_t term_cond,
    mmu_t *mmu
  );

  tc_entry_t *access(reg_t pc, size_t pred_vec);

  void feed(payload *payload_buf, size_t pay_buff_idx, size_t br_direction);

  void squash_unfinished_fill(); // squash ongoing fill

  void flush();

 protected:
  mmu_t *mmu;

 private:
  class active_fill_record {
   public:
    tc_entry_t *tc_slot;
  };
  std::list<active_fill_record> pending_fill;

  tc_entry_t **tc_storage;
  bool conf_en;

  TerminateHeuristic conf_term_cond;
  bool conf_non_block_fill;
  size_t conf_max_ongoing_fill;
  bool conf_index_with_pred_bit;

  size_t conf_tc_prop_m, conf_tc_prop_n;
  size_t conf_tc_n_set, conf_tc_n_assoc;




  void fill_slot_prepare(tc_entry_t *tc_entry);

  void fill_slot_terminate(tc_entry_t *tc_entry);

  void fill_slot_abort(tc_entry_t *tc_entry);

  size_t search_trace(reg_t pc, size_t pred_vec);

  size_t calc_index(reg_t pc, size_t pred_vec);

  bool match_br_direction(size_t pred_vec, size_t actual_dir, size_t num_br, bool end_with_br);

  bool test_terminate_cond(insn_t *inst);

};

#endif //P2_UARCHSIM_TRACE_CACHE_H_
