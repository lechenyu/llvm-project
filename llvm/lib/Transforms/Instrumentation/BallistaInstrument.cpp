#include "llvm/Transforms/Instrumentation/BallistaInstrument.h"
#include "llvm/Frontend/OpenMP/OMPIRBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"

using namespace llvm;

namespace {
StringRef findModuleSimpleName(Module &M) {
  StringRef ModuleName = M.getName();
  size_t Start = ModuleName.find_last_of('/');
  Start = (Start == StringRef::npos) ? 0 : Start + 1;
  size_t End = ModuleName.find('.', Start);
  return ModuleName.substr(Start, End - Start);
}

// void addOffloadingEntry(Module &M, Constant *Variable, StringRef Name,
//                         uint64_t Size, int32_t Flags) {
//   Type *Int8PtrTy = Type::getInt8PtrTy(M.getContext());
//   Type *Int32Ty = Type::getInt32Ty(M.getContext());
//   Type *SizeTy = M.getDataLayout().getIntPtrType(M.getContext());

//   Constant *AddrName = ConstantDataArray::getString(M.getContext(), Name);

//   // Create the constant string used to look up the symbol in the device.
//   GlobalVariable *Str =
//       new GlobalVariable(M, AddrName->getType(), true,
//                          GlobalValue::InternalLinkage, AddrName,
//                          ".omp_offloading.entry_name");
//   Str->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

//   // Construct the offloading entry.
//   Constant *EntryData[] = {
//       ConstantExpr::getPointerBitCastOrAddrSpaceCast(Variable, Int8PtrTy),
//       ConstantExpr::getPointerBitCastOrAddrSpaceCast(Str, Int8PtrTy),
//       ConstantInt::get(SizeTy, Size),
//       ConstantInt::get(Int32Ty, Flags),
//       ConstantInt::get(Int32Ty, 0),
//   };
  
//   StructType *OffloadEntry = StructType::create({Int8PtrTy, Int8PtrTy, SizeTy, Int32Ty, Int32Ty}, "__tgt_offload_entry");
//   Constant *EntryInitializer =
//       ConstantStruct::get(OffloadEntry, EntryData);

//   GlobalVariable *Entry = new GlobalVariable(
//       M, OffloadEntry,
//       true, GlobalValue::WeakAnyLinkage, EntryInitializer,
//       ".omp_offloading.entry." + Name, nullptr, GlobalValue::NotThreadLocal,
//       M.getDataLayout().getDefaultGlobalsAddressSpace());

//   // The entry has to be created in the section the linker expects it to be.
//   Entry->setSection("omp_offloading_entries");
//   Entry->setAlignment(Align(1));
// }

} // namespace

PreservedAnalyses BallistaInstrumentPass::run(Function &F,
                                              FunctionAnalysisManager &FAM) {
  // errs() << "run ballista " << F.getName() << "\n";
  if (F.getName().startswith("__omp_offloading_")) {
    for (auto &BB : F) {
      for (auto &I : BB) {
        
      }
    }
  }
  return PreservedAnalyses::none();
}

PreservedAnalyses ModuleBallistaInstrumentPass::run(Module &M,
                                                    ModuleAnalysisManager &AM) {
  // errs() << "run ballista on module " << M.getName()  << "\n";

  StringRef ModuleSimpleName = findModuleSimpleName(M);
  std::string Var1 = "app_start_" + ModuleSimpleName.str();
  std::string Var2 = "shadow_start_" + ModuleSimpleName.str();
  StringRef AppStartName(Var1);
  StringRef ShaStartName(Var2);

  Type *Int64Ty = Type::getInt64Ty(M.getContext());

  if (ModuleDevice == OMP_TARGET) {
    GlobalVariable *AppStart =
        new GlobalVariable(M, Int64Ty, false, GlobalVariable::ExternalLinkage,
                           Constant::getNullValue(Int64Ty), AppStartName);
    AppStart->setAlignment(Align(8));
    AppStart->setVisibility(GlobalValue::ProtectedVisibility);
    GlobalVariable *ShaStart =
        new GlobalVariable(M, Int64Ty, false, GlobalVariable::ExternalLinkage,
                           Constant::getNullValue(Int64Ty), ShaStartName);
    ShaStart->setAlignment(Align(8));
    ShaStart->setVisibility(GlobalValue::ProtectedVisibility);
    return PreservedAnalyses::none();
  } else if (ModuleDevice == OMP_HOST) {
    GlobalVariable *AppStart =
        new GlobalVariable(M, Int64Ty, false, GlobalVariable::ExternalLinkage,
                           Constant::getNullValue(Int64Ty), AppStartName);
    AppStart->setDSOLocal(true);
    AppStart->setAlignment(Align(8));
    GlobalVariable *ShaStart =
        new GlobalVariable(M, Int64Ty, false, GlobalVariable::ExternalLinkage,
                           Constant::getNullValue(Int64Ty), ShaStartName);
    ShaStart->setDSOLocal(true);
    ShaStart->setAlignment(Align(8));

   
    // TODO (Ballista): The pointer size may not be 8.
    // See clang/lib/CodeGen/CGOpenMPRuntime.cpp:10816
    OpenMPIRBuilder OMPBuilder(M);
    OMPBuilder.initialize();
    OMPBuilder.emitOffloadingEntry(AppStart, AppStartName, 8, 0);
    OMPBuilder.emitOffloadingEntry(ShaStart, ShaStartName, 8, 0);
    // addOffloadingEntry(M, AppStart, AppStartName, 8, 0);
    // addOffloadingEntry(M, ShaStart, ShaStartName, 8, 0);
    return PreservedAnalyses::none();
  }
  return PreservedAnalyses::all();
}
