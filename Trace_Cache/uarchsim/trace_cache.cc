//
// Created by s117 on 5/2/19.
//

#include "trace_cache.h"
#include "disasm.h"
#include "csignal"

static const size_t bitmask[] = {
  1u, 1u << 1u, 1u << 2u, 1u << 3u, 1u << 4u, 1u << 5u, 1u << 6u, 1u << 7u,
  1u << 8u, 1u << 9u, 1u << 10u, 1u << 11u, 1u << 12u, 1u << 13u, 1u << 14u, 1u << 15u
};

static inline reg_t calc_insn_next_addr(reg_t pc, insn_t *i, bool br_dir_assumption) {
  reg_t next_pc;
  switch (i->opcode()) {
    case OP_JALR:
      next_pc = UNKNOWN_PC_ADDR;
      break;
    case OP_JAL:
      next_pc = pc + i->uj_imm();
      break;
    case OP_BRANCH:
      if (br_dir_assumption)
        next_pc = pc + i->sb_imm();
      else
        next_pc = INCREMENT_PC(pc);
      break;
    default:
      next_pc = INCREMENT_PC(pc);
      break;
  }
  return next_pc;
}

static inline reg_t get_insn_from_memory(mmu_t *mmu, reg_t pc, bool br_dir_assumption, insn_t *i) {
  assert(pc != UNKNOWN_PC_ADDR);
  try {
    *i = mmu->load_insn(pc).insn;
  } catch (trap_t &t) {
    const char *t_name = t.name();
    reg_t t_cause = t.cause();
    assert(0);
  }
  return calc_insn_next_addr(pc, i, br_dir_assumption);
}

trace_cache::trace_cache(
  bool enable,
  size_t m, size_t n, size_t trace_capacity, size_t assoc,
  size_t max_ongoing_fill, bool en_non_block_fill, bool index_with_pred_bit,
  size_t term_cond,
  mmu_t *mmu
) {
  assert(IsPow2(trace_capacity));
  assert(IsPow2(assoc));
  assert((NONE <= term_cond) && (term_cond < INVALID_HEURISTIC));

  conf_en = enable;

  conf_non_block_fill = en_non_block_fill;
  conf_max_ongoing_fill = max_ongoing_fill;
  conf_index_with_pred_bit = index_with_pred_bit;
  conf_term_cond = (TerminateHeuristic) term_cond;
  conf_tc_prop_m = m;
  conf_tc_prop_n = n;
  conf_tc_n_set = trace_capacity / assoc;
  conf_tc_n_assoc = assoc;
  this->mmu = mmu;

  tc_storage = new tc_entry_t *[conf_tc_n_set];
  for (size_t i = 0; i < conf_tc_n_set; i++) {
    tc_storage[i] = new tc_entry_t[conf_tc_n_assoc];
  }
  perf_access = 0;
  perf_hit = 0;
  perf_fill = 0;
  perf_avg_hit_trace_length = 0;
  perf_fill_success = 0;

  flush();
}

trace_cache::tc_entry_t *trace_cache::access(reg_t pc, size_t pred_vec) {
  if(!conf_en)
    return nullptr;

  perf_access++;

  tc_entry_t *tc_set = tc_storage[calc_index(pc, pred_vec)];
  size_t tc_way = search_trace(pc, pred_vec);
//  return nullptr;

  if (!conf_non_block_fill && !pending_fill.empty())
    return nullptr;

  if (tc_way >= conf_tc_n_assoc || !tc_set[tc_way].valid) {
    // miss, initiate a filling if possible

    // don't initiate a fill if multi_fill feature is disable and
    // there is on going fill
    if (pending_fill.size() >= conf_max_ongoing_fill)
      return nullptr;

    // 1. find lru
    size_t replace_way = conf_tc_n_assoc;
    for (size_t i = 0; i < conf_tc_n_assoc; i++) {
      if (tc_set[i].lru_cnt == (conf_tc_n_assoc - 1)) {
        replace_way = i;
        break;
      }
    }
    assert(replace_way < conf_tc_n_assoc);
    // 2. check availability
    if (!tc_set[replace_way].filling) {
      // 2.1 prepare to fill
      tc_set[replace_way].start_pc = pc;
      tc_set[replace_way].br_direction_vec = 0;
      fill_slot_prepare(&tc_set[replace_way]);

//      if(pc == 0x00015330){
//        std::raise(SIGINT);
//      }

      // 2.2 update lru
      for (size_t i = 0; i < conf_tc_n_assoc; i++) {
        if (i == replace_way) {
          tc_set[i].lru_cnt = 0;
        } else {
          tc_set[i].lru_cnt += 1;
        }
      }

      // 2.3 record it
      active_fill_record fill_record{};
      fill_record.tc_slot = &tc_set[replace_way];
      pending_fill.push_front(fill_record);

    }

    return nullptr;
  } else {
    perf_hit++;
    // hit, update lru
    for (size_t i = 0; i < conf_tc_n_assoc; i++) {
      if (tc_set[i].lru_cnt < tc_set[tc_way].lru_cnt) {
        tc_set[i].lru_cnt += 1;
      }
    }
    tc_set[tc_way].lru_cnt = 0;

    perf_avg_hit_trace_length =
      ((perf_avg_hit_trace_length * (perf_access - 1)) + tc_set[tc_way].num_insn) / (float)perf_access;

    return &tc_set[tc_way];
  }
}

void trace_cache::feed(payload *payload_buf, size_t pay_buff_idx, size_t br_direction) {
  if (pending_fill.empty())
    return;

  payload_t &insn_payload_struct = payload_buf->buf[pay_buff_idx];
  // test trace terminate heuristic, always terminate the trace when JALR insn met
  bool trace_term_cond = test_terminate_cond(&insn_payload_struct.inst);

  // add instruction to all the trace slot waiting to be filled
  for (auto pf:pending_fill) {
    tc_entry_t *tc_slot = pf.tc_slot;
    // skip the slot that is finished
    if (tc_slot->filling) {
      // build trace
      if (insn_payload_struct.inst.opcode() == OP_BRANCH) {
        // check max branch count limit
        if (tc_slot->num_br != conf_tc_prop_m) {
          // add branch
          if (insn_payload_struct.next_pc != INCREMENT_PC(insn_payload_struct.pc)) {
            tc_slot->br_direction_vec |= bitmask[tc_slot->num_br];
          }
          tc_slot->fall_thru_addr = INCREMENT_PC(insn_payload_struct.pc);
          tc_slot->target_addr = insn_payload_struct.pc + insn_payload_struct.inst.sb_imm();
          tc_slot->num_insn++;
          tc_slot->num_br++;
          tc_slot->end_with_br = true;
        } else {
          // can't load any new branch in this trace
          fill_slot_terminate(tc_slot);
        }
      } else {
        // add normal instruction
        tc_slot->fall_thru_addr = calc_insn_next_addr(insn_payload_struct.pc, &insn_payload_struct.inst, false);
        tc_slot->num_insn++;
        tc_slot->end_with_br = false;
      }
      if (trace_term_cond || (tc_slot->num_insn == conf_tc_prop_n)) {
        // end the trace
        fill_slot_terminate(tc_slot);
      }
    }
  }

  // sweep the finished record
  auto it = pending_fill.begin();
  while (it != pending_fill.end()) {
    if (it->tc_slot->filling) {
      it++;
    } else {
      it = pending_fill.erase(it);
    }
  }
}

size_t trace_cache::calc_index(reg_t pc, size_t pred_vec) {
  if (conf_index_with_pred_bit) {
    return (pc ^ pred_vec) % (conf_tc_n_set);
  } else {
    return (pc) % (conf_tc_n_set);
  }
}

// return way idx
// if return value == n_assoc, then not found
size_t trace_cache::search_trace(reg_t pc, size_t pred_vec) {
  size_t tc_index;
  tc_entry_t *tc_row;
  size_t way_idx;

  tc_index = calc_index(pc, pred_vec);
  tc_row = tc_storage[tc_index];

  for (way_idx = 0; way_idx < conf_tc_n_assoc; way_idx++) {
    if (
      (
        pc == tc_row[way_idx].start_pc) &&
        match_br_direction(
          pred_vec,
          tc_row[way_idx].br_direction_vec,
          tc_row[way_idx].num_br,
          tc_row[way_idx].end_with_br
        )
      ) {
      break;
    }
  }

  return way_idx;
}

void trace_cache::flush() {
  for (size_t i = 0; i < conf_tc_n_set; i++) {
    for (size_t j = 0; j < conf_tc_n_assoc; ++j) {
      tc_storage[i][j].tc = this;
      tc_storage[i][j].valid = false;
      tc_storage[i][j].filling = false;
      tc_storage[i][j].lru_cnt = j;

      tc_storage[i][j].start_pc = 0;
      tc_storage[i][j].num_insn = 0;
      tc_storage[i][j].num_br = 0;
      tc_storage[i][j].br_direction_vec = 0;
      tc_storage[i][j].fall_thru_addr = 0;
      tc_storage[i][j].target_addr = 0;

      tc_storage[i][j].end_with_br = false;

    }
  }

  squash_unfinished_fill();
}

bool trace_cache::test_terminate_cond(insn_t *inst) {
  bool term = false;
  if (inst->opcode() == OP_JALR) {
    // always terminate at indirect jump
    term = true;
  } else if (
    (inst->opcode() == OP_AMO) ||
      (inst->opcode() == OP_SYSTEM)
    ) {
    // always terminate at atomic and system operation
    term = true;
  } else {
    switch (conf_term_cond) {
      case NONE:
        break;
      case BACKWARD_BRANCH:
        if (inst->sb_imm() < 0) {
          term = true;
        }
        break;
      case INVALID_HEURISTIC:
        assert(0);
    }
  }
  return term;
}

void trace_cache::fill_slot_terminate(trace_cache::tc_entry_t *tc_entry) {
  perf_fill_success++;
  tc_entry->valid = true;
  tc_entry->filling = false;
}

void trace_cache::fill_slot_prepare(trace_cache::tc_entry_t *tc_entry) {
  perf_fill++;
  tc_entry->valid = false;
  tc_entry->filling = true;
  tc_entry->num_insn = 0;
  tc_entry->num_br = 0;
}

void trace_cache::fill_slot_abort(trace_cache::tc_entry_t *tc_entry) {
  tc_entry->valid = false;
  tc_entry->filling = false;
}

bool trace_cache::match_br_direction(size_t pred_vec, size_t actual_dir, size_t num_br, bool end_with_br) {
  size_t mask = 0, i = 0;
  while (i < num_br) {
    mask = (mask << 1u) | 1u;
    i++;
  }
  if (end_with_br) {
    mask = mask & (~(1u << (num_br - 1)));
  }
  return (pred_vec & mask) == (actual_dir & mask);
}

void trace_cache::squash_unfinished_fill() {
  auto it = pending_fill.begin();
  while (it != pending_fill.end()) {
    fill_slot_abort(it->tc_slot);
    it = pending_fill.erase(it);
  }
}

trace_cache::tc_insn_iterator::tc_insn_iterator(trace_cache::tc_entry_t *entry, mmu_t *mmu) {
  if (entry && mmu) {
    assert(entry->valid);
    assert(entry->num_insn > 0);
    this->tc_entry = entry;
    this->mmu = mmu;

    this->next_br_idx = 0;
    this->next_insn_idx = 0;
    this->next_pc = tc_entry->start_pc;
  }
}

bool trace_cache::tc_insn_iterator::next(insn_t *i, bool do_term_check) {
  if (next_insn_idx >= tc_entry->num_insn) {
    return false;
  }

  assert(next_pc != UNKNOWN_PC_ADDR);

  next_pc = get_insn_from_memory(
    mmu,
    next_pc,
    tc_entry->br_direction_vec & bitmask[next_br_idx],
    i
  );

  if (do_term_check && (next_insn_idx < tc_entry->num_insn - 1)) {
    // JALR can only appear at the end of trace
    assert(i->opcode() != OP_JALR);
    assert(i->opcode() != OP_AMO);
    assert(i->opcode() != OP_SYSTEM);
  }

  next_insn_idx++;
  if (i->opcode() == OP_BRANCH) {
    next_br_idx++;
  }

  return true;
}

void trace_cache::tc_insn_iterator::rewind() {
  this->next_br_idx = 0;
  this->next_insn_idx = 0;
  this->next_pc = tc_entry->start_pc;
}

reg_t trace_cache::tc_insn_iterator::get_next_pc() {
  return next_pc;
}

bool trace_cache::tc_insn_iterator::end() {
  return next_insn_idx >= tc_entry->num_insn;
}

trace_cache::tc_insn_iterator trace_cache::tc_entry_t::get_insn_iterator() {
  return {this, tc->mmu};
}

std::string trace_cache::tc_entry_t::disasm_trace() {
  std::string result;
  disassembler_t disassembler;
  int offset = 0;
  char buf[64];

  auto tit = this->get_insn_iterator();
  insn_t curr_insn;// = new insn_t;
  reg_t curr_ip;

  while (!tit.end()) {
    curr_ip = tit.get_next_pc();
    tit.next(&curr_insn, false);
    sprintf(buf, "[T+%02d 0x%08lX] ", offset++, curr_ip);
    result += buf + disassembler.disassemble(curr_insn) + "\n";
  }


//  delete curr_insn;

  return result;
}
