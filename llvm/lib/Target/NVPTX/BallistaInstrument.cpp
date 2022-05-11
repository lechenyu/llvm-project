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
void initializeBallistaInstrumentPass(PassRegistry &);
}

namespace {
class BallistaInstrument : public FunctionPass {
  bool runOnFunction(Function &F) override;

public:
  static char ID; // Pass identification, replacement for typeid
  BallistaInstrument() : FunctionPass(ID) {}
  StringRef getPassName() const override {
    return "Record memory access to mapped variables";
  }
};
} // namespace

char BallistaInstrument::ID = 1;

INITIALIZE_PASS(BallistaInstrument, "ballista-instrument",
                "Record memory access (Ballista)", false, false)

bool BallistaInstrument::runOnFunction(Function &F) {
  if (F.getName() == "__omp_outlined__1") {
    for (auto &BB : F) {
      for (auto &I : BB) {
        if (isa<BinaryOperator>(&I)) {
          BinaryOperator *BI = cast<BinaryOperator>(&I);
          if (BI->getOpcode() == Instruction::Add && 
              isa<LoadInst>(BI->getOperand(0))) {
            LoadInst *LI = cast<LoadInst>(BI->getOperand(0));
            if (isa<GetElementPtrInst>(LI->getPointerOperand())) {
              IRBuilder<> IRB(&I);
              Value *V = IRB.CreateAdd(BI->getOperand(0), IRB.getInt32(10));
              BI->setOperand(0, V);
            }
          } 
        }
      }
    }
  }
  return true;
}

FunctionPass *
llvm::createBallistaInstrumentPass() {
  return new BallistaInstrument();
}