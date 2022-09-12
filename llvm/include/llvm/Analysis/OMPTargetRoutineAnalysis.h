#ifndef LLVM_ANALYSIS_OMPTARGETROUTINEANALYSIS_H
#define LLVM_ANALYSIS_OMPTARGETROUTINEANALYSIS_H

#include "llvm/ADT/StringSet.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace llvm {

class OMPTargetRoutineAnalysis : public AnalysisInfoMixin<OMPTargetRoutineAnalysis> {
public:
  typedef StringSet<> Result;

  OMPTargetRoutineAnalysis() = default;

  StringSet<> run(const Module &M, ModuleAnalysisManager &);

private:
  friend AnalysisInfoMixin<OMPTargetRoutineAnalysis>;
  static AnalysisKey Key;
};

} // end namaspace llvm

#endif // LLVM_ANALYSIS_OMPTARGETROUTINEANALYSIS_H