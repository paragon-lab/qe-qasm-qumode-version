#ifndef __QASM_AST_TO_PULSE_H
#define __QASM_AST_TO_PULSE_H

#include <qasm/AST/ASTStatement.h>
#include <qasm/IR/Pulse/PulseProgram.h>

namespace QASM {

class ASTToPulse {
public:
  PulseProgram Lower(const ASTStatementList *SL);

private:
  void LowerStatement(const ASTStatement *S, PulseProgram &P);
};

} // namespace QASM

#endif