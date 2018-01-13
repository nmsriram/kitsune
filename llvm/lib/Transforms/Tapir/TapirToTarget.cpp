//===- LowerToCilk.cpp - Convert Tapir into Cilk runtime calls ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass converts functions that include Tapir instructions to call out to
// the Cilk runtime system.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Tapir/CilkABI.h"
#include "llvm/Transforms/Tapir/OpenMPABI.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Tapir.h"
#include "llvm/Transforms/Tapir/TapirUtils.h"

#define DEBUG_TYPE "tapir2target"

using namespace llvm;

namespace {

struct LowerTapirToTarget : public ModulePass {
  static char ID; // Pass identification, replacement for typeid
  TapirTarget* tapirTarget;
  explicit LowerTapirToTarget(TapirTarget* tapirTarget = nullptr)
      : ModulePass(ID), tapirTarget(tapirTarget) {
    assert(tapirTarget);
    initializeLowerTapirToTargetPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override {
    return "Simple Lowering of Tapir to Target ABI";
  }

  bool runOnModule(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<DominatorTreeWrapperPass>();
  }
private:
  ValueToValueMapTy DetachCtxToStackFrame;
  bool unifyReturns(Function &F);
  SmallVectorImpl<Function *> *processFunction(Function &F, DominatorTree &DT,
                                               AssumptionCache &AC);
};
}  // End of anonymous namespace

char LowerTapirToTarget::ID = 0;
INITIALIZE_PASS_BEGIN(LowerTapirToTarget, "tapir2target",
                      "Simple Lowering of Tapir to Target ABI", false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(LowerTapirToTarget, "tapir2target",
                    "Simple Lowering of Tapir to Target ABI", false, false)


bool LowerTapirToTarget::unifyReturns(Function &F) {
  SmallVector<BasicBlock *, 4> ReturningBlocks;
  for (BasicBlock &BB : F)
    if (isa<ReturnInst>(BB.getTerminator()))
      ReturningBlocks.push_back(&BB);

  // If this function already has a single return, then terminate early.
  if (ReturningBlocks.size() == 1)
    return false;

  BasicBlock *NewRetBlock = BasicBlock::Create(F.getContext(),
                                               "UnifiedReturnBlock", &F);
  PHINode *PN = nullptr;
  if (F.getReturnType()->isVoidTy()) {
    ReturnInst::Create(F.getContext(), nullptr, NewRetBlock);
  } else {
    // If the function doesn't return void... add a PHI node to the block...
    PN = PHINode::Create(F.getReturnType(), ReturningBlocks.size(),
                         "UnifiedRetVal");
    NewRetBlock->getInstList().push_back(PN);
    ReturnInst::Create(F.getContext(), PN, NewRetBlock);
  }

  // Loop over all of the blocks, replacing the return instruction with an
  // unconditional branch.
  //
  for (BasicBlock *BB : ReturningBlocks) {
    // Add an incoming element to the PHI node for every return instruction that
    // is merging into this new block...
    if (PN)
      PN->addIncoming(BB->getTerminator()->getOperand(0), BB);

    BB->getInstList().pop_back();  // Remove the return insn
    BranchInst::Create(NewRetBlock, BB);
  }
  return true;
}

SmallVectorImpl<Function *>
*LowerTapirToTarget::processFunction(Function &F, DominatorTree &DT,
                                     AssumptionCache &AC) {
  if (unifyReturns(F))
    DT.recalculate(F);

  tapirTarget->preProcessFunction(F);

  bool changed = false;
  // Lower Tapir instructions in this function.  Collect the set of helper
  // functions generated by this process.
  SmallVector<Function *, 4> *NewHelpers = new SmallVector<Function *, 4>();
  for (Function::iterator I = F.begin(), E = F.end(); I != E; ++I) {
    if (DetachInst* DI = dyn_cast_or_null<DetachInst>(I->getTerminator())) {
      changed = true;
      // Lower a detach instruction, and collect the helper function generated
      // in this process for executing the detached task.
      Function *Helper = tapirTarget->createDetach(*DI, DetachCtxToStackFrame, DT, AC);
      NewHelpers->push_back(Helper);
    } else if (SyncInst* SI = dyn_cast_or_null<SyncInst>(I->getTerminator())) {
      // Lower a sync instruction.
      changed = true;
      tapirTarget->createSync(*SI, DetachCtxToStackFrame);
    }
  }

  if (!changed) return NewHelpers;

  if (verifyFunction(F, &errs())) {
    DEBUG(F.dump());
    assert(0);
  }

  tapirTarget->postProcessFunction(F);
  for (Function *H : *NewHelpers) {
    tapirTarget->postProcessHelper(*H);
  }

  return NewHelpers;
}

bool LowerTapirToTarget::runOnModule(Module &M) {
  if (skipModule(M))
    return false;

  // Add functions that detach to the work list.
  SmallVector<Function *, 4> WorkList;
  for (Function &F : M) {
    if (F.getName() == "main")
      WorkList.push_back(&F);

    for (BasicBlock &BB : F)
      if (isa<DetachInst>(BB.getTerminator())) {
        WorkList.push_back(&F);
        break;
      }
  }

  if (WorkList.empty())
    return false;

  bool Changed = false;
  std::unique_ptr<SmallVectorImpl<Function *>> NewHelpers;
  while (!WorkList.empty()) {
    // Process the next function.
    Function *F = WorkList.back();
    WorkList.pop_back();
    DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>(*F).getDomTree();
    AssumptionCacheTracker &ACT = getAnalysis<AssumptionCacheTracker>();
    NewHelpers.reset(processFunction(*F, DT, ACT.getAssumptionCache(*F)));
    Changed |= !NewHelpers->empty();
    // Check the generated helper functions to see if any need to be processed,
    // that is, to see if any of them themselves detach a subtask.
    for (Function *Helper : *NewHelpers)
      for (BasicBlock &BB : *Helper)
        if (isa<DetachInst>(BB.getTerminator()))
          WorkList.push_back(Helper);
  }
  return Changed;
}

// createLowerTapirToTargetPass - Provide an entry point to create this pass.
//
namespace llvm {
ModulePass *createLowerTapirToTargetPass(TapirTarget* tapirTarget) {
  return new LowerTapirToTarget(tapirTarget);
}
}
