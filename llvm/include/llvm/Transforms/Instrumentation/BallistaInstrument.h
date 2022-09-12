#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_BALLISTAINSTRUMENT_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_BALLISTAINSTRUMENT_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class Function;
class FunctionPass;
class Module;

enum Device {
  OMP_HOST,
  OMP_TARGET
};
// TODO (Ballista): Should add Ballista passes into PassBuilder

/// A function pass for Ballista instrumentation.
///
/// Instrument memory access in OpenMP target regions 
/// to record memory accesses on the target device
class BallistaInstrumentPass : public PassInfoMixin<BallistaInstrumentPass> {
private:
  Device ModuleDevice;
public:
  BallistaInstrumentPass(Device ModuleDevice) : ModuleDevice(ModuleDevice) {};
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  static bool isRequired() { return true; }
};

/// A module pass for Ballista instrumentation.
///
/// Add necessary global variables for dynamic analysis
class ModuleBallistaInstrumentPass
  : public PassInfoMixin<ModuleBallistaInstrumentPass> {
private:
  Device ModuleDevice;
public:
  ModuleBallistaInstrumentPass(Device ModuleDevice) : ModuleDevice(ModuleDevice) {}; 
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_BALLISTAINSTRUMENT_H