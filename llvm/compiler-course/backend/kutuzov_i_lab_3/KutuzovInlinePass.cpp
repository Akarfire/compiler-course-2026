#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include <map>
#include <set>

using namespace llvm;

namespace {

class KutuzovInlinePass : public ModulePass {
  static const int MAX_INSTRUCTIONS = 15;
  static const int MAX_RECURSION_DEPTH = 3;

public:
  static char ID;
  KutuzovInlinePass() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineModuleInfoWrapperPass>();
    ModulePass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override { return "kutuzov_inline-x86"; }
  bool runOnModule(Module &M) override;

private:
  int countInstructions(const MachineFunction &MF) const;
  bool hasCalls(const MachineFunction &MF) const;
  bool tryInline(MachineFunction &Caller, MachineBasicBlock &MBB,
                 MachineInstr &CallMI, int &Depth, MachineModuleInfo &MMI,
                 std::set<const Function *> &LocalBlacklist);
};

char KutuzovInlinePass::ID = 0;

bool KutuzovInlinePass::runOnModule(Module &M) {
  MachineModuleInfo &MMI = getAnalysis<MachineModuleInfoWrapperPass>().getMMI();
  bool Changed = false;

  for (Function &F : M) {
    if (F.isDeclaration())
      continue;

    MachineFunction *MF = MMI.getMachineFunction(F);
    if (!MF)
      continue;

    std::set<const Function *> LocalBlacklist;
    int Depth = 0;
    bool LocalChange = true;

    while (LocalChange) {
      LocalChange = false;

      for (MachineBasicBlock &MBB : *MF) {
        for (auto MI = MBB.begin(); MI != MBB.end();) {
          MachineInstr &Ins = *MI++;

          if (Ins.getOpcode() != X86::CALL64pcrel32)
            continue;

          MachineOperand &Op = Ins.getOperand(0);
          if (!Op.isGlobal())
            continue;
          const Function *CalleeF = dyn_cast<Function>(Op.getGlobal());
          if (!CalleeF)
            continue;

          if (LocalBlacklist.count(CalleeF))
            continue;

          if (tryInline(*MF, MBB, Ins, Depth, MMI, LocalBlacklist)) {
            LocalChange = true;
            Changed = true;
            break;
          }
        }
        if (LocalChange)
          break;
      }
    }
  }
  return Changed;
}

int KutuzovInlinePass::countInstructions(const MachineFunction &MF) const {
  int Count = 0;
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      if (!MI.isDebugInstr() && !MI.isMetaInstruction())
        ++Count;
    }
  }
  return Count;
}

bool KutuzovInlinePass::hasCalls(const MachineFunction &MF) const {
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      if (MI.isCall())
        return true;
    }
  }
  return false;
}

bool KutuzovInlinePass::tryInline(MachineFunction &Caller,
                                  MachineBasicBlock &MBB, MachineInstr &CallMI,
                                  int &Depth, MachineModuleInfo &MMI,
                                  std::set<const Function *> &LocalBlacklist) {
  MachineOperand &Op = CallMI.getOperand(0);
  const Function *CalleeF = cast<Function>(Op.getGlobal());
  MachineFunction *CalleeMF = nullptr;

  if (CalleeF == &Caller.getFunction()) {
    if (Depth >= MAX_RECURSION_DEPTH)
      return false;
    ++Depth;
    CalleeMF = &Caller;
  } else {
    CalleeMF = MMI.getMachineFunction(*CalleeF);
    if (!CalleeMF)
      return false;
  }

  if (countInstructions(*CalleeMF) > MAX_INSTRUCTIONS)
    return false;

  MachineRegisterInfo &CallerMRI = Caller.getRegInfo();
  MachineRegisterInfo &CalleeMRI = CalleeMF->getRegInfo();

  std::map<Register, Register> VRegMap;
  SmallVector<MachineInstr *, 16> ToClone;

  for (MachineInstr &MI : CalleeMF->front()) {
    if (MI.isReturn())
      continue;
    ToClone.push_back(&MI);
  }

  for (MachineInstr *MI : ToClone) {
    for (MachineOperand &MO : MI->operands()) {
      if (MO.isReg() && MO.getReg().isVirtual()) {
        Register Reg = MO.getReg();
        if (!VRegMap.count(Reg)) {
          const TargetRegisterClass *RC = CalleeMRI.getRegClass(Reg);
          VRegMap[Reg] = CallerMRI.createVirtualRegister(RC);
        }
      }
    }
  }

  MachineBasicBlock::iterator InsertPt = CallMI.getIterator();
  for (MachineInstr *Orig : ToClone) {
    MachineInstr *Clone = Caller.CloneMachineInstr(Orig);
    for (MachineOperand &MO : Clone->operands()) {
      if (MO.isReg() && MO.getReg().isVirtual()) {
        auto It = VRegMap.find(MO.getReg());
        if (It != VRegMap.end())
          MO.setReg(It->second);
      }
    }
    MBB.insert(InsertPt, Clone);
  }

  if (CalleeF != &Caller.getFunction() && hasCalls(*CalleeMF))
    LocalBlacklist.insert(CalleeF);

  CallMI.eraseFromParent();
  return true;
}

} // namespace

static RegisterPass<KutuzovInlinePass> X("kutuzov_inline-x86",
                                         "kutuzov_inline-x86", false, false);