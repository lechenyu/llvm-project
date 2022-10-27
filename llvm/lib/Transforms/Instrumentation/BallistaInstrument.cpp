#include "llvm/Transforms/Instrumentation/BallistaInstrument.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/Transforms/Instrumentation.h"

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

GlobalVariable *BallistaHostInstrumentPass::addGlobPtr(
    Module &M, OpenMPIRBuilder &OMPBuilder, Type *VarType, StringRef &VarName,
    uint64_t VarSize) {
  GlobalVariable *G = new GlobalVariable(
      M, VarType, false, GlobalVariable::ExternalLinkage, nullptr, VarName);
  G->setDSOLocal(true);
  G->setAlignment(Align(8));
  OMPBuilder.emitOffloadingEntry(G, VarName, VarSize, 0);
  return G;
}

void BallistaHostInstrumentPass::insertBallistaRoutines(Module &M) {
  for (auto &F : M) {
    if (F.getName() == "main") {
      IRBuilder<> IRB(&F.getEntryBlock().front());
      Value *Argc, *Argv;
      IntegerType *I32 = IRB.getInt32Ty();
      PointerType *PtrTy = IRB.getPtrTy();
      if (F.arg_size() == 2) {
        Argc = F.getArg(0);
        Argv = F.getArg(1);
      } else {
        Argc = ConstantInt::getSigned(I32, 0);
        Argv = ConstantPointerNull::get(PtrTy);
      }
      IRB.CreateCall(RegisterGlobPtrs, {AppMemStart, AppShdwStart, Argc, Argv});
    }
  }
}

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
                << M.getTargetTriple() << " (host device)"
                << "\n";
  StringRef Var1("app_mem_start_"), Var2("app_shdw_start_"),
      Var3("glob_mem_start_"), Var4("glob_mem_end_"), Var5("glob_shdw_start_");
  IntegerType *I64 = Type::getInt64Ty(M.getContext());

  OpenMPIRBuilder OMPBuilder(M);
  OMPBuilder.initialize();
  AppMemStart = addGlobPtr(M, OMPBuilder, I64, Var1, I64->getBitWidth() / 8);
  AppShdwStart = addGlobPtr(M, OMPBuilder, I64, Var2, I64->getBitWidth() / 8);
  // GlobMemStart = addGlobPtr(M, OMPBuilder, Var3, Int64Ty, 8);
  // GlobMemEnd = addGlobPtr(M, OMPBuilder, Var4, Int64Ty, 8);
  // GlobShdwStart = addGlobPtr(M, OMPBuilder, Var5, Int64Ty, 8);
  // unsigned LongSize = M.getDataLayout().getPointerSizeInBits();
  // IntegerType IntPtrTy = Type::getIntNTy(IRB.getContext(), LongSize);
  
  IRBuilder<> IRB(M.getContext());
  AttributeList Attr;
  Attr = Attr.addFnAttribute(M.getContext(), Attribute::NoUnwind);
  SmallString<32> FuncName("ballista_register_glob_ptrs");
  RegisterGlobPtrs = M.getOrInsertFunction(FuncName, Attr, IRB.getVoidTy(), IRB.getPtrTy(), IRB.getPtrTy(), IRB.getInt32Ty(), IRB.getPtrTy());

  insertBallistaRoutines(M);
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

GlobalVariable *BallistaTargetInstrumentPass::addGlobPtr(
    Module &M, Type *VarType, Constant *Initializer, StringRef &VarName,
    Optional<unsigned> AddressSpace) {
  GlobalVariable *G = new GlobalVariable(
      M, VarType, false, GlobalVariable::ExternalLinkage, Initializer, VarName,
      nullptr, GlobalValue::NotThreadLocal, AddressSpace);
  G->setAlignment(Align(8));
  G->setVisibility(GlobalValue::ProtectedVisibility);
  return G;
}

bool BallistaTargetInstrumentPass::aliasWithMappedVariables(
    AAResults &AA, Function &F, Instruction &I, unsigned ArgBeginIdx) {
  bool MayAlias = false;
  for (auto Iter = F.arg_begin() + ArgBeginIdx, End = F.arg_end(); Iter != End; Iter++) {
    Argument &Arg = *Iter;
    AliasResult AR = AA.alias(MemoryLocation::get(&I),
                              MemoryLocation::getBeforeOrAfter(&Arg));
    if (AR != AliasResult::NoAlias) {
      MayAlias = true;
      break;
    }
  }
  return MayAlias;
}

void BallistaTargetInstrumentPass::instrumentLoadOrStore(Instruction &I, BallistaShadow &AccessId) {
  IRBuilder<> IRB(&I);
  const bool IsWrite = isa<StoreInst>(&I);
  Value *Addr = IsWrite ? cast<StoreInst>(&I)->getPointerOperand()
                        : cast<LoadInst>(&I)->getPointerOperand();
  // locate shadow cell
  IntegerType *I64 = Type::getInt64Ty(IRB.getContext());
  IntegerType *Shadow = Type::getIntNTy(IRB.getContext(), kShadowSize);
  PointerType *ShadowPtr = Shadow->getPointerTo();

  LoadInst *ShdwStart = IRB.CreateLoad(I64, AppShdwStart, "load shadow mem start");
  LoadInst *MemStart = IRB.CreateLoad(I64, AppMemStart, "load app mem start");
  Value *AddrVal = IRB.CreatePtrToInt(Addr, I64);
  Value *Delta = IRB.CreateSub(AddrVal, MemStart);
  Value *Offset = IRB.CreateUDiv(Delta, ConstantInt::get(I64, kShadowCell));
  Value *ShdwStartPtr = IRB.CreateIntToPtr(ShdwStart, ShadowPtr);
  Value *CellPtr = IRB.CreateGEP(Shadow, ShdwStartPtr, Offset, "", true);
  Value *Cond1 = IRB.CreateICmpUGE(AddrVal, MemStart);
  Value *Cond2 = IRB.CreateICmpULT(AddrVal, ShdwStart);
  Value *Cond = IRB.CreateAnd(Cond1, Cond2);
  Value *ValidCellPtr = IRB.CreateSelect(Cond, CellPtr, DummyShadow);
  LoadInst *CellVal = IRB.CreateLoad(Shadow, ValidCellPtr);
  Value *State = IRB.CreateAnd(CellVal, kStateMask);
  Value *NewState;
  if (IsWrite) {
    NewState = IRB.CreateOr(State, kWriteMask);
  } else {
    Value *Xor = IRB.CreateXor(State, ConstantInt::getSigned(Shadow, -1));
    Value *SW = IRB.CreateAnd(Xor, kWriteMask);
    Value *SRbw = IRB.CreateAShr(SW, kWriteMask - kReadBeforeWriteMask);
    Value *SWRbw = IRB.CreateOr(SRbw, kReadMask);
    NewState = IRB.CreateOr(State, SWRbw);
  }
  if (AccessId >= 1 << 12 ) {
    llvm::errs() << "Warning: Reset AccessId to 1, there are more than " << (1 << 12) << "load/store instructions in Function " << I.getParent()->getName() << "\n"; 
    AccessId = 1;
  }
  Value *Dbg = ConstantInt::get(Shadow, AccessId << kDbgShift);
  AccessId += 1;
  Value *NewCellVal = IRB.CreateOr(Dbg, NewState);
  IRB.CreateStore(NewCellVal, ValidCellPtr);
}

PreservedAnalyses BallistaTargetInstrumentPass::run(Module &M,
                                                    ModuleAnalysisManager &AM) {
  // std::error_code err;
  // raw_fd_ostream fs(M.getName().str() + "-target.before", err);
  // M.print(fs, nullptr);
  SmallVector<Function *> SelectedFunc = getFunctionsToInstrument(M);
  if (SelectedFunc.empty()) {
    verboseOuts() << "Not instrument " << M.getModuleIdentifier()
                  << " for target " << M.getTargetTriple() << " (target device)"
                  << "\n";
    return PreservedAnalyses::all();
  }
  verboseOuts() << "Instrument " << M.getModuleIdentifier() << " for target "
                << M.getTargetTriple() << "(target device)"
                << "\n";
  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  // Insert global pointers to store the start address of application memory and
  // shadow memory
  StringRef Var1("app_mem_start_"), Var2("app_shdw_start_"),
      Var3("glob_mem_start_"), Var4("glob_mem_end_"), Var5("glob_shdw_start_");
  StringRef VarDummy("dummy_shadow");
  Type *I64 = Type::getInt64Ty(M.getContext());
  Type *Shadow = Type::getIntNTy(M.getContext(), kShadowSize);

  AppMemStart = addGlobPtr(M, I64, nullptr, Var1);
  AppShdwStart = addGlobPtr(M, I64, nullptr, Var2);
  DummyShadow = addGlobPtr(M, Shadow, ConstantInt::get(Shadow, 0), VarDummy);
  // GlobMemStart = addGlobPtr(M, I64, nullptr, Var3);
  // GlobMemEnd = addGlobPtr(M, I64, nullptr, Var4);
  // GlobShdwStart = addGlobPtr(M, I64, nullptr, Var5);

  // Instrument load/store
  for (auto &F : SelectedFunc) {
    if (F->getName().startswith("__omp_offloading")) {
      verboseOuts() << "Instrument " << F->getName() << "\n"; 
      AAResults &AA = FAM.getResult<AAManager>(*F);
      for (auto &BB : *F) {
        BallistaShadow AccessId = 1;
        for (auto &I : BB) {
          if ((isa<LoadInst>(&I) || isa<StoreInst>(&I)) &&
              aliasWithMappedVariables(AA, *F, I)) {
            instrumentLoadOrStore(I, AccessId);
          }
        }
      }
    } else if (F->getName().startswith("__omp_outlined__")) {
      verboseOuts() << "Instrument " << F->getName() << "\n";
      AAResults &AA = FAM.getResult<AAManager>(*F);
      MDNode* MD = F->getMetadata("omp_outlined_type");
      if (!MD) {
        report_fatal_error(Twine("Unsupported outlined function:", F->getName()));
      }
      StringRef OutlinedType = dyn_cast<MDString>(MD->getOperand(0))->getString();
      unsigned ArgBeginIdx = 0;
      if (OutlinedType == "parallel") {
        ArgBeginIdx = 4;
      } else if (OutlinedType == "teams") {
        ArgBeginIdx = 2;
      } else if (OutlinedType == "wrapper") {
        continue;
      } else {
        report_fatal_error(Twine("Unknown outlined type:", OutlinedType));
      }
      for (auto &BB : *F) {
        BallistaShadow AccessId = 1;
        for (auto &I : BB) {
          if ((isa<LoadInst>(&I) || isa<StoreInst>(&I)) &&
              aliasWithMappedVariables(AA, *F, I, ArgBeginIdx)) {
            instrumentLoadOrStore(I, AccessId);
          }
        }
      }
    } else {
      verboseOuts() << "Instrument User-defined function " << F->getName() << "\n";
    }
  }

  // std::error_code err;
  // raw_fd_ostream fs(M.getName().str() + "-target.after", err);
  // M.print(fs, nullptr);
  return PreservedAnalyses::none();
}