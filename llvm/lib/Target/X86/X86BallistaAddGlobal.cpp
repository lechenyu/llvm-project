#include "X86.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Pass.h"

using namespace llvm;

namespace llvm {
  void initializeX86BallistaAddGlobalPass(PassRegistry &);
}

namespace {

class X86BallistaAddGlobal : public ModulePass {
public:
  static char ID;

  X86BallistaAddGlobal() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;
};
} // end namespace

char X86BallistaAddGlobal::ID = 0;

ModulePass *llvm::createX86BallistaAddGlobalPass() { return new X86BallistaAddGlobal(); }

INITIALIZE_PASS(
    X86BallistaAddGlobal, "x86-ballista-add-global",
    "Add global host variables required by Ballista runtime", false,
    false)

bool X86BallistaAddGlobal::runOnModule(Module &M) {
  // for (auto Iter = M.global_begin(), End = M.global_end(); Iter != End; ++Iter) {
  //   GlobalVariable &G = *Iter;
  //   G.print(errs());
  //   errs() << '\n';
  // }
  return true;
}