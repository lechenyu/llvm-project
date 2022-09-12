#include "NVPTX.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/GlobalVariable.h"
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
  // StringRef AppStartName = "app_start"; 
  // Type *Int64Ty = Type::getInt64Ty(M.getContext());
  // GlobalVariable *AppStart = static_cast<GlobalVariable *>(
  //     M.getOrInsertGlobal(AppStartName, Int64Ty, [&] {
  //       return new GlobalVariable(
  //           M, Int64Ty, false, GlobalVariable::ExternalLinkage, Constant::getNullValue(Int64Ty), AppStartName, nullptr, GlobalValue::NotThreadLocal, 1, false);
  //     }));
  // AppStart->setAlignment(MaybeAlign{8});
  // AppStart->setVisibility(GlobalValue::ProtectedVisibility);
  // for (auto Iter = M.global_begin(), End = M.global_end(); Iter != End; ++Iter) {
  //   GlobalVariable &G = *Iter;
  //   G.print(errs());
  //   errs() << "    " << G.getLinkage() << "\n";
  // }
  return true;
}