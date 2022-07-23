#include "NVPTX.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Pass.h"

using namespace llvm;

namespace llvm {
  void initializeNVPTXBallistaAddGlobalPass(PassRegistry &);
}

namespace {

class NVPTXBallistaAddGlobal : public ModulePass {
public:
  static char ID;

  NVPTXBallistaAddGlobal() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;
};
} // end namespace

char NVPTXBallistaAddGlobal::ID = 0;

ModulePass *llvm::createNVPTXBallistaAddGlobalPass() { return new NVPTXBallistaAddGlobal(); }

INITIALIZE_PASS(
    NVPTXBallistaAddGlobal, "nvptx-ballista-add-global",
    "Add global device variables required by Ballista runtime", false,
    false)

bool NVPTXBallistaAddGlobal::runOnModule(Module &M) {
  return true;
}