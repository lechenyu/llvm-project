#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_BALLISTAINSTRUMENT_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_BALLISTAINSTRUMENT_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Frontend/OpenMP/OMPIRBuilder.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

// TODO (Ballista): Should add Ballista passes into PassBuilder

/// A module pass for Ballista's instrumentation.
///
/// 1. Add necessary global variables for dynamic analysis
/// 2. Instrument memory access to mapped variables in target regions
/// 3. Insert memory transfer before and after each target region for shadow
/// memory
class BallistaHostInstrumentPass
    : public PassInfoMixin<BallistaHostInstrumentPass> {
private:
  bool Verbose;

private:
  raw_ostream &verboseOuts() { return Verbose ? llvm::errs() : llvm::nulls(); }
  bool shouldInstrument(Module &M);
  GlobalVariable *addShadowMemPtr(Module &M, OpenMPIRBuilder &OMPBuilder,
                                  StringRef &VarName, Type *VarType,
                                  uint64_t VarSize);
  void instrumentLoadOrStore(Module &M);

public:
  BallistaHostInstrumentPass(bool IsVerbose) : Verbose(IsVerbose){};
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

class BallistaTargetInstrumentPass
    : public PassInfoMixin<BallistaTargetInstrumentPass> {
private:
  bool Verbose;
  SmallVector<std::string> FuncPrefixToSkip;
  std::string NameSpaceToSkip;

private:
  raw_ostream &verboseOuts() { return Verbose ? llvm::errs() : llvm::nulls(); }
  SmallVector<Function *> getFunctionsToInstrument(Module &M);
  GlobalVariable *addShadowMemPtr(Module &M, StringRef &VarName, Type *VarType);
  void instrumentLoadOrStore(SmallVector<Function *> &FuncList);

public:
  BallistaTargetInstrumentPass(bool IsVerbose) : Verbose(IsVerbose) {
    FuncPrefixToSkip = {"__assert_", "__llvm",  "llvm",   "__kmpc_",
                        "omp_",      "vprintf", "malloc", "free"};
    NameSpaceToSkip = "_OMP";
  }
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_BALLISTAINSTRUMENT_H