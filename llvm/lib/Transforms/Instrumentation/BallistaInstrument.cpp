#include "llvm/Transforms/Instrumentation/BallistaInstrument.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"

using namespace llvm;

static StringRef findModuleSimpleName(Module &M) {
  StringRef ModuleName = M.getName();
  size_t Start = ModuleName.find_last_of('/');
  Start = (Start == StringRef::npos) ? 0 : Start + 1;
  size_t End = ModuleName.find('.', Start);
  return ModuleName.substr(Start, End - Start);
}

bool BallistaHostInstrumentPass::shouldInstrument(Module &M) {
  for (auto &F : M) {
    if (F.getName() == ".omp_offloading.requires_reg") {
      return true;
    }
  }
  return false;
}

GlobalVariable *BallistaHostInstrumentPass::addShadowMemPtr(
    Module &M, OpenMPIRBuilder &OMPBuilder, StringRef &VarName, Type *VarType,
    uint64_t VarSize) {
  GlobalVariable *G = new GlobalVariable(
      M, VarType, false, GlobalVariable::ExternalLinkage, nullptr, VarName);
  G->setDSOLocal(true);
  G->setAlignment(Align(8));
  OMPBuilder.emitOffloadingEntry(G, VarName, VarSize, 0);
  return G;
}

void BallistaHostInstrumentPass::instrumentLoadOrStore(Module &M) {}

PreservedAnalyses BallistaHostInstrumentPass::run(Module &M,
                                                  ModuleAnalysisManager &AM) {
  // std::error_code err;
  // raw_fd_ostream fs(M.getName().str() + "-host.before", err);
  // M.print(fs, nullptr);
  if (!shouldInstrument(M)) {
    verboseOuts() << "Not instrument " << M.getModuleIdentifier()
                  << " for target " << M.getTargetTriple() << "(host device)"
                  << "\n";
    return PreservedAnalyses::all();
  }
  verboseOuts() << "Instrument " << M.getModuleIdentifier() << " for target "
                << M.getTargetTriple() << "(host device)"
                << "\n";
  std::string Var1 = "app_start_";
  std::string Var2 = "shadow_start_";
  StringRef AppStartName(Var1);
  StringRef ShaStartName(Var2);
  Type *Int64Ty = Type::getInt64Ty(M.getContext());

  OpenMPIRBuilder OMPBuilder(M);
  OMPBuilder.initialize();
  GlobalVariable *AppStart =
      addShadowMemPtr(M, OMPBuilder, AppStartName, Int64Ty, 8);
  GlobalVariable *ShaStart =
      addShadowMemPtr(M, OMPBuilder, ShaStartName, Int64Ty, 8);
  instrumentLoadOrStore(M);
  // std::error_code err;
  // raw_fd_ostream fs(M.getName().str() + "-host.after", err);
  // M.print(fs, nullptr);
  return PreservedAnalyses::none();
}

SmallVector<Function *>
BallistaTargetInstrumentPass::getFunctionsToInstrument(Module &M) {
  SmallVector<Function *> SelectedFunc;
  for (auto &F : M) {
    if (!F.empty()) {
      std::string DemangledName = llvm::demangle(F.getName().str());
      size_t NameSpaceEnd = DemangledName.find("::");
      if (NameSpaceEnd == std::string::npos) {
        NameSpaceEnd = 0;
      }
      size_t FuncNameStart = DemangledName.rfind("::");
      if (FuncNameStart == std::string::npos) {
        FuncNameStart = 0;
      } else {
        FuncNameStart += 2;
      }
      std::string NameSpace = DemangledName.substr(0, NameSpaceEnd);
      std::string FuncName = DemangledName.substr(FuncNameStart);
      if (NameSpace == NameSpaceToSkip) {
        continue;
      }
      bool Skip = false;
      for (auto &FP : FuncPrefixToSkip) {
        if (FuncName.find(FP) == 0) {
          Skip = true;
          break;
        }
      }
      if (!Skip) {
        SelectedFunc.emplace_back(&F);
      }
    }
  }
  return SelectedFunc;
}

GlobalVariable *
BallistaTargetInstrumentPass::addShadowMemPtr(Module &M, StringRef &VarName,
                                              Type *VarType) {
  GlobalVariable *G = new GlobalVariable(
      M, VarType, false, GlobalVariable::ExternalLinkage, nullptr, VarName);
  G->setAlignment(Align(8));
  G->setVisibility(GlobalValue::ProtectedVisibility);
  return G;
}

void BallistaTargetInstrumentPass::instrumentLoadOrStore(Module &M) {}

PreservedAnalyses BallistaTargetInstrumentPass::run(Module &M,
                                                    ModuleAnalysisManager &AM) {
  // std::error_code err;
  // raw_fd_ostream fs(M.getName().str() + "-target.before", err);
  // M.print(fs, nullptr);
  SmallVector<Function *> SelectedFunc = getFunctionsToInstrument(M);
  if (SelectedFunc.empty()) {
    verboseOuts() << "Not instrument " << M.getModuleIdentifier()
                  << " for target " << M.getTargetTriple() << "(target device)"
                  << "\n";
    return PreservedAnalyses::all();
  }
  verboseOuts() << "Instrument " << M.getModuleIdentifier() << " for target "
                << M.getTargetTriple() << "(target device)"
                << "\n";

  // Insert global pointers to store the start address of application memory and
  // shadow memory
  std::string Var1 = "app_start_";
  std::string Var2 = "shadow_start_";
  StringRef AppStartName(Var1);
  StringRef ShaStartName(Var2);
  Type *Int64Ty = Type::getInt64Ty(M.getContext());
  GlobalVariable *AppStart = addShadowMemPtr(M, AppStartName, Int64Ty);
  GlobalVariable *ShaStart = addShadowMemPtr(M, ShaStartName, Int64Ty);
  // std::error_code err;
  // raw_fd_ostream fs(M.getName().str() + "-target.after", err);
  // M.print(fs, nullptr);
  return PreservedAnalyses::none();
}