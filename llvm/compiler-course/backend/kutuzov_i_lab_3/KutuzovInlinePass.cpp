#include "X86.h"
#include "X86InstrInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"

#include <map>

using namespace llvm;

namespace {

class KutuzovInlinePass : public MachineFunctionPass {
  static const int MAX_INSTRUCTIONS = 15;
  static const int MAX_RECURSION_DEPTH = 3;

  int countInstructions(const MachineFunction &MF) const {
    int numInstructions = 0;
    for (const auto &MBB : MF)
      for (const auto &MI : MBB)
        if (!MI.isDebugInstr() && !MI.isMetaInstruction())
          ++numInstructions;
    return numInstructions;
  }

  bool tryInline(MachineFunction &Caller, MachineBasicBlock &MBB,
                 MachineInstr &MI, int &recursionDepth) {
    if (MI.getOpcode() != X86::CALL64pcrel32 || MI.getNumOperands() == 0)
      return false;

    MachineOperand &op = MI.getOperand(0);
    if (!op.isGlobal())
      return false;

    const Function *CalleeF = dyn_cast<Function>(op.getGlobal());
    if (!CalleeF)
      return false;

    MachineFunction *CalleeMF = nullptr;
    Function *CallerF = &Caller.getFunction();

    if (CalleeF == CallerF) {
      if (recursionDepth >= MAX_RECURSION_DEPTH)
        return false;
      ++recursionDepth;
      CalleeMF = &Caller;
    } else {
      auto &MMI = getAnalysis<MachineModuleInfoWrapperPass>().getMMI();
      CalleeMF = MMI.getMachineFunction(*CalleeF);
      if (!CalleeMF)
        return false;
    }

    if (CalleeMF->empty())
      return false;

    if (countInstructions(*CalleeMF) > MAX_INSTRUCTIONS)
      return false;

    MachineRegisterInfo &CallerMRI = Caller.getRegInfo();
    MachineRegisterInfo &CalleeMRI = CalleeMF->getRegInfo();

    std::map<Register, Register> vregMap;
    SmallVector<MachineInstr *, MAX_INSTRUCTIONS + 1> instsToClone;

    for (MachineInstr &I : CalleeMF->front())
      if (!I.isReturn())
        instsToClone.push_back(&I);

    for (MachineInstr *Orig : instsToClone) {
      MachineInstr *Cloned = Caller.CloneMachineInstr(Orig);

      for (unsigned i = 0, e = Cloned->getNumOperands(); i != e; ++i) {
        MachineOperand &MO = Cloned->getOperand(i);
        if (MO.isReg() && MO.getReg().isVirtual()) {
          Register oldVReg = MO.getReg();
          auto it = vregMap.find(oldVReg);
          if (it == vregMap.end()) {
            const TargetRegisterClass *RC = CalleeMRI.getRegClass(oldVReg);
            Register newVReg = CallerMRI.createVirtualRegister(RC);
            it = vregMap.insert({oldVReg, newVReg}).first;
          }
          MO.setReg(it->second);
        }
      }

      MBB.insert(MachineBasicBlock::iterator(MI), Cloned);
    }

    MI.eraseFromParent();
    return true;
  }

public:
  static char ID;
  KutuzovInlinePass() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    bool changed = false;
    bool localChanged = true;

    while (localChanged) {
      localChanged = false;
      int recursionDepth = 0;

      for (auto &MBB : MF) {
        for (auto it = MBB.begin(); it != MBB.end();) {
          MachineInstr &MI = *it++;
          if (MI.getOpcode() == X86::CALL64pcrel32) {
            if (tryInline(MF, MBB, MI, recursionDepth)) {
              localChanged = true;
              changed = true;
            }
          }
        }
      }
    }
    return changed;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineModuleInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

char KutuzovInlinePass::ID = 0;

} // namespace

static RegisterPass<KutuzovInlinePass>
    X("kutuzov_inline-x86", "Kutuzov function inlining pass", false, false);