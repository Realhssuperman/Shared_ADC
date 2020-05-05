#ifndef BPRED_INTERFACE_H
#define BPRED_INTERFACE_H
//-------------------------------------------------------------------
//-------------------------------------------------------------------
//
// Other files assumed to be included before this file:
//
#include "decode.h"
#include "parameters.h"
//
//-------------------------------------------------------------------
//-------------------------------------------------------------------

#define BHR_RSHIFT

#define BHR_LEN      16u
#ifdef BHR_RSHIFT
#define HIST_MASK    0xfffcu
#define HIST_BIT     0x8000u
#define PC_MASK      0x3fffu
#define gen_next_history(old_history, taken) ((((old_history)>>1u)|((taken)?HIST_BIT:0u))&HIST_MASK)
#else
#define HIST_MASK    0x3fffu
#define HIST_BIT     0x0001u
#define PC_MASK      0x3fffu
#define gen_next_history(old_history, taken) ((((old_history)<<1u)|((taken)?HIST_BIT:0u))&HIST_MASK)
#endif



#if 0
//////////////////////////////////
// Simple predictor (2K bimodal)
//////////////////////////////////

#define HIST_MASK	0x000
#define HIST_BIT	0x400
#define PC_MASK		INDEX_MASK

#endif



////////////////////////////////////////////////////////////////////////////
//
// STRUCTURES and CLASSES
//
////////////////////////////////////////////////////////////////////////////

class stats_t;

class BpredPredictAutomaton {
 public:
  uint32_t tag;
  uint32_t pred;
  uint8_t hyst;

  BpredPredictAutomaton() {
    tag = 0;
    hyst = 0;
    pred = 0;
  }

  ~BpredPredictAutomaton() {
  }

  void update(uint32_t outcome) {
    if (outcome == pred) {
      if (hyst < 1) {
        hyst++;
      }
    } else {
      if (hyst) {
        hyst--;
      } else {
        pred = outcome;
      }
    }
  }

  void conf_update(uint32_t outcome, bool use_reset, uint32_t max_value) {
    if (outcome) {
      if (pred < max_value) {
        pred++;
      }
    } else {
      if (use_reset) {
        pred = 0;
      } else if (pred) {
        pred--;
      }
    }
  }

};

class RAS_node_b {
 public:
  uint32_t addr;
  RAS_node_b *next;

  RAS_node_b() {
    next = NULL;
  }

  ~RAS_node_b() {
    if (next) {
      delete next;
    }
  }
};

class CTI_entry_b {
 public:
  uint32_t multi_bp_tbl_index;
  uint32_t multi_bp_tbl_col;

  int RAS_action; // 1 push, -1 pop, 0 nothing
  uint32_t RAS_address;
  bool flush_RAS;
  uint32_t history;

  uint32_t pc;
  insn_t inst;
  uint32_t comp_target;
  uint32_t original_pred;
  uint32_t target;
  bool taken;

  bool use_BTB;
  bool is_return;
  bool is_call;
  bool is_cond;

  uint32_t state;
  uint32_t conf;
  uint32_t fm;        // "FM"
  bool is_conf;

  bool use_global_history;
  uint32_t global_history;
};
class Future_Br_Pred_t {
 public:
  bool taken;
  uint32_t pred_index;
//  uint32_t pred_col;
};

//-------------------------------------------------------------------
//-------------------------------------------------------------------
//
// CLASS FOR INTERFACE
//
//-------------------------------------------------------------------
//-------------------------------------------------------------------
class bpred_interface {

 private:

  stats_t *stats;

  //
  // Internal functions
  //
  void update();
  void RAS_update();
  bool RAS_lookup(uint32_t *target);
  void update_predictions(bool fm);                // "FM"
  void update_multi_predictions();
  void make_predictions(unsigned int branch_history);
  bool multi_pred_make_predictions(unsigned int branch_history);
  void decode();


  //
  // Variables
  //

  CTI_entry_b *cti_Q;
  unsigned int cti_head;
  unsigned int cti_tail;

  RAS_node_b *RAS;

  BpredPredictAutomaton *BTB;
//  BpredPredictAutomaton *pred_table;
  BpredPredictAutomaton *conf_table;
  BpredPredictAutomaton *fm_table;    // "FM": false-misprediction estimator
  BpredPredictAutomaton **multi_pred_table;// New for multiple prediction
  // This trail train buffer is used to track the CTI which have already
  //   retired.
  // The reason we need this is when those instruction are retiring, we
  //   can only train their own prediction bit, but their following
  //   branch prediction bit is still need to be train using the branch
  //   retired in future, so we need to keep their information until all the
  //   related future branch are retired
  // The size of this buffer is MULTI_PRED_NUM_PRED-1 (number of bit still
  //   need to be trained when the branch is retired and have trained the bit
  //   for it's self)
//  CTI_entry_b *multi_pred_table_trail_train_buf;// New for multiple prediction
  Future_Br_Pred_t *multi_pred_buf; // [0] - first pred, [1] - second pred, ..
  size_t multi_pred_buf_avail_idx;
//  size_t multi_pred_table_trail_train_buf_tail;

 public:


//-------------------------------------------------------------------
//
// Stat variables
//
//-------------------------------------------------------------------

  unsigned int stat_num_pred;
  unsigned int stat_num_miss;
  unsigned int stat_num_cond_pred;
  unsigned int stat_num_cond_miss;
  unsigned int stat_num_flush_RAS;
  unsigned int stat_num_conf_corr;
  unsigned int stat_num_conf_ncorr;
  unsigned int stat_num_nconf_corr;
  unsigned int stat_num_nconf_ncorr;


//-------------------------------------------------------------------
//
// Constructor / Destructor
//
//-------------------------------------------------------------------


  //
  // Constructor
  //
  // Initialize control flow prediction stuff.
  //
  bpred_interface();

  //
  // Destructor
  //
  // Generic destructor
  //
  ~bpred_interface();


//-------------------------------------------------------------------
//
// Functions for prediction
//
//-------------------------------------------------------------------

  //
  // 	get_pred()
  //
  // Get the next prediction.
  // Returns the predicted target, and also assigns a tag to the prediction,
  // which is used as a kind of sequence number by 'update_pred'.
  //
  unsigned int get_pred(unsigned int branch_history,
                        unsigned int PC, insn_t inst,
                        unsigned int comp_target, unsigned int *pred_tag);
  unsigned int get_pred(unsigned int branch_history,
                        unsigned int PC, insn_t inst,
                        unsigned int comp_target, unsigned int *pred_tag,
                        bool *conf,
                        bool *fm);    // "FM"

  unsigned int get_multi_pred_for_branch(unsigned int branch_history,
                                         unsigned int PC,
                                         insn_t inst,
                                         unsigned int comp_target,
                                         bool *pred_vaild,
                                         unsigned int *pred_tag,
                                         bool *fm,
                                         bool *conf);
  void prepare_multi_pred_future_buf(unsigned int PC, unsigned int *pred_vec);
  void multi_pred_notify_branch(reg_t comp_target, insn_t inst);

  void set_nconf(unsigned int pred_tag);
  //
  //	    fix_pred()
  //
  // Tells the predictor that the instruciton has executed and was found
  // to be mispredicted.  This backs up the history to the point after the
  // misprediction.
  //
  // The prediction is identified via a tag, originally gotten from get_pred.
  //
  void fix_pred(unsigned int pred_tag, unsigned int next_pc);

  //
  //      verify_pred()
  //
  // Tells the predictor to go ahead and update state for a given prediction.
  //
  // The prediction is identified via a tag, originally gotten from get_pred.
  //
  void verify_pred(unsigned int pred_tag, unsigned int next_pc,
                   bool fm);        // "FM"


  //  flush()
  //
  // flush pending predictions
  //
  void flush();

  //
  // Dump config and stats
  //
  void dump_config(FILE *fp);
  void dump_stats(FILE *fp);
  void set_stats(stats_t *_stats) { this->stats = _stats; }

  unsigned int update_global_history(unsigned int ghistory,
                                     insn_t inst, unsigned int pc,
                                     unsigned int next_pc);

}; // end of bpred_interface class definition

#endif //BPRED_INTERFACE_H
