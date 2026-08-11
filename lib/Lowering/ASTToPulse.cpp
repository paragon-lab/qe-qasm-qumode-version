#include <qasm/Lowering/ASTToPulse.h>

#include <qasm/AST/ASTGates.h>
#include <qasm/AST/ASTTypes.h>

#include <iostream>
namespace QASM {

PulseProgram ASTToPulse::Lower(const ASTStatementList *SL) {
  PulseProgram P;
  if (!SL)
    return P;

  for (ASTStatementList::const_iterator I = SL->begin();
       I != SL->end(); ++I) {
    LowerStatement(*I, P);
  }

  return P;
}

void ASTToPulse::LowerStatement(const ASTStatement *S,
                                PulseProgram &P) {
  if (!S)
    return;

  const ASTStatementNode *SN =
      dynamic_cast<const ASTStatementNode *>(S);

  if (!SN)
    return;

  const ASTExpressionNode *Expr = SN->GetExpression();

  if (!Expr)
    return;

  if (Expr->GetASTType() == ASTTypeDispGate) {
    const ASTDispGateNode *DG =
        dynamic_cast<const ASTDispGateNode *>(Expr);

    if (!DG)
      return;

    std::cout << "ASTToPulse found DISP!" << std::endl;
  }
}

} // namespace QASM