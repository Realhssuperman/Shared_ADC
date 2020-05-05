#include "pipeline.h"
#include "mmu.h"
#include "CacheClass.h"

// vector format:
// br_found=3
// Vec:   00000111
// BrPos: XXXXX321 (1 is the prediction for the closest branch)
static uint32_t mk_multi_br_pred_perfect(
  debug_buffer_t *db_buf, debug_index_t db_index, unsigned int Tid,
  uint32_t *multi_pred_vector
) {
  // we only search ${TRACE_LENGTH} future instructions
  // for ${MAX_NUM_OF_BR_IN_TRACE} branches direction info
  // because those clue are enough for us to do the perfect
  // trace cache hit detection
  //
//  static const uint32_t FUTURE_LOOK_AHEAD_STEP = 16;
  static const uint32_t FUTURE_LOOK_AHEAD_STEP = FETCH_WIDTH;
//  debug_index_t db_index = PAY->buf[payload_idx].db_index;
//  auto *db_buf = proc->get_pipe();
  uint32_t future_vector = 0;
  size_t br_found = 0;
  if (db_index != DEBUG_INDEX_INVALID) {
    for (size_t i = 0; i < FUTURE_LOOK_AHEAD_STEP && br_found < MULTI_PRED_NUM_PRED; ++i) {
      db_index = MOD((db_index + 1), PIPE_QUEUE_SIZE);
      auto *future_insn = db_buf->peek(db_index);
      if (future_insn->a_inst.opcode() == OP_BRANCH) {
        if (future_insn->a_next_pc != future_insn->a_pc + 4) {
          future_vector |= 1u << br_found;
        }
        br_found++;
      }
    }
  }

  *multi_pred_vector = future_vector;
  return br_found;
}

static size_t num_prediction = 0;
static size_t num_bit_correct[16] = {0};

static float correct_ratio[16] = {0.0};

static unsigned int gen_pred_bit_mask(unsigned n_th_pred) {
  return (1u << (n_th_pred));
}

static void stat_pred_accuracy(unsigned int real_vec, unsigned int perfect_vec, unsigned int useful_bit) {
  num_prediction++;
  unsigned int useful_bit_mask = 0;
  for (size_t i = 0; i < useful_bit; ++i) {
    useful_bit_mask |= gen_pred_bit_mask(i);
  }
  auto real_vec_cmp = useful_bit_mask & real_vec;
  auto perf_vec_cmp = useful_bit_mask & perfect_vec;
  for (size_t i = 0; i < MULTI_PRED_NUM_PRED; ++i) {
    unsigned int cmp_mask = gen_pred_bit_mask(i);
    if ((cmp_mask & real_vec_cmp) == (cmp_mask & perf_vec_cmp)) {
      num_bit_correct[i]++;
      correct_ratio[i] = ((float) num_bit_correct[i]) / num_prediction;
    } else {
      break;
    }
  }
}

void pipeline_t::fetch() {
  // Variables related to instruction cache.
  unsigned int line1;
  unsigned int line2;
  bool hit1;
  bool hit2;
  cycle_t resolve_cycle1;
  cycle_t resolve_cycle2;

  // Variables influencing when to terminate fetch bundle.
  unsigned int i;    // iterate up to fetch width
  bool stop;        // branch, icache block boundary, etc.

  // Instruction fetched from mmu.
  insn_t insn;        // "insn" is used by some MACROs, hence, need to use this name for the instruction variable.
  reg_t trap_cause = 0;

  // PAY index for newly fetched instruction.
  unsigned int index;

  // Link to corresponding instrution in functional simulator, i.e., map_to_actual() functionality.
  db_t *actual;

  // Local variables related to branch prediction.
  unsigned int history_reg;
  unsigned int direct_target;
  reg_t next_pc;
  unsigned int pred_tag;
  bool conf;
  bool fm;

  unsigned int br_multi_pred_vec_real;
  unsigned int br_multi_pred_vec_perfect;
  bool pred_valid = true;
  bool first_insn = true;
  bool has_branch_in_curr_bundle = false;

  unsigned int first_insn_db_idx;

  if ((cycle & (0x400000u - 1u)) == 0) {
    printf("cycle=%lu\n"
           " TC   - TC HitR=%zu/%zu=%2.2f%%, TC FillSucc=%zu/%zu=%2.2f%%, AvgTCLen=%2.2f\n"
           " MBPU - Prediction=%zu, Accu[1] = %2.2f%%, Accu[2] = %2.2f%%, Accu[3] = %2.2f%%, Accu[4] = %2.2f%%, Accu[5] = %2.2f%%\n",
           cycle,
           TC->perf_hit,
           TC->perf_access,
           (((float) TC->perf_hit) / TC->perf_access) * 100,
           TC->perf_fill_success,
           TC->perf_fill,
           (((float) TC->perf_fill_success) / TC->perf_fill) * 100,
           TC->perf_avg_hit_trace_length,
           num_prediction,
           correct_ratio[0] * 100,
           correct_ratio[1] * 100,
           correct_ratio[2] * 100,
           correct_ratio[3] * 100,
           correct_ratio[4] * 100
    );

  }
  /////////////////////////////
  // Stall logic.
  /////////////////////////////

  // Stall the Fetch Stage if either:
  // 1. The Decode Stage is stalled.
  // 2. An I$ miss has not yet resolved.
  if ((DECODE[0].valid) ||        // Decode Stage is stalled.
    (cycle < next_fetch_cycle)) {    // I$ miss has not yet resolved.
    return;
  }

  BP.prepare_multi_pred_future_buf(pc, &br_multi_pred_vec_real);

  // TEST Trace Cache to see if we can go to the fast path?
  // If true, goto fast path, and if we are in refill mode
  //    fast path:

  // If not, goto slow path below, and start the refill mode

  // * the fill mode will get ended when a trace is built
  trace_cache::tc_entry_t *tc_line = TC->access(pc, br_multi_pred_vec_real);

  trace_cache::tc_insn_iterator tc_line_insn_it(nullptr, nullptr);
  if (tc_line) {
    tc_line_insn_it = tc_line->get_insn_iterator();
//    printf("%s\n", tc_line->disasm_trace().c_str());
  }


  /////////////////////////////
  // Model I$ misses.
  /////////////////////////////
  // Don't check I$ for miss if we hit the Trace Cache
  if (!PERFECT_ICACHE && !tc_line) {
//  if (!PERFECT_ICACHE) {
    line1 = (pc >> L1_IC_LINE_SIZE);
    resolve_cycle1 = IC->Access(Tid, cycle, (line1 << L1_IC_LINE_SIZE), false, &hit1);
    if (IC_INTERLEAVED) {
      // Access next consecutive line.
      line2 = (pc >> L1_IC_LINE_SIZE) + 1;
      resolve_cycle2 = IC->Access(Tid, cycle, (line2 << L1_IC_LINE_SIZE), false, &hit2);
    } else {
      hit2 = true;
    }

    if (!hit1 || !hit2) {
      next_fetch_cycle = MAX((hit1 ? 0 : resolve_cycle1), (hit2 ? 0 : resolve_cycle2));
      assert(next_fetch_cycle > cycle);
      return;
    }
  }

  /////////////////////////////
  // Compose fetch bundle.
  /////////////////////////////

  i = 0;
  stop = false;
  while ((i < fetch_width) && (PERFECT_FETCH || !stop)) {
    if (tc_line && tc_line_insn_it.end()) {
      break;
    }

    //////////////////////////////////////////////////////
    // Fetch instruction -or- inject NOP for fetch stall.
    //////////////////////////////////////////////////////

    if (fetch_exception || fetch_csr || fetch_amo) {
      // Stall the fetch unit if there is a prior unresolved fetch exception, CSR instruction, or AMO instruction.
      // A literal stall may deadlock the Rename Stage: it requires a full bundle from the FQ to progress.
      // Thus, instead of literally stalling the fetch unit, stall it by injecting NOPs after the offending
      // instruction until the offending instruction is resolved in the Retire Stage.
      insn = insn_t(INSN_NOP);
      tc_line = nullptr;
      TC->squash_unfinished_fill();
    } else {
      // Try fetching the instruction via the MMU or TraceCache.
      // Generate a "NOP with fetch exception" if the MMU reference generates an exception.
      reg_t tc_insn_pc = 0;

      try {
        insn = (mmu->load_insn(pc)).insn;
        if (tc_line) {
          insn_t tmp_insn{};
          tc_insn_pc = tc_line_insn_it.get_next_pc();
          tc_line_insn_it.next(&tmp_insn);
          assert(tc_insn_pc == pc);
          assert(tmp_insn.bits() == insn.bits());
        }
      }
      catch (trap_t &t) {
        insn = insn_t(INSN_NOP);
        set_fetch_exception();
        trap_cause = t.cause();
        TC->squash_unfinished_fill();
      }
    }

    if (insn.opcode() == OP_AMO)
      set_fetch_amo();
    else if (insn.opcode() == OP_SYSTEM)
      set_fetch_csr();

    // Put the instruction's information into PAY.
    index = PAY.push();
    PAY.buf[index].inst = insn;
    PAY.buf[index].pc = pc;
    PAY.buf[index].sequence = sequence;
    PAY.buf[index].fetch_exception = fetch_exception;
    PAY.buf[index].fetch_exception_cause = trap_cause;

    //////////////////////////////////////////////////////
    // map_to_actual()
    //////////////////////////////////////////////////////

    // Try to link the instruction to the corresponding instruction in the functional simulator.
    // NOTE: Even when NOPs are injected, successfully mapping to actual is not a problem,
    // as the NOP instructions will never be committed.
    PAY.map_to_actual(this, index, Tid);
    if (PAY.buf[index].good_instruction)
      actual = pipe->peek(PAY.buf[index].db_index);
    else
      actual = (db_t *) NULL;

    if (first_insn) {
      first_insn = false;
      first_insn_db_idx = PAY.buf[index].db_index;
    }

    //////////////////////////////////////////////////////
    // Set next_pc and the prediction tag.
    //////////////////////////////////////////////////////
    // Initialize some predictor-related flags.
    pred_tag = 0;
    history_reg = 0xFFFFFFFF;


    // next-pc select logic for the slow path
    switch (insn.opcode()) {
      case OP_JAL:
        direct_target = JUMP_TARGET;
        next_pc = (PERFECT_BRANCH_PRED ?
                   (actual ? actual->a_next_pc : direct_target) :
                   BP.get_pred(history_reg, pc, insn, direct_target, &pred_tag, &conf, &fm));
        if (tc_line) {
          assert(next_pc == tc_line_insn_it.get_next_pc());
          stop = tc_line_insn_it.end();
        } else {
          assert(next_pc == direct_target);
          stop = true;
        }
        assert(next_pc == direct_target);

        break;

      case OP_JALR:
        next_pc = (PERFECT_BRANCH_PRED ?
                   (actual ? actual->a_next_pc : INCREMENT_PC(pc)) :
                   BP.get_pred(history_reg, pc, insn, 0, &pred_tag, &conf, &fm));
        if (tc_line) {
          assert(tc_line_insn_it.get_next_pc() == UNKNOWN_PC_ADDR);
        }

        stop = true;
        break;

      case OP_BRANCH:
        direct_target = BRANCH_TARGET;
        has_branch_in_curr_bundle = true;

        next_pc = (PERFECT_BRANCH_PRED ?
                   (actual ? actual->a_next_pc : INCREMENT_PC(pc)) :
                   BP.get_multi_pred_for_branch(history_reg,
                                                pc,
                                                insn,
                                                direct_target,
                                                &pred_valid,
                                                &pred_tag,
                                                &fm,
                                                &conf));

        assert((next_pc == pc) || (next_pc == direct_target) || (next_pc == INCREMENT_PC(pc)));
        assert((next_pc == pc) == !pred_valid);

        if (tc_line) {
          if (!tc_line->end_with_br)
            assert(next_pc == tc_line_insn_it.get_next_pc());
          else
            assert(
              (INCREMENT_PC(pc) == tc_line_insn_it.get_next_pc()) ||
                (direct_target) == tc_line_insn_it.get_next_pc()
            );

          assert(pred_valid);
          stop = tc_line_insn_it.end();
        } else {
          if (!pred_valid || (next_pc != INCREMENT_PC(pc)))
            stop = true;
        }

        if (!pred_valid) {
          PAY.undo_push();
        }
        break;

      default:
        next_pc = INCREMENT_PC(pc);
        if (tc_line) {
          assert(next_pc == tc_line_insn_it.get_next_pc());
        }
        break;
    }

    // Set payload buffer entry's next_pc and pred_tag.
    PAY.buf[index].next_pc = next_pc;
    PAY.buf[index].pred_tag = pred_tag;

    // Latch instruction into fetch-decode pipeline register.
    DECODE[i].valid = pred_valid;
    DECODE[i].index = index;

    // Keep count of number of fetched instructions.
    i++;

    // If not already stopped:
    // Stop if the I$ is not interleaved and if a line boundary is crossed.
    if (!tc_line && !stop && !IC_INTERLEAVED) {
//    if (!stop && !IC_INTERLEAVED) {
      line1 = (pc >> L1_IC_LINE_SIZE);
      line2 = (next_pc >> L1_IC_LINE_SIZE);
      stop = (line1 != line2);
    }

    // Go to next PC.
    pc = next_pc;
    state.pc = pc;
    sequence++;

    TC->feed(&PAY, index, br_multi_pred_vec_real);
  }            // while()

  if (has_branch_in_curr_bundle) {
    uint32_t
      useful_bit = mk_multi_br_pred_perfect(this->get_pipe(), first_insn_db_idx, Tid, &br_multi_pred_vec_perfect);
    stat_pred_accuracy(br_multi_pred_vec_real, br_multi_pred_vec_perfect, useful_bit);
  }

}            // fetch()
