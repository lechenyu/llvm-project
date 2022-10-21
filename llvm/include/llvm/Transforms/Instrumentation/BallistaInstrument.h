#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_BALLISTAINSTRUMENT_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_BALLISTAINSTRUMENT_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Frontend/OpenMP/OMPIRBuilder.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/PassManager.h"

typedef uint16_t BallistaShadow;

namespace llvm {

const size_t kShadowCell = 8;  // ratio of app mem to shadow mem
const size_t kShadowSize = 16; // size in bits
const BallistaShadow kReadBeforeWriteMask = 0x0001;
const BallistaShadow kWriteMask = 0x0002;
const BallistaShadow kReadMask = 0x0004;
const BallistaShadow kStateMask = 0x000f;
const BallistaShadow kDbgMask = 0xfff0;
const int kDbgShift = 4;


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
  GlobalVariable *AppMemStart;
  GlobalVariable *AppShdwStart;
  // GlobalVariable *GlobMemStart;
  // GlobalVariable *GlobMemEnd;
  // GlobalVariable *GlobShdwStart;
  FunctionCallee RegisterGlobPtrs;

private:
  raw_ostream &verboseOuts() { return Verbose ? llvm::errs() : llvm::nulls(); }
  bool shouldInstrument(Module &M);
  GlobalVariable *addGlobPtr(Module &M, OpenMPIRBuilder &OMPBuilder,
                                  StringRef &VarName, Type *VarType,
                                  uint64_t VarSize);
  void insertBallistaRoutines(Module &M);

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
  GlobalVariable *AppMemStart;
  GlobalVariable *AppShdwStart;
  // GlobalVariable *GlobMemStart;
  // GlobalVariable *GlobMemEnd;
  // GlobalVariable *GlobShdwStart;


private:
  raw_ostream &verboseOuts() { return Verbose ? llvm::errs() : llvm::nulls(); }
  SmallVector<Function *> getFunctionsToInstrument(Module &M);
  GlobalVariable *addGlobPtr(Module &M, StringRef &VarName, Type *VarType);
  bool aliasWithMappedVariables(AAResults &AA, Function &F, Instruction &I);
  void instrumentLoadOrStore(Instruction &I, BallistaShadow &AccessId);

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