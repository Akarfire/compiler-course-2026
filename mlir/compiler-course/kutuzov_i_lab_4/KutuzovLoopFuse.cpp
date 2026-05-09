#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Tools/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace {
class KutuzovLoopFuse
    : public PassWrapper<KutuzovLoopFuse, OperationPass<ModuleOp>> {

  bool checkBounds(scf::ForOp firstLoop, scf::ForOp secondLoop) {
    if (firstLoop.getLowerBound() != secondLoop.getLowerBound())
      return false;
    if (firstLoop.getUpperBound() != secondLoop.getUpperBound())
      return false;
    if (firstLoop.getStep() != secondLoop.getStep())
      return false;

    return true;
  }

  bool checkDependencies(scf::ForOp firstLoop, scf::ForOp secondLoop) {
    // Checking inter-loop data dependencies
    bool hasDependencies = false;

    // Collecting results of the first loop's operation
    llvm::DenseSet<Value> firstLoopValues;

    firstLoop.walk([&](Operation *firstLoopOp) {
      if (firstLoopOp == firstLoop.getOperation())
        return WalkResult::advance();

      for (OpResult res : firstLoopOp->getResults())
        firstLoopValues.insert(res);

      return WalkResult::advance();
    });

    for (Value result : firstLoop.getResults())
      firstLoopValues.insert(result);

    // Checking every operand in the second loop to find overlaps
    secondLoop.walk([&](Operation *secondLoopOp) {
      if (secondLoopOp == secondLoop.getOperation())
        return WalkResult::advance();

      for (Value operand : secondLoopOp->getOperands())
        if (firstLoopValues.contains(operand)) {
          hasDependencies = true;
          return WalkResult::interrupt();
        }

      return WalkResult::advance();
    });

    return !hasDependencies;
  }

  void fuse(scf::ForOp firstLoop, scf::ForOp secondLoop) {
    // Fusing init arguments
    SmallVector<Value> fusedInitArguments;
    fusedInitArguments.append(firstLoop.getInitArgs().begin(),
                              firstLoop.getInitArgs().end());
    fusedInitArguments.append(secondLoop.getInitArgs().begin(),
                              secondLoop.getInitArgs().end());

    // Building the fused loop
    OpBuilder builder(firstLoop);
    auto fusedLoop = builder.create<scf::ForOp>(
        firstLoop.getLoc(), firstLoop.getLowerBound(),
        firstLoop.getUpperBound(), firstLoop.getStep(), fusedInitArguments);

    // Mapping old values to the fused ones
    IRMapping cloneMapping;
    cloneMapping.map(firstLoop.getInductionVar(), fusedLoop.getInductionVar());
    cloneMapping.map(secondLoop.getInductionVar(), fusedLoop.getInductionVar());

    for (unsigned i = 0; i < firstLoop.getNumRegionIterArgs(); i++)
      cloneMapping.map(firstLoop.getRegionIterArg(i),
                       fusedLoop.getRegionIterArg(i));

    for (unsigned i = 0; i < secondLoop.getNumRegionIterArgs(); i++)
      cloneMapping.map(
          secondLoop.getRegionIterArg(i),
          fusedLoop.getRegionIterArg(firstLoop.getNumRegionIterArgs() + i));

    // Rebuilding fused loop body
    fusedLoop.getBody()->clear();
    builder.setInsertionPointToEnd(fusedLoop.getBody());

    // Cloning operations
    for (auto &op : firstLoop.getBody()->without_terminator())
      builder.clone(op, cloneMapping);

    for (auto &op : secondLoop.getBody()->without_terminator())
      builder.clone(op, cloneMapping);

    // Handling yields
    SmallVector<Value> fusedYields;

    auto firstLoopYield =
        cast<scf::YieldOp>(firstLoop.getBody()->getTerminator());
    for (auto operand : firstLoopYield.getOperands())
      fusedYields.push_back(cloneMapping.lookup(operand));

    auto secondLoopYield =
        cast<scf::YieldOp>(secondLoop.getBody()->getTerminator());
    for (auto operand : secondLoopYield.getOperands())
      fusedYields.push_back(cloneMapping.lookup(operand));

    builder.create<scf::YieldOp>(fusedLoop.getLoc(), fusedYields);

    // Handling results
    for (unsigned i = 0; i < firstLoop.getNumResults(); i++)
      firstLoop.getResult(i).replaceAllUsesWith(fusedLoop.getResult(i));

    for (unsigned i = 0; i < secondLoop.getNumResults(); i++)
      secondLoop.getResult(i).replaceAllUsesWith(
          fusedLoop.getResult(firstLoop.getNumResults() + i));

    // Erasing old loops
    firstLoop.erase();
    secondLoop.erase();
  }

public:
  StringRef getArgument() const final { return "kutuzov_loop_fuse"; }
  StringRef getDescription() const final {
    return "Fuses two adjacent scf.for loops with \
    identical iteration bounds and no inter-loop data dependencies into a single scf.for";
  }

  void runOnOperation() override {
    bool changed;
    do {
      changed = false;
      SmallVector<std::pair<Operation *, Operation *>> loopsToFuse;

      getOperation()->walk([&](scf::ForOp firstLoop) {
        // Locating 2 loops in a row
        auto secondLoop = dyn_cast<scf::ForOp>(firstLoop->getNextNode());
        if (!secondLoop)
          return;

        // Fuse conditions
        if (!checkBounds(firstLoop, secondLoop))
          return;
        if (!checkDependencies(firstLoop, secondLoop))
          return;

        loopsToFuse.push_back(
            {firstLoop.getOperation(), secondLoop.getOperation()});
      });

      for (auto loops : loopsToFuse) {
        auto firstLoop = scf::ForOp(loops.first);
        auto secondLoop = scf::ForOp(loops.second);

        // Validating loops
        if (!firstLoop.getOperation()->getBlock() ||
            !secondLoop.getOperation()->getBlock())
          continue;

        // Fusing loops
        fuse(firstLoop, secondLoop);
        changed = true;

        // One fusion per walk
        break;
      }
    } while (changed);
  }
};
} // namespace

MLIR_DECLARE_EXPLICIT_TYPE_ID(KutuzovLoopFuse)
MLIR_DEFINE_EXPLICIT_TYPE_ID(KutuzovLoopFuse)

mlir::PassPluginLibraryInfo getFunctionCallCounterPassPluginInfo() {
  return {MLIR_PLUGIN_API_VERSION, "KutuzovLoopFuse", "1.0",
          []() { mlir::PassRegistration<KutuzovLoopFuse>(); }};
}

extern "C" LLVM_ATTRIBUTE_WEAK mlir::PassPluginLibraryInfo
mlirGetPassPluginInfo() {
  return getFunctionCallCounterPassPluginInfo();
}