//===-- HelloWorld.cpp - Example Transformations --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/HelloWorld.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"

using namespace llvm;

PreservedAnalyses HelloWorldPass::run(Function &F,
                                      FunctionAnalysisManager &AM) {
  errs() << "Function name: " << F.getName() << "\n";


  for (BasicBlock &BB : F){
    // Print out the name of the basic block if it has one, and then the
    // number of instructions that it contains
    errs() << "Basic block (name=" << BB.getName() << ") has "
              << BB.size() << " instructions.\n";

    bool insert = false;
    for (Instruction &I : BB){
      errs() << I << "\n";

      if(insert){
        Module* module = F.getParent();
        LLVMContext & context = module->getContext();
        
        IRBuilder<> builder(&I);
      
        Type *intType = Type::getInt32Ty(context);

        std::vector<Type *> printfArgsTypes({Type::getInt8PtrTy(context)});
        FunctionType *printfType = FunctionType::get(intType, printfArgsTypes, true);
        FunctionCallee printfFunc = module->getOrInsertFunction("printf", printfType);

        Value *str = builder.CreateGlobalStringPtr("test print after store with magic number %d \n", "strName");
        
        llvm::Constant *magic_number = llvm::ConstantInt::get(intType, 88, false);

        builder.CreateCall(printfFunc, {str, magic_number}, "calltmp");
        insert = false;
      }
      else{
        const bool IsWrite = isa<StoreInst>(I);
        if(IsWrite){
          errs() << "   this is a store instruction \n";
          insert = true;
        }
      }

    }
  }

  // F is a pointer to a Function instance
  // errs() << "instruction printed using inst_iterator on Functions \n";
  // for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I)
  //   errs() << *I << "\n";


  return PreservedAnalyses::all();
}
