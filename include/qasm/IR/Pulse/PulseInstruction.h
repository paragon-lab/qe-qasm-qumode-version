#ifndef __QASM_PULSE_INSTRUCTION_H
#define __QASM_PULSE_INSTRUCTION_H

#include <iostream>
#include <string>

namespace QASM {

enum class PulseOpType {
  Play,
  Wait,
  FrameChange
};

class PulseInstruction {
private:
  PulseOpType OpType;
  std::string Target;
  std::string Waveform;
  double Amplitude;
  double Phase;
  double Duration;

public:
  PulseInstruction(PulseOpType T, const std::string &Target,
                   const std::string &Waveform = "",
                   double Amplitude = 0.0,
                   double Phase = 0.0,
                   double Duration = 0.0)
      : OpType(T), Target(Target), Waveform(Waveform),
        Amplitude(Amplitude), Phase(Phase), Duration(Duration) {}

  PulseOpType GetOpType() const { return OpType; }

  const std::string &GetTarget() const { return Target; }

  const std::string &GetWaveform() const { return Waveform; }

  double GetAmplitude() const { return Amplitude; }

  double GetPhase() const { return Phase; }

  double GetDuration() const { return Duration; }

  void print() const;
};

} // namespace QASM

#endif