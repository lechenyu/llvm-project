#include "llvm/Analysis/OMPTargetRoutineAnalysis.h"
#include "llvm/IR/Function.h"

using namespace llvm;

StringSet<> OMPTargetRoutineAnalysis::run(const Module &M, ModuleAnalysisManager &) {
  StringSet<> DeviceFuncs;
  // for (auto &F : M) {
  //   if (F.getName().startswith()) {
      
  //   }
  // }
  return DeviceFuncs;
}

AnalysisKey OMPTargetRoutineAnalysis::Key;

