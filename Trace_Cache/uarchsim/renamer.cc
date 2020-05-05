#include <cstring>
#include "renamer.h"
#include "BitmapUtils.h"
#include "pipeline.h"

/////////////////////////////////////////////////////////////////////
// Private functions.
// e.g., a generic function to copy state from one map to another.
/////////////////////////////////////////////////////////////////////

void renamer::initialize_structure() {
  // init unresolved branch tracking system
  // clear GBM, no need to init the value in shadow map array
  mGBM = Bitmap(m_n_branches);

  for (uint8_t i = 0; i < m_n_branches; ++i) {
    mShadowMapArray[i].saved_RMT = std::unique_ptr<rmt_entry_t[]>(new rmt_entry_t[m_n_log_regs]);
    mShadowMapArray[i].saved_GBM = Bitmap(m_n_branches);
  }

  // init value and readiness of registers
  for (uint64_t i = 0; i < m_n_phy_regs; ++i) {
    mPRF[i] = 0;
    mRdyPRF[i] = true;
  }

  // init reg renaming system
  for (uint64_t i = 0; i < m_n_log_regs; ++i) {
    mAMT[i] = i;
    mRMT[i].valid = false;
    mRMT[i].phy_reg_idx = 0;
  }

  mFreeList.reset();
  // init free list
  for (uint64_t i = 0; i < m_n_fl_size; ++i) {
    mFreeList.push(m_n_log_regs + i);
  }

  // init active list
  mActiveList.reset();
}


////////////////////////////////////////
// Public functions.
////////////////////////////////////////

/////////////////////////////////////////////////////////////////////
// This is the constructor function.
// When a renamer object is instantiated, the caller indicates:
// 1. The number of logical registers (e.g., 32).
// 2. The number of physical registers (e.g., 128).
// 3. The maximum number of unresolved branches.
//    Requirement: 1 <= n_branches <= 64.
//
// Tips:
//
// Assert the number of physical registers > number logical registers.
// Assert 1 <= n_branches <= 64.
// Then, allocate space for the primary data structures.
// Then, initialize the data structures based on the knowledge
// that the pipeline is intially empty (no in-flight instructions yet).
/////////////////////////////////////////////////////////////////////
renamer::renamer(uint64_t n_log_regs, uint64_t n_phys_regs,
                 uint64_t n_branches) :
    mFreeList(n_phys_regs - n_log_regs),
    mActiveList((uint64_t) ACTIVE_LIST_SIZE),
    m_n_log_regs(n_log_regs), m_n_phy_regs(n_phys_regs), m_n_branches((uint8_t) n_branches),
    m_n_al_size((uint64_t) ACTIVE_LIST_SIZE),
    m_n_fl_size(n_phys_regs - n_log_regs){
  assert(ACTIVE_LIST_SIZE > 0);
  assert(1 <= n_branches && n_branches <= 64);
  assert(n_phys_regs > n_log_regs);

  mRMT = std::unique_ptr<rmt_entry_t[]>(new rmt_entry_t[m_n_log_regs]);
  mAMT = std::unique_ptr<phy_reg_idx_t[]>(new phy_reg_idx_t[m_n_log_regs]);
  mPRF = std::unique_ptr<reg_t[]>(new reg_t[m_n_phy_regs]);
  mRdyPRF = std::unique_ptr<bool[]>(new bool[m_n_phy_regs]);
  mShadowMapArray = std::unique_ptr<shadow_map_entry_t[]>(new shadow_map_entry_t[m_n_branches]);

  initialize_structure();
}

/////////////////////////////////////////////////////////////////////
// This is the destructor, used to clean up memory space and
// other things when simulation is done.
// I typically don't use a destructor; you have the option to keep
// this function empty.
/////////////////////////////////////////////////////////////////////
renamer::~renamer() = default;

//////////////////////////////////////////
// Functions related to Rename Stage.   //
//////////////////////////////////////////

/////////////////////////////////////////////////////////////////////
// The Rename Stage must stall if there aren't enough free physical
// registers available for renaming all logical destination registers
// in the current rename bundle.
//
// Inputs:
// 1. bundle_dst: number of logical destination registers in
//    current rename bundle
//
// Return value:
// Return "true" (stall) if there aren't enough free physical
// registers to allocate to all of the logical destination registers
// in the current rename bundle.
/////////////////////////////////////////////////////////////////////
bool renamer::stall_reg(uint64_t bundle_dst) {
  bool stall = mFreeList.size() < bundle_dst;

  return stall;
}

/////////////////////////////////////////////////////////////////////
// The Rename Stage must stall if there aren't enough free
// checkpoints for all branches in the current rename bundle.
//
// Inputs:
// 1. bundle_branch: number of branches in current rename bundle
//
// Return value:
// Return "true" (stall) if there aren't enough free checkpoints
// for all branches in the current rename bundle.
/////////////////////////////////////////////////////////////////////
bool renamer::stall_branch(uint64_t bundle_branch) {
  bool stall = mGBM.GetAvail() < bundle_branch;

  return stall;
}

/////////////////////////////////////////////////////////////////////
// This function is used to get the branch mask for an instruction.
/////////////////////////////////////////////////////////////////////
uint64_t renamer::get_branch_mask() {
  uint64_t curr_gbm = mGBM.GetBitmap();

  return curr_gbm;
}

/////////////////////////////////////////////////////////////////////
// This function is used to rename a single source register.
//
// Inputs:
// 1. log_reg: the logical register to rename
//
// Return value: physical register name
/////////////////////////////////////////////////////////////////////
uint64_t renamer::rename_rsrc(uint64_t log_reg) {
  assert(0 <= log_reg && log_reg < m_n_log_regs);

  phy_reg_idx_t actual_pr;
  if (mRMT[log_reg].valid) {
    actual_pr = mRMT[log_reg].phy_reg_idx;
  } else {
    actual_pr = mAMT[log_reg];
  }

  return actual_pr;
}

/////////////////////////////////////////////////////////////////////
// This function is used to rename a single destination register.
//
// Inputs:
// 1. log_reg: the logical register to rename
//
// Return value: physical register name
/////////////////////////////////////////////////////////////////////
uint64_t renamer::rename_rdst(uint64_t log_reg) {
  assert(log_reg != 0);
  assert(0 < log_reg && log_reg < m_n_log_regs);
  assert(!mFreeList.empty());

  uint64_t alloc_phy_reg = mFreeList.pop();
  mRMT[log_reg].phy_reg_idx = alloc_phy_reg;
  mRMT[log_reg].valid = true;

  return alloc_phy_reg;
}

/////////////////////////////////////////////////////////////////////
// This function creates a new branch checkpoint.
//
// Inputs: none.
//
// Output:
// 1. The function returns the branch's ID. When the branch resolves,
//    its ID is passed back to the renamer via "resolve()" below.
//
// Tips:
//
// Allocating resources for the branch (a GBM bit and a checkpoint):
// * Find a free bit -- i.e., a '0' bit -- in the GBM. Assert that
//   a free bit exists: it is the user's responsibility to avoid
//   a structural hazard by calling stall_branch() in advance.
// * Set the bit to '1' since it is now in use by the new branch.
// * The position of this bit in the GBM is the branch's ID.
// * Use the branch checkpoint that corresponds to this bit.
//
// The branch checkpoint should contain the following:
// 1. Shadow Map Table (checkpointed Rename Map Table)
// 2. checkpointed Free List head index
// 3. checkpointed GBM
/////////////////////////////////////////////////////////////////////
uint64_t renamer::checkpoint() {
  assert(mGBM.GetAvail() > 0);
  Bitmap old_GBM = mGBM;
  uint8_t br_id = mGBM.GetFirstFreeBitPos();
  assert(br_id != m_n_branches);
  mGBM.SetBit(br_id);

  mShadowMapArray[br_id].saved_free_list_head = mFreeList.head_idx();
  mShadowMapArray[br_id].saved_GBM = old_GBM;
  memcpy(
      (void *) mShadowMapArray[br_id].saved_RMT.get(),
      (void *) mRMT.get(),
      m_n_log_regs * sizeof(rmt_entry_t)
  );

  return br_id;
}

//////////////////////////////////////////
// Functions related to Dispatch Stage. //
//////////////////////////////////////////

/////////////////////////////////////////////////////////////////////
// The Dispatch Stage must stall if there are not enough free
// entries in the Active List for all instructions in the current
// dispatch bundle.
//
// Inputs:
// 1. bundle_inst: number of instructions in current dispatch bundle
//
// Return value:
// Return "true" (stall) if the Active List does not have enough
// space for all instructions in the dispatch bundle.
/////////////////////////////////////////////////////////////////////
bool renamer::stall_dispatch(uint64_t bundle_inst) {
  bool stall = mActiveList.available() < bundle_inst;

  return stall;
}

/////////////////////////////////////////////////////////////////////
// This function dispatches a single instruction into the Active
// List.
//
// Inputs:
// 1. dest_valid: If 'true', the instr. has a destination register,
//    otherwise it does not. If it does not, then the log_reg and
//    phys_reg inputs should be ignored.
// 2. log_reg: Logical register number of the instruction's
//    destination.
// 3. phys_reg: Physical register number of the instruction's
//    destination.
// 4. load: If 'true', the instr. is a load, otherwise it isn't.
// 5. store: If 'true', the instr. is a store, otherwise it isn't.
// 6. branch: If 'true', the instr. is a branch, otherwise it isn't.
// 7. amo: If 'true', this is an atomic memory operation.
// 8. csr: If 'true', this is a system instruction.
// 9. PC: Program counter of the instruction.
//
// Return value:
// Return the instruction's index in the Active List.
//
// Tips:
//
// Before dispatching the instruction into the Active List, assert
// that the Active List isn't full: it is the user's responsibility
// to avoid a structural hazard by calling stall_dispatch()
// in advance.
/////////////////////////////////////////////////////////////////////
uint64_t renamer::dispatch_inst(bool dest_valid, uint64_t log_reg,
                                uint64_t phys_reg, bool load, bool store,
                                bool branch, bool amo, bool csr, uint64_t PC) {

  assert(!mActiveList.full());

  active_list_entry_t new_data;
  new_data.complete = false;
  new_data.squash_br_mispred = false;
  new_data.squash_exception = false;
  new_data.squash_load_violate = false;
  new_data.squash_val_mispred = false;

  new_data.flag_has_dst_reg = dest_valid;
  new_data.dst_logic_reg_id = log_reg;
  new_data.dst_phy_reg_id = phys_reg;
  new_data.flag_load = load;
  new_data.flag_store = store;
  new_data.flag_branch = branch;
  new_data.flag_atomic = amo;
  new_data.flag_csr = csr;
  new_data.inst_pc = PC;

  size_t al_idx = mActiveList.push(new_data);
  return al_idx;
}

//////////////////////////////////////////
// Functions related to Schedule Stage. //
//////////////////////////////////////////

/////////////////////////////////////////////////////////////////////
// Test the ready bit of the indicated physical register.
// Returns 'true' if ready.
/////////////////////////////////////////////////////////////////////
bool renamer::is_ready(uint64_t phys_reg) {
  assert(0 <= phys_reg && phys_reg < m_n_phy_regs);

  return mRdyPRF[phys_reg];
}

/////////////////////////////////////////////////////////////////////
// Clear the ready bit of the indicated physical register.
/////////////////////////////////////////////////////////////////////
void renamer::clear_ready(uint64_t phys_reg) {
  assert(0 <= phys_reg && phys_reg < m_n_phy_regs);

  mRdyPRF[phys_reg] = false;
}

/////////////////////////////////////////////////////////////////////
// Set the ready bit of the indicated physical register.
/////////////////////////////////////////////////////////////////////
void renamer::set_ready(uint64_t phys_reg) {
  assert(0 <= phys_reg && phys_reg < m_n_phy_regs);

  mRdyPRF[phys_reg] = true;
}

//////////////////////////////////////////
// Functions related to Reg. Read Stage.//
//////////////////////////////////////////

/////////////////////////////////////////////////////////////////////
// Return the contents (value) of the indicated physical register.
/////////////////////////////////////////////////////////////////////
uint64_t renamer::read(uint64_t phys_reg) {
  assert(0 <= phys_reg && phys_reg < m_n_phy_regs);

  return mPRF[phys_reg];
}

//////////////////////////////////////////
// Functions related to Writeback Stage.//
//////////////////////////////////////////

/////////////////////////////////////////////////////////////////////
// Write a value into the indicated physical register.
/////////////////////////////////////////////////////////////////////
void renamer::write(uint64_t phys_reg, uint64_t value) {
  assert(0 <= phys_reg && phys_reg < m_n_phy_regs);

  mPRF[phys_reg] = value;
}

/////////////////////////////////////////////////////////////////////
// Set the completed bit of the indicated entry in the Active List.
/////////////////////////////////////////////////////////////////////
void renamer::set_complete(uint64_t AL_index) {
  active_list_entry_t &curr_inst_al_record = mActiveList.at(AL_index);
  curr_inst_al_record.complete = true;
}

/////////////////////////////////////////////////////////////////////
// This function is for handling branch resolution.
//
// Inputs:
// 1. AL_index: Index of the branch in the Active List.
// 2. branch_ID: This uniquely identifies the branch and the
//    checkpoint in question.  It was originally provided
//    by the checkpoint function.
// 3. correct: 'true' indicates the branch was correctly
//    predicted, 'false' indicates it was mispredicted
//    and recovery is required.
//
// Outputs: none.
//
// Tips:
//
// While recovery is not needed in the case of a correct branch,
// some actions are still required with respect to the GBM and
// all checkpointed GBMs:
// * Remember to clear the branch's bit in the GBM.
// * Remember to clear the branch's bit in all checkpointed GBMs.
//
// In the case of a misprediction:
// * Restore the GBM from the checkpoint. Also make sure the
//   mispredicted branch's bit is cleared in the restored GBM,
//   since it is now resolved and its bit and checkpoint are freed.
// * You don't have to worry about explicitly freeing the GBM bits
//   and checkpoints of branches that are after the mispredicted
//   branch in program order. The mere act of restoring the GBM
//   from the checkpoint achieves this feat.
// * Restore other state using the branch's checkpoint.
//   In addition to the obvious state ...  *if* you maintain a
//   freelist length variable (you may or may not), you must
//   recompute the freelist length. It depends on your
//   implementation how to recompute the length.
//   (Note: you cannot checkpoint the length like you did with
//   the head, because the tail can change in the meantime;
//   you must recompute the length in this function.)
// * Do NOT set the branch misprediction bit in the active list.
//   (Doing so would cause a second, full squash when the branch
//   reaches the head of the Active List. We don’t want or need
//   that because we immediately recover within this function.)
/////////////////////////////////////////////////////////////////////
void renamer::resolve(uint64_t AL_index, uint64_t branch_ID, bool correct) {
  assert(branch_ID < m_n_branches);
  assert(mActiveList.at(AL_index).flag_branch);
  assert(mGBM.TestBit((uint8_t) branch_ID));

  active_list_entry_t &curr_br_al_record = mActiveList.at(AL_index);

  if (correct) {
    mGBM.UnsetBit((uint8_t) branch_ID);
    // iterate all valid checkpoint and clear the corresponding saved GBM bit
    for (
        uint8_t curr_iter_br_id = mGBM.GetFirstSetBitPos();
        curr_iter_br_id < m_n_branches;
        curr_iter_br_id = mGBM.GetFirstSetBitPos(curr_iter_br_id + (uint8_t) 1)
        ) {
      mShadowMapArray[curr_iter_br_id].saved_GBM.UnsetBit((uint8_t) branch_ID);
    }

  } else {
    mGBM = mShadowMapArray[branch_ID].saved_GBM;
    assert(!mGBM.TestBit((uint8_t) branch_ID));

    mFreeList.restore_head_idx(mShadowMapArray[branch_ID].saved_free_list_head);

    memcpy(
        mRMT.get(),
        mShadowMapArray[branch_ID].saved_RMT.get(),
        m_n_log_regs * sizeof(rmt_entry_t)
    );

    mActiveList.drop_newer(AL_index);
  }
}

//////////////////////////////////////////
// Functions related to Retire Stage.   //
//////////////////////////////////////////

///////////////////////////////////////////////////////////////////
// This function allows the caller to examine the instruction at the head
// of the Active List.
//
// Input arguments: none.
//
// Return value:
// * Return "true" if the Active List is NOT empty, i.e., there
//   is an instruction at the head of the Active List.
// * Return "false" if the Active List is empty, i.e., there is
//   no instruction at the head of the Active List.
//
// Output arguments:
// Simply return the following contents of the head entry of
// the Active List.  These are don't-cares if the Active List
// is empty (you may either return the contents of the head
// entry anyway, or not set these at all).
// * completed bit
// * exception bit
// * load violation bit
// * branch misprediction bit
// * value misprediction bit
// * load flag (indicates whether or not the instr. is a load)
// * store flag (indicates whether or not the instr. is a store)
// * branch flag (indicates whether or not the instr. is a branch)
// * amo flag (whether or not instr. is an atomic memory operation)
// * csr flag (whether or not instr. is a system instruction)
// * program counter of the instruction
/////////////////////////////////////////////////////////////////////
bool renamer::precommit(bool &completed, bool &exception, bool &load_viol,
                        bool &br_misp, bool &val_misp, bool &load, bool &store,
                        bool &branch, bool &amo, bool &csr, uint64_t &PC) {
  bool is_al_not_empty = !mActiveList.empty();

  if (is_al_not_empty) {
    active_list_entry_t &inst_to_commit = mActiveList.at(mActiveList.head_idx());

    completed = inst_to_commit.complete;
    exception = inst_to_commit.squash_exception;
    load_viol = inst_to_commit.squash_load_violate;
    br_misp = inst_to_commit.squash_br_mispred;
    val_misp = inst_to_commit.squash_val_mispred;
    load = inst_to_commit.flag_load;
    store = inst_to_commit.flag_store;
    branch = inst_to_commit.flag_branch;
    amo = inst_to_commit.flag_atomic;
    csr = inst_to_commit.flag_csr;
    PC = inst_to_commit.inst_pc;
  }

  return is_al_not_empty;
}

/////////////////////////////////////////////////////////////////////
// This function commits the instruction at the head of the Active List.
//
// Tip (optional but helps catch bugs):
// Before committing the head instruction, assert that it is valid to
// do so (use assert() from standard library). Specifically, assert
// that all of the following are true:
// - there is a head instruction (the active list isn't empty)
// - the head instruction is completed
// - the head instruction is not marked as an exception
// - the head instruction is not marked as a load violation
// It is the caller's (pipeline's) duty to ensure that it is valid
// to commit the head instruction BEFORE calling this function
// (by examining the flags returned by "precommit()" above).
// This is why you should assert() that it is valid to commit the
// head instruction and otherwise cause the simulator to exit.
/////////////////////////////////////////////////////////////////////
void renamer::commit() {
  assert(!mActiveList.empty());

  active_list_entry_t &inst_to_commit = mActiveList.at(mActiveList.head_idx());
  assert(inst_to_commit.complete);
  assert(!inst_to_commit.squash_exception);
  assert(!inst_to_commit.squash_load_violate);

  if (inst_to_commit.flag_has_dst_reg) {
    // update RMT
    if (mRMT[inst_to_commit.dst_logic_reg_id].valid &&
        mRMT[inst_to_commit.dst_logic_reg_id].phy_reg_idx == inst_to_commit.dst_phy_reg_id) {
      mRMT[inst_to_commit.dst_logic_reg_id].valid = false;
      mRMT[inst_to_commit.dst_logic_reg_id].phy_reg_idx = 0;
    }
    // commit change to AMT and free the unused phy reg
    phy_reg_idx_t phy_reg_to_free = mAMT[inst_to_commit.dst_logic_reg_id];
    mAMT[inst_to_commit.dst_logic_reg_id] = inst_to_commit.dst_phy_reg_id;
    mFreeList.push(phy_reg_to_free);
  }

  mActiveList.pop();
}

//////////////////////////////////////////////////////////////////////
// Squash the renamer class.
//
// Squash all instructions in the Active List and think about which
// structures in your renamer class need to be restored, and how.
//
// After this function is called, the renamer should be rolled-back
// to the committed state of the machine and all renamer state
// should be consistent with an empty pipeline.
/////////////////////////////////////////////////////////////////////
void renamer::squash() {
  for (logic_reg_idx_t log_reg = 0; log_reg < m_n_log_regs; ++log_reg) {
    mRMT[log_reg].valid = false;
  }

  mActiveList.reset();
  mFreeList.restore_head_idx(mFreeList.tail_idx());

  mGBM.ClearBitmap();
}

//////////////////////////////////////////
// Functions not tied to specific stage.//
//////////////////////////////////////////

/////////////////////////////////////////////////////////////////////
// Functions for individually setting the exception bit,
// load violation bit, branch misprediction bit, and
// value misprediction bit, of the indicated entry in the Active List.
/////////////////////////////////////////////////////////////////////
void renamer::set_exception(uint64_t AL_index) {
  mActiveList.at(AL_index).squash_exception = true;
}

void renamer::set_load_violation(uint64_t AL_index) {
  mActiveList.at(AL_index).squash_load_violate = true;
}

void renamer::set_branch_misprediction(uint64_t AL_index) {
  mActiveList.at(AL_index).squash_br_mispred = true;
}

void renamer::set_value_misprediction(uint64_t AL_index) {
  mActiveList.at(AL_index).squash_val_mispred = true;
}

/////////////////////////////////////////////////////////////////////
// Query the exception bit of the indicated entry in the Active List.
/////////////////////////////////////////////////////////////////////
bool renamer::get_exception(uint64_t AL_index) {
  return mActiveList.at(AL_index).squash_exception;
}

