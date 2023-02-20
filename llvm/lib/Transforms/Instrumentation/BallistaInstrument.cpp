#include "llvm/Transforms/Instrumentation/BallistaInstrument.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/Transforms/Instrumentation.h"

using namespace llvm;

namespace {
/// Kind of a given entry. 
/// Copyed from <LLVM_ROOT>/clang/lib/CodeGen/CGOpenMPRuntime.h
enum OffloadingEntryInfoKinds : unsigned {
  /// Entry is a target region.
  OffloadingEntryInfoTargetRegion = 0,
  /// Entry is a declare target variable.
  OffloadingEntryInfoDeviceGlobalVar = 1,
  /// Invalid entry info.
  OffloadingEntryInfoInvalid = ~0u
};
}

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

GlobalVariable *
BallistaHostInstrumentPass::addGlobPtr(Module &M, OpenMPIRBuilder &OMPBuilder,
                                       Type *VarType, StringRef &VarName,
                                       uint64_t VarSize) {
  GlobalVariable *ExistingGlob = M.getGlobalVariable(VarName);
  if (ExistingGlob && ExistingGlob->getValueType() == VarType) {
    llvm::errs() << "Warning: " << VarName << " exists in " << M.getName() << " (host), skip insertion\n";
    return ExistingGlob;
  }
  GlobalVariable *G = new GlobalVariable(
      M, VarType, false, GlobalVariable::ExternalLinkage, nullptr, VarName);
  G->setDSOLocal(true);
  G->setAlignment(Align(8));
  OMPBuilder.emitOffloadingEntry(G, VarName, VarSize, 0);
  return G;
}

bool BallistaHostInstrumentPass::skipFunction(Function *F) {
  for (auto &FP : FuncPrefixToSkip) {
    if (F->getName().str().find(FP) == 0) {
      return true;
    }
  }
  return false;
}

SmallVector<Constant *>
BallistaHostInstrumentPass::getAccessedDeviceGlobals(Function *F) {
  SmallSet<Function *, 16> VisitedFuncs;
  SmallVector<Function *> UnvisitedFuncs;
  SmallSet<Constant *, 16> AccessedGlobs;
  UnvisitedFuncs.emplace_back(F);
  Module *M = F->getParent();

  while (!UnvisitedFuncs.empty()) {
    Function *UF = UnvisitedFuncs.pop_back_val();
    if (VisitedFuncs.contains(UF)) {
      continue;
    } else {
      VisitedFuncs.insert(UF);
    }
    // llvm::errs() << "Visit " << UF->getName() << "\n";
    for (auto &BB : *UF) {
      for (auto &I : BB) {
        for (Value *Val : I.operand_values()) {
          if (isa<GlobalVariable>(Val) && DeviceGlobs.contains(cast<GlobalVariable>(Val))) {
            AccessedGlobs.insert(cast<GlobalVariable>(Val));
          } else if (isa<ConstantExpr>(Val)) {
            ConstantExpr *Expr = cast<ConstantExpr>(Val);
            for (Value *EVal : Expr->operand_values()) {
              if (isa<GlobalVariable>(EVal) && DeviceGlobs.contains(cast<GlobalVariable>(EVal))) {
                AccessedGlobs.insert(cast<GlobalVariable>(EVal));
              }
            }
          }
        }
        if (isa<CallBase>(&I)) {
          CallBase *CB = cast<CallBase>(&I);
          Function *CF = CB->getCalledFunction();
          if (CF && !skipFunction(CF) && M->getFunction(CF->getName()) && !VisitedFuncs.contains(CF)) {
            UnvisitedFuncs.emplace_back(CF);
          }
          for (auto &Arg : CB->args()) {
            Value *ArgVal = Arg.get();
            if (isa<Function>(ArgVal)) {
              Function *ArgFunc = cast<Function>(ArgVal);
              if (!skipFunction(ArgFunc) && M->getFunction(ArgFunc->getName()) && !VisitedFuncs.contains(ArgFunc)) {
                UnvisitedFuncs.emplace_back(ArgFunc);
              }
            }
          }
        }
      }
    }
  }

  SmallVector<Constant *> AccessedGlobsV(AccessedGlobs.begin(), AccessedGlobs.end());
  return AccessedGlobsV;
}

void BallistaHostInstrumentPass::instrumentKernelCall(CallBase *CB) {
  Module &M = *CB->getModule();
  StringRef ArgKernel =
      CB->getArgOperand(CB->arg_size() - 2)->getName();
  char Sep = '.';
  int FirstSep = ArgKernel.find(Sep);
  size_t KernalNameSize = ArgKernel.rfind(Sep) - 1 - FirstSep;
  StringRef KernelName =
      ArgKernel.substr(FirstSep + 1, KernalNameSize);
  //Value *KernelArg = CB->getArgOperand(CB->arg_size() - 1);
  verboseOuts() << "Instrument " << KernelName << "\n";
  Function *Kernel = M.getFunction(KernelName);
  SmallVector<Constant *> GV = getAccessedDeviceGlobals(Kernel);
  for (auto V : GV) {
    verboseOuts() << "Accessed global variable: " << V->getName() << "\n";
  }
  IRBuilder<> IRB(CB);
  if (GV.size()) {
    ArrayType *GlobArrayTy = ArrayType::get(IRB.getVoidTy()->getPointerTo(), GV.size());
    std::string ID = std::to_string(NextIDForGlob);
    Twine GlobArrayName(GlobArrayPrefix, ID.c_str());
    GlobalVariable *GlobArray = new GlobalVariable(
        M, GlobArrayTy, false, GlobalVariable::InternalLinkage,
        ConstantArray::get(GlobArrayTy, ArrayRef<Constant *>(GV)),
        GlobArrayName);
    Type *I64 = IRB.getInt64Ty();
    ArrayType *GlobSizeArrayTy = ArrayType::get(I64, GV.size());
    Twine GlobSizeArrayName(GlobSizeArrayPrefix, ID.c_str());
    int64_t *GVS = new int64_t[GV.size()];
    for (size_t I = 0; I < GV.size(); I++) {
      GVS[I] = static_cast<int64_t>(M.getDataLayout().getTypeSizeInBits(cast<GlobalVariable>(GV[I])->getValueType()).getFixedSize() / 8);
    }
    GlobalVariable *GlobSizeArray = new GlobalVariable(
        M, GlobSizeArrayTy, false, GlobalVariable::InternalLinkage,
        ConstantDataArray::get(M.getContext(),
                               ArrayRef<int64_t>(GVS, GV.size())),
        GlobSizeArrayName);
    NextIDForGlob++;
    IRB.CreateCall(GetTargetKernelInfo, {GlobArray, GlobSizeArray, ConstantInt::get(IRB.getInt32Ty(), GV.size(), true)});
    delete[] GVS;
  } else {
    Constant *NullPtr1 = ConstantPointerNull::get(IRB.getVoidTy()->getPointerTo()->getPointerTo());
    Constant *NullPtr2 = ConstantPointerNull::get(IRB.getInt64Ty()->getPointerTo());
    IRB.CreateCall(GetTargetKernelInfo, {NullPtr1, NullPtr2, ConstantInt::get(IRB.getInt32Ty(), 0, true)});
  }
}

void BallistaHostInstrumentPass::insertBallistaRoutines(Module &M) {
  SmallVector<CallBase *> KernelCalls;
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
      IRB.CreateCall(RegisterGlobPtrs, {AppMemStart, AppShdwStart, AppShdwEnd, Argc, Argv});
    }

    for (auto &BB : F) {
      for (auto &I : BB) {
        if (isa<CallBase>(&I)) {
          CallBase *CB = cast<CallBase>(&I);
          Function *CF = CB->getCalledFunction();
          if (CF && CF->getName() == "__tgt_target_kernel") {
            KernelCalls.emplace_back(CB);
          }
        }
      }
    }
  }

  for (auto Call : KernelCalls) {
    instrumentKernelCall(Call);
  }
}

void BallistaHostInstrumentPass::collectDeviceGlobalVariables(Module &M) {
  for (auto &Glob : M.globals()) {
    std::string OffloadEntry = ".omp_offloading.entry." + Glob.getName().str();
    if (M.getGlobalVariable(StringRef(OffloadEntry))) {
      DeviceGlobs.insert(&Glob);
    }
  }
  verboseOuts() << "\"declare target\" variables on the host:" << "\n";
  for (auto DG : DeviceGlobs) {
    verboseOuts() << DG->getName() << "\n";
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
  collectDeviceGlobalVariables(M);

  StringRef Var1("app_mem_start_"), Var2("app_shdw_start_"), Var3("app_shdw_end_");
  // Var3("glob_mem_start_"), Var4("glob_mem_end_"), Var5("glob_shdw_start_");
  IntegerType *I64 = Type::getInt64Ty(M.getContext());

  OpenMPIRBuilder OMPBuilder(M);
  OMPBuilder.initialize();
  AppMemStart = addGlobPtr(M, OMPBuilder, I64, Var1, I64->getBitWidth() / 8);
  AppShdwStart = addGlobPtr(M, OMPBuilder, I64, Var2, I64->getBitWidth() / 8);
  AppShdwEnd = addGlobPtr(M, OMPBuilder, I64, Var3, I64->getBitWidth() / 8);
  // GlobMemStart = addGlobPtr(M, OMPBuilder, Var3, Int64Ty, 8);
  // GlobMemEnd = addGlobPtr(M, OMPBuilder, Var4, Int64Ty, 8);
  // GlobShdwStart = addGlobPtr(M, OMPBuilder, Var5, Int64Ty, 8);
  // unsigned LongSize = M.getDataLayout().getPointerSizeInBits();
  // IntegerType IntPtrTy = Type::getIntNTy(IRB.getContext(), LongSize);

  IRBuilder<> IRB(M.getContext());
  AttributeList Attr;
  Attr = Attr.addFnAttribute(M.getContext(), Attribute::NoUnwind);
  SmallString<32> FuncName("__ballista_register_glob_ptrs");
  RegisterGlobPtrs =
      M.getOrInsertFunction(FuncName, Attr, IRB.getVoidTy(), IRB.getPtrTy(),
                            IRB.getPtrTy(), IRB.getPtrTy(), IRB.getInt32Ty(), IRB.getPtrTy());
  SmallString<32> FuncName2("__ballista_get_target_kernel_info");
  GetTargetKernelInfo =
      M.getOrInsertFunction(FuncName2, Attr, IRB.getVoidTy(), IRB.getPtrTy(),
                            IRB.getPtrTy(), IRB.getInt32Ty());
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
  GlobalVariable *ExistingGlob = M.getGlobalVariable(VarName);
  if (ExistingGlob && ExistingGlob->getValueType() == VarType) {
    llvm::errs() << "Warning: " << VarName << " exists in " << M.getName() << " (target), skip insertion\n";
    return ExistingGlob;
  }
  GlobalVariable *G = new GlobalVariable(
      M, VarType, false, GlobalVariable::ExternalLinkage, Initializer, VarName,
      nullptr, GlobalValue::NotThreadLocal, AddressSpace);
  G->setAlignment(Align(8));
  G->setVisibility(GlobalValue::ProtectedVisibility);
  return G;
}

bool BallistaTargetInstrumentPass::aliasWithMappedOrGlobalVars(
    AAResults &AA, Function &F, Instruction &I, unsigned ArgBeginIdx) {
  bool MayAlias = false;
  unsigned Idx = ArgBeginIdx;
  for (auto Iter = F.arg_begin() + ArgBeginIdx, End = F.arg_end(); Iter != End;
       Iter++) {
    Argument &Arg = *Iter;
    AliasResult AR = AA.alias(MemoryLocation::get(&I),
                              MemoryLocation::getBeforeOrAfter(&Arg));
    if (AR != AliasResult::NoAlias) {
      verboseOuts() << "  " << I.getOpcodeName() << " may use argument " << Idx << "\n";
      MayAlias = true;
      break;
    }
    Idx++;
  }
  if (!MayAlias) {
    for (GlobalVariable *Glob : DeviceGlobs) {
      Value *Operand = isa<LoadInst>(&I) ? cast<LoadInst>(&I)->getPointerOperand() : cast<StoreInst>(&I)->getPointerOperand();
      AliasResult AR = AA.alias(Operand, Glob);
      if (AR != AliasResult::NoAlias) {
        verboseOuts() << "  " << I.getOpcodeName() << " may use global variable " << Glob->getName() << "\n"; 
        MayAlias = true;
        break;
      }
    }
  }
  return MayAlias;
}

void BallistaTargetInstrumentPass::instrumentLoadOrStore(
    Instruction &I, BallistaShadow &AccessId, size_t ShadowCell) {
  verboseOuts() << "  Instrument ";
  I.print(verboseOuts());
  verboseOuts() << "\n";

  Module *M = I.getModule();
  size_t ShadowCellInBits = ShadowCell * 8;
  IRBuilder<> IRB(&I);
  const bool IsWrite = isa<StoreInst>(&I);
  Value *Addr;
  size_t AccessedBits;
  if (IsWrite) {
    StoreInst *SI = cast<StoreInst>(&I);
    Addr = SI->getPointerOperand();
    AccessedBits = M->getDataLayout().getTypeSizeInBits(SI->getValueOperand()->getType()).getFixedSize();
  } else {
    LoadInst *LI = cast<LoadInst>(&I);
    Addr = LI->getPointerOperand();
    AccessedBits = M->getDataLayout().getTypeSizeInBits(LI->getType()).getFixedSize();
  }
  size_t AccessedShadow = (AccessedBits + ShadowCellInBits - 1)/ ShadowCellInBits;

  // locate shadow cell
  IntegerType *I64 = Type::getInt64Ty(IRB.getContext());
  IntegerType *Shadow = Type::getIntNTy(IRB.getContext(), kShadowSize);
  PointerType *ShadowPtr = Shadow->getPointerTo();

  if (AccessId >= 1 << 12) {
    llvm::errs() << "Warning: Reset AccessId to 1, there are more than "
                 << (1 << 12) << "load/store instructions in Function "
                 << I.getParent()->getName() << "\n";
    AccessId = 1;
  }
  Value *Dbg = ConstantInt::get(Shadow, AccessId << kDbgShift);
  AccessId += 1;
  LoadInst *ShdwStart =
      IRB.CreateLoad(I64, AppShdwStart, "load shadow mem start");
  LoadInst *ShdwEnd = IRB.CreateLoad(I64, AppShdwEnd, "load shadow mem end");
  LoadInst *MemStart = IRB.CreateLoad(I64, AppMemStart, "load app mem start");
  Value *AddrVal = IRB.CreatePtrToInt(Addr, I64);
  Value *Delta = IRB.CreateSub(AddrVal, MemStart);
  Value *Offset = IRB.CreateUDiv(Delta, ConstantInt::get(I64, ShadowCell));
  Value *ShdwStartPtr = IRB.CreateIntToPtr(ShdwStart, ShadowPtr);

  for (size_t I = 0; I < AccessedShadow; I++) {
    Value *CellPtr = IRB.CreateGEP(Shadow, ShdwStartPtr, Offset, "", false);
    Value *CellPtrVal = IRB.CreatePtrToInt(CellPtr, I64);
    Value *Cond1 = IRB.CreateICmpUGE(CellPtrVal, ShdwStart);
    Value *Cond2 = IRB.CreateICmpULT(CellPtrVal, ShdwEnd);
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
      Value *SRRbw = IRB.CreateOr(SRbw, kReadMask);
      NewState = IRB.CreateOr(State, SRRbw);
    }
    Value *NewCellVal = IRB.CreateOr(Dbg, NewState);
    IRB.CreateStore(NewCellVal, ValidCellPtr);
    if (I < AccessedShadow - 1) {
      Offset = IRB.CreateAdd(Offset, ConstantInt::get(I64, 1));
    }
  }
}

void BallistaTargetInstrumentPass::collectDeviceGlobalVariables(Module &M) {
  NamedMDNode *OffloadInfo = M.getNamedMetadata("omp_offload.info");
  if (!OffloadInfo) {
    llvm::errs() << "Warning: No \"omp_offload.info\" metadata node" << "\n";
    return;
  }
  for (MDNode *MN: OffloadInfo->operands()) {
    auto &&GetMDInt = [MN](unsigned Idx) {
      auto *V = cast<llvm::ConstantAsMetadata>(MN->getOperand(Idx));
      return cast<llvm::ConstantInt>(V->getValue())->getZExtValue();
    };

    auto &&GetMDString = [MN](unsigned Idx) {
      auto *V = cast<llvm::MDString>(MN->getOperand(Idx));
      return V->getString();
    };
    if (GetMDInt(0) == OffloadingEntryInfoDeviceGlobalVar) {
      StringRef VarName = GetMDString(1);
      DeviceGlobs.insert(M.getGlobalVariable(VarName));
    }
  }
  verboseOuts() << "\"declare target\" variables on the target:" << "\n";
  for (auto DG : DeviceGlobs) {
    verboseOuts() << DG->getName() << "\n";
  }
}

PreservedAnalyses BallistaTargetInstrumentPass::run(Module &M,
                                                    ModuleAnalysisManager &AM) {
  // std::error_code err;
  // raw_fd_ostream fs(M.getName().str() + "-target.before", err);
  // M.print(fs, nullptr);
  collectDeviceGlobalVariables(M);
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
  StringRef Var1("app_mem_start_"), Var2("app_shdw_start_"), Var3("app_shdw_end_");
  // Var3("glob_mem_start_"), Var4("glob_mem_end_"), Var5("glob_shdw_start_");
  StringRef VarDummy("dummy_shadow");
  Type *I64 = Type::getInt64Ty(M.getContext());
  Type *Shadow = Type::getIntNTy(M.getContext(), kShadowSize);

  AppMemStart = addGlobPtr(M, I64, nullptr, Var1);
  AppShdwStart = addGlobPtr(M, I64, nullptr, Var2);
  AppShdwEnd = addGlobPtr(M, I64, nullptr, Var3);
  DummyShadow = addGlobPtr(M, Shadow, ConstantInt::get(Shadow, 0), VarDummy);
  // GlobMemStart = addGlobPtr(M, I64, nullptr, Var3);
  // GlobMemEnd = addGlobPtr(M, I64, nullptr, Var4);
  // GlobShdwStart = addGlobPtr(M, I64, nullptr, Var5);

  // Instrument load/store
  for (auto &F : SelectedFunc) {
    BallistaShadow AccessId = 1;
    if (F->getName().startswith("__omp_offloading")) {
      verboseOuts() << "Instrument " << F->getName() << "\n";
      AAResults &AA = FAM.getResult<AAManager>(*F);
      for (auto &BB : *F) {
        for (auto &I : BB) {
          if ((isa<LoadInst>(&I) || isa<StoreInst>(&I)) &&
              aliasWithMappedOrGlobalVars(AA, *F, I)) {
            instrumentLoadOrStore(I, AccessId);
          }
        }
      }
    } else if (F->getName().startswith("__omp_outlined__")) {
      verboseOuts() << "Instrument " << F->getName() << "\n";
      AAResults &AA = FAM.getResult<AAManager>(*F);
      MDNode *MD = F->getMetadata("omp_outlined_type");
      if (!MD) {
        report_fatal_error(
            Twine("Unsupported outlined function (no MDNode \"omp_outlined_type\"):", F->getName()));
      }
      StringRef OutlinedType =
          dyn_cast<MDString>(MD->getOperand(0))->getString();
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
        for (auto &I : BB) {
          if ((isa<LoadInst>(&I) || isa<StoreInst>(&I)) &&
              aliasWithMappedOrGlobalVars(AA, *F, I, ArgBeginIdx)) {
            instrumentLoadOrStore(I, AccessId);
          }
        }
      }
    } else {
      verboseOuts() << "Instrument User-defined function " << F->getName()
                    << "\n";
      report_fatal_error(Twine("need to implement instrumentation for user-defined function ", F->getName()));
    }
  }

  // std::error_code err;
  // raw_fd_ostream fs(M.getName().str() + "-target.after", err);
  // M.print(fs, nullptr);
  return PreservedAnalyses::none();
}