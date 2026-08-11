#include <qasm/IR/Pulse/PulseInstruction.h>

#include <iostream>

namespace QASM {

void PulseInstruction::print() const {
  std::cout << "<PulseInstruction>" << std::endl;

  switch (OpType) {
  case PulseOpType::Play:
    std::cout << "<Type>Play</Type>" << std::endl;
    break;
  case PulseOpType::Wait:
    std::cout << "<Type>Wait</Type>" << std::endl;
    break;
  case PulseOpType::FrameChange:
    std::cout << "<Type>FrameChange</Type>" << std::endl;
    break;
  }

  std::cout << "<Target>" << Target << "</Target>" << std::endl;
  std::cout << "<Waveform>" << Waveform << "</Waveform>" << std::endl;
  std::cout << "<Amplitude>" << Amplitude << "</Amplitude>" << std::endl;
  std::cout << "<Phase>" << Phase << "</Phase>" << std::endl;
  std::cout << "<Duration>" << Duration << "</Duration>" << std::endl;

  std::cout << "</PulseInstruction>" << std::endl;
}

} // namespace QASM