//
// Created by s117 on 1/30/19.
//

#ifndef INC_721SIM_DATADEFINE_H
#define INC_721SIM_DATADEFINE_H

#include <stdint.h>
#include "CircularQueue.h"
#include "BitmapUtils.h"

typedef uint64_t logic_reg_idx_t;

typedef uint64_t phy_reg_idx_t;

typedef uint64_t pc_t;

typedef uint64_t reg_t;

typedef struct _rmt_entry_t {
  bool valid;
  phy_reg_idx_t phy_reg_idx;
} rmt_entry_t;

//typedef CircularQueue<phy_reg_idx_t> free_list_t;
typedef struct _active_list_entry_t {
  // ----- Fields related to destination register.
  // 1. indicates whether or not the instr. has a destination register
  bool flag_has_dst_reg;
  // 2. logical register number of the instruction's destination
  logic_reg_idx_t dst_logic_reg_id;
  // 3. physical register number of the instruction's destination
  phy_reg_idx_t dst_phy_reg_id;

  // ----- Fields related to completion status.
  // 4. completed bit
  bool complete;
  // ----- Fields for signaling offending instructions.
  // 5. exception bit
  bool squash_exception;
  // 6. load violation bit
  //    * Younger load issued before an older conflicting store.
  //      This can happen when speculative memory disambiguation
  //      is enabled.
  bool squash_load_violate;
  // 7. branch misprediction bit
  //    * At present, not ever set by the pipeline. It is simply
  //      available for deferred-recovery Approaches #1 or #2.
  //      Project 1 uses Approach #5, however.
  bool squash_br_mispred;
  // 8. value misprediction bit
  //    * At present, not ever set by the pipeline. It is simply
  //      available for deferred-recovery Approaches #1 or #2,
  //      if value prediction is added (e.g., research projects).
  // ----- Fields indicating special instruction types.
  bool squash_val_mispred;

  // 9. load flag (indicates whether or not the instr. is a load)
  bool flag_load;

  // 10. store flag (indicates whether or not the instr. is a store)
  bool flag_store;
  // 11. branch flag (indicates whether or not the instr. is a branch)
  bool flag_branch;
  // 12. amo flag (whether or not instr. is an atomic memory operation)
  bool flag_atomic;
  // 13. csr flag (whether or not instr. is a system instruction)
  bool flag_csr;
  // ----- Other fields.
  // 14. program counter of the instruction
  pc_t inst_pc;

} active_list_entry_t;

typedef struct _shadow_map_entry_t {
  std::unique_ptr<rmt_entry_t[]> saved_RMT;
  size_t saved_free_list_head;
  Bitmap saved_GBM;
} shadow_map_entry_t;

#endif //INC_721SIM_DATADEFINE_H
