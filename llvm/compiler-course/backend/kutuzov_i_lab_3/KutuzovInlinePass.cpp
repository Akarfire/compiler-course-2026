#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

static const char *MetaDataKey = "kutuzov.recursion_depth";

static int getRecursionDepth(CallInst *call_instruction) {
  if (MDNode *metadata = call_instruction->getMetadata(MetaDataKey)) {
    if (metadata->getNumOperands() == 1) {
      const auto *extract = mdconst::extract<ConstantInt>(metadata->getOperand(0));
      return extract->getZExtValue();
    }
  }
  return 0;
}

static void setRecursionDepth(CallInst *call_instruction, int depth) {
  LLVMContext &context = call_instruction->getContext();
  MDNode *metadata = MDNode::get(context, ConstantAsMetadata::get(
                                   ConstantInt::get(Type::getInt32Ty(context), depth)));
  call_instruction->setMetadata(MetaDataKey, metadata);
}

class KutuzovInlinePass : public PassInfoMixin<KutuzovInlinePass> {
  static const int MAX_INSTRUCTIONS = 15;
  static const int MAX_RECURSION_DEPTH = 3;

  bool canInline(Function &func) const {
    int num_instructions = 0;
    for (BasicBlock &block : func)
      for (Instruction &instruction : block) {
        if (instruction.isTerminator() || isa<DbgInfoIntrinsic>(instruction))
          continue;
        if (++num_instructions > MAX_INSTRUCTIONS)
          return false;
      }
    return true;
  }

  bool performInline(Function &caller, CallInst *call_instruction, Function &called_func) {
    BasicBlock &called_entry_block = called_func.getEntryBlock();

    if (called_func.size() != 1)
      return false;
    ReturnInst *ret = dyn_cast<ReturnInst>(called_entry_block.getTerminator());
    if (!ret)
      return false;

    ValueMap<const Value *, Value *> arg_map;
    int arg_index = 0;
    for (Argument &arg : called_func.args())
      arg_map[&arg] = call_instruction->getArgOperand(arg_index++);

    IRBuilder<> builder(call_instruction);
    BasicBlock *caller_block = call_instruction->getParent();
    for (Instruction &instruction : called_entry_block) {
      if (instruction.isTerminator())
        continue;
      Instruction *cloned_instruction = instruction.clone();
      for (int Op = 0, E = cloned_instruction->getNumOperands(); Op != E; ++Op) {
        Value *OpV = cloned_instruction->getOperand(Op);
        if (OpV && arg_map.count(OpV))
          cloned_instruction->setOperand(Op, arg_map[OpV]);
      }
      builder.Insert(cloned_instruction);
      arg_map[&instruction] = cloned_instruction;
    }

    if (Value *return_value = ret->getReturnValue()) {
      Value *mapped_return_value = arg_map.lookup(return_value);
      call_instruction->replaceAllUsesWith(mapped_return_value ? mapped_return_value : return_value);
    } else {
      call_instruction->replaceAllUsesWith(UndefValue::get(call_instruction->getType()));
    }

    int depth = getRecursionDepth(call_instruction);
    for (Instruction &I : *caller_block) {
      auto *Newcall_instruction = dyn_cast<CallInst>(&I);
      if (Newcall_instruction && Newcall_instruction->getCalledFunction() == &called_func) {
        setRecursionDepth(Newcall_instruction, depth + 1);
      }
    }

    call_instruction->eraseFromParent();
    return true;
  }

  bool runImpl(Module &module) {
    bool changed = false;

    for (Function &func : module) {
      if (func.isDeclaration())
        continue;

      bool local_changed;
      do {
        local_changed = false;
        for (BasicBlock &block : func) {
          for (auto instr = block.begin(); instr != block.end();) {
            Instruction *instruction = &*instr++;
            auto *call_instruction = dyn_cast<CallInst>(instruction);
            if (!call_instruction)
              continue;

            Function *called_func = call_instruction->getCalledFunction();
            if (!called_func || called_func->isDeclaration())
              continue;

            if (getRecursionDepth(call_instruction) >= MAX_RECURSION_DEPTH)
              continue;

            if (!canInline(*called_func))
              continue;

            if (performInline(func, call_instruction, *called_func)) {
              local_changed = true;
              changed = true;
              break;
            }
          }
          if (local_changed)
            break;
        }
      } while (local_changed);
    }
    return changed;
  }

public:
  PreservedAnalyses run(Module &module, ModuleAnalysisManager &analyis_manager) {
    if (!runImpl(module))
      return PreservedAnalyses::all();
    return PreservedAnalyses::none();
  }
};

} // namespace

PassPluginLibraryInfo getKutuzovInlinePluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "kutuzov_inline-x86", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "kutuzov_inline-x86") {
                    MPM.addPass(KutuzovInlinePass());
                    return true;
                  }
                  return false;
                });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return getKutuzovInlinePluginInfo();
}