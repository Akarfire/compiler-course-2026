#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include <unordered_map>

namespace {
struct KutuzovLoadStorePass : llvm::PassInfoMixin<KutuzovLoadStorePass> {
  llvm::PreservedAnalyses run(llvm::Function &func,
                              llvm::FunctionAnalysisManager &) {
    bool changed = false;

    for (auto &BB : func) {
      // Memory model
      std::unordered_map<llvm::Value *, llvm::Value *> memory;
      std::unordered_map<llvm::Value *, llvm::StoreInst *> storeCache;

      for (auto it = BB.begin(); it != BB.end();) {
        llvm::Instruction *I = &*it++;

        // Load-s
        if (auto *load = llvm::dyn_cast<llvm::LoadInst>(I)) {
          if (load->isVolatile() || load->isAtomic())
            continue;

          auto *ptr = load->getPointerOperand();
          ptr = ptr->stripPointerCasts();

          auto valIt = memory.find(ptr);
          if (valIt != memory.end()) {
            load->replaceAllUsesWith(valIt->second);
            load->eraseFromParent();
            changed = true;
          } else {
            memory[ptr] = load;
          }
          continue;
        }

        // Store-s
        if (auto *store = llvm::dyn_cast<llvm::StoreInst>(I)) {
          if (store->isVolatile() || store->isAtomic())
            continue;

          auto *ptr = store->getPointerOperand();
          ptr = ptr->stripPointerCasts();
          llvm::Value *val = store->getValueOperand();

          if (memory.count(ptr) && memory[ptr] == val) {
            store->eraseFromParent();
            changed = true;
            continue;
          }

          if (storeCache.count(ptr)) {
            storeCache[ptr]->eraseFromParent();
            changed = true;
          }

          storeCache[ptr] = store;
          memory[ptr] = val;
          continue;
        }

        // Clearing model cache
        if (I->mayWriteToMemory()) {
          memory.clear();
          storeCache.clear();
        }
      }
    }
    return changed ? llvm::PreservedAnalyses::none()
                   : llvm::PreservedAnalyses::all();
  }

  static bool isRequired() { return true; }
};
} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "KutuzovLoadStorePass", "0.1",
          [](llvm::PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](llvm::StringRef name, llvm::FunctionPassManager &FPM,
                   llvm::ArrayRef<llvm::PassBuilder::PipelineElement>) -> bool {
                  if (name == "kutuzov_load_store_pass") {
                    FPM.addPass(KutuzovLoadStorePass{});
                    return true;
                  }
                  return false;
                });
          }};
}