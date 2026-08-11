#include <qasm/IR/Pulse/PulseProgram.h>

#include <iostream>

namespace QASM {

void PulseProgram::print() const {
  std::cout << "<PulseProgram>" << std::endl;

  for (const auto &I : Instructions)
    I.print();

  std::cout << "</PulseProgram>" << std::endl;
}

} // namespace QASM