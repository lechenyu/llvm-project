#include "NVPTX.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Pass.h"

using namespace llvm;

namespace llvm {
void initializeNVPTXBallistaInstrumentPass(PassRegistry &);
}

namespace {
class NVPTXBallistaInstrument : public FunctionPass {
  bool runOnFunction(Function &F) override;

public:
  static char ID; // Pass identification, replacement for typeid
  NVPTXBallistaInstrument() : FunctionPass(ID) {}
  StringRef getPassName() const override {
    return "Record memory access to mapped variables";
  }
};
} // namespace

char NVPTXBallistaInstrument::ID = 1;

INITIALIZE_PASS(NVPTXBallistaInstrument, "nvptx-ballista-instrument",
                "Record memory access (Ballista)", false, false)

bool NVPTXBallistaInstrument::runOnFunction(Function &F) {
  // if (F.getName() == "__omp_outlined__1") {
  //   for (auto &BB : F) {
  //     for (auto &I : BB) {
  //       if (isa<BinaryOperator>(&I)) {
  //         BinaryOperator *BI = cast<BinaryOperator>(&I);
  //         if (BI->getOpcode() == Instruction::Add && 
  //             isa<LoadInst>(BI->getOperand(0))) {
  //           LoadInst *LI = cast<LoadInst>(BI->getOperand(0));
  //           if (isa<GetElementPtrInst>(LI->getPointerOperand())) {
  //             IRBuilder<> IRB(&I);
  //             Value *V = IRB.CreateAdd(BI->getOperand(0), IRB.getInt32(10));
  //             BI->setOperand(0, V);
  //           }
  //         } 
  //       }
  //     }
  //   }
  // }
  // errs() << F.getParent()->getName() << " : " << F.getName() << "\n";
  // if (F.getName() == "__kmpc_target_deinit") {
  //   for (auto Iter = F.getParent()->global_begin(), End = F.getParent()->global_end(); Iter != End; ++Iter) {
  //     GlobalVariable &g = *Iter;
  //     // g.print(errs());
  //     // errs() << "\n";
  //   }
  // }
  return true;
}

FunctionPass *
llvm::createNVPTXBallistaInstrumentPass() {
  return new NVPTXBallistaInstrument();
} 