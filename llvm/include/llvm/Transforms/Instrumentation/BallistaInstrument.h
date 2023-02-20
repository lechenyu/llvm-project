#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_BALLISTAINSTRUMENT_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_BALLISTAINSTRUMENT_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Frontend/OpenMP/OMPIRBuilder.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/PassManager.h"

typedef uint16_t BallistaShadow;

namespace llvm {

// const size_t kShadowCell = 8;  // ratio of app mem to shadow mem
const size_t kShadowSize = 16; // size in bits

// shadow word bitmap:
// [DEBUG_INFO_ID:12][UNUSED_BIT:1][READ:1][WRITE:1][READ_BEFORE_WRITE:1]
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
  SmallVector<std::string> FuncPrefixToSkip;
  GlobalVariable *AppMemStart;
  GlobalVariable *AppShdwStart;
  GlobalVariable *AppShdwEnd;
  // GlobalVariable *GlobMemStart;
  // GlobalVariable *GlobMemEnd;
  // GlobalVariable *GlobShdwStart;
  FunctionCallee RegisterGlobPtrs;
  FunctionCallee GetTargetKernelInfo;
  SmallSet<GlobalVariable *, 16> DeviceGlobs;
  int NextIDForGlob;
  StringRef GlobArrayPrefix;
  StringRef GlobSizeArrayPrefix;

private:
  raw_ostream &verboseOuts() { return Verbose ? llvm::errs() : llvm::nulls(); }
  bool shouldInstrument(Module &M);
  GlobalVariable *addGlobPtr(Module &M, OpenMPIRBuilder &OMPBuilder,
                                  Type *VarType, StringRef &VarName,
                                  uint64_t VarSize);
  void insertBallistaRoutines(Module &M);
  void collectDeviceGlobalVariables(Module &M);
  SmallVector<Constant *> getAccessedDeviceGlobals(Function *F);
  bool skipFunction(Function *F);
  void instrumentKernelCall(CallBase *CB);

public:
  BallistaHostInstrumentPass(bool IsVerbose)
      : Verbose(IsVerbose), NextIDForGlob(1),
        GlobArrayPrefix(".accessed_global."),
        GlobSizeArrayPrefix(".accessed_global_size.") {
    FuncPrefixToSkip = {"llvm", "__kmpc_"};
  };
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
  GlobalVariable *AppShdwEnd;
  GlobalVariable *DummyShadow; // dummy shadow for stack memory access
  SmallSet<GlobalVariable *, 16> DeviceGlobs;
  // GlobalVariable *GlobMemStart;
  // GlobalVariable *GlobMemEnd;
  // GlobalVariable *GlobShdwStart;


private:
  raw_ostream &verboseOuts() { return Verbose ? llvm::errs() : llvm::nulls(); }
  SmallVector<Function *> getFunctionsToInstrument(Module &M);
  GlobalVariable *addGlobPtr(Module &M, Type *VarType, Constant *Initializer, StringRef &VarName, Optional<unsigned> AddressSpace = None);
  bool aliasWithMappedOrGlobalVars(AAResults &AA, Function &F, Instruction &I, unsigned ArgBeginIdx = 0);
  void instrumentLoadOrStore(Instruction &I, BallistaShadow &AccessId, size_t ShadowCell = 4);
  void collectDeviceGlobalVariables(Module &M);

public:
  BallistaTargetInstrumentPass(bool IsVerbose) : Verbose(IsVerbose) {
    FuncPrefixToSkip = {
        "__assert_", "__llvm", "llvm", "__kmpc_", "omp_", "_omp_reduction",
        "vprintf",   "malloc", "free", "invokeMicrotask"
    };
    NameSpaceToSkip = "_OMP";
  }
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_BALLISTAINSTRUMENT_H