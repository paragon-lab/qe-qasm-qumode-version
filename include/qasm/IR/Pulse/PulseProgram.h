#ifndef __QASM_PULSE_PROGRAM_H
#define __QASM_PULSE_PROGRAM_H

#include <qasm/IR/Pulse/PulseInstruction.h>

#include <vector>

namespace QASM {

class PulseProgram {
private:
  std::vector<PulseInstruction> Instructions;

public:
  void Add(const PulseInstruction &I) {
    Instructions.push_back(I);
  }

  const std::vector<PulseInstruction> &
  GetInstructions() const {
    return Instructions;
  }

  void print() const;
};

} // namespace QASM

#endif