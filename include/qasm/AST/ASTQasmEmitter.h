/* -*- coding: utf-8 -*-
 *
 * Copyright 2026. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * =============================================================================
 */

#ifndef __QASM_AST_QASM_EMITTER_H
#define __QASM_AST_QASM_EMITTER_H

#include <qasm/AST/ASTGates.h>
#include <qasm/AST/ASTMeasure.h>
#include <qasm/AST/ASTRoot.h>
#include <qasm/AST/ASTStatement.h>
#include <qasm/AST/ASTTypes.h>

#include <ostream>
#include <string>

namespace QASM {

class ASTForStatementNode;
class ASTGateFockControlNode;
class ASTGateGPhaseExpressionNode;
class ASTGPhaseExpressionNode;
class ASTIdentifierList;

/// Incremental OpenQASM source emitter. Unknown statement kinds are ignored
/// (EmitUnhandled is a no-op) so support can be added category by category.
class ASTQasmEmitter {
private:
  std::ostream &OS;
  unsigned IndentLevel = 0;

public:
  explicit ASTQasmEmitter(std::ostream &Out) : OS(Out) {}

  virtual ~ASTQasmEmitter() = default;

  /// Emit from the global statement list attached to \p Root.
  void Emit(const ASTRoot *Root);

  /// Emit every statement in \p SL (order preserved).
  void Emit(const ASTStatementList *SL);

  /// Dispatch one statement; unknown kinds call EmitUnhandled (no-op).
  void EmitStatement(const ASTStatement *S);

protected:
  virtual void EmitOpenQASM(const ASTOpenQASMStatementNode *S);

  /// Emit a variable declaration (classical, quantum, array, stretch, …).
  virtual void EmitDeclaration(const ASTDeclarationNode *DN);

  /// Emit `lhs = rhs;` / compound assignment from a binary-op statement.
  virtual void EmitBinaryOpStatement(const ASTBinaryOpStatementNode *S);

  /// Emit `measure …;` / `bit = measure …;`.
  virtual void EmitMeasure(const ASTMeasureNode *MN);

  /// Emit `measure_photon_count(N) qm;` / `bit = measure_photon_count(N) qm;`.
  virtual void EmitPhotonCountMeasure(const ASTPhotonCountMeasureNode *MN);

  /// Emit `gate name[…](…) qargs { … }`.
  virtual void EmitGateDeclaration(const ASTGateDeclarationNode *GDN);

  /// Emit a gate-body / top-level quantum op (call, ctrl, gphase, for, …).
  virtual void EmitGateQOp(const ASTGateQOpNode *GQ);

  /// Emit a gate call: `name[…](params) qargs;`.
  virtual void EmitGateCall(const ASTGateQOpNode *GQ);

  /// Emit an expression tree (identifiers, literals, binary/unary ops, …).
  virtual void EmitExpression(const ASTExpressionNode *E);

  /// Default no-op for statement kinds not yet supported.
  virtual void EmitUnhandled(const ASTStatement * /*S*/) {}

  /// True for named variable declarations this emitter knows how to print.
  static bool IsVariableDeclaration(ASTType Ty);

  /// Print the OpenQASM type spelling for \p DN (no trailing space).
  bool EmitType(const ASTDeclarationNode *DN);

  /// Optionally print ` = <expr>` for declaration initializers.
  void EmitInitializer(const ASTDeclarationNode *DN);

  /// Initializer expression attached to a declaration value node, or null.
  static const ASTExpressionNode *
  GetDeclInitExpression(const ASTExpressionNode *Value);

  void EmitSizedType(const char *Keyword, unsigned Bits, unsigned DefaultBits,
                     bool AlwaysSized);

  void EmitArrayType(const ASTArrayNode *AN);

  void EmitBinaryOp(const ASTBinaryOpNode *BOP);
  void EmitUnaryOp(const ASTUnaryOpNode *UOP);

  void EmitIndent();
  void EmitGateFormalType(const ASTGateNode *GN, unsigned Index);
  void EmitGateParamName(const ASTExpressionNode *E);
  void EmitQuantumOperandList(const ASTGateNode *GN);
  void EmitIdentifierList(const ASTIdentifierList &IL);
  void EmitCtrlPrefix(const char *Keyword, unsigned Bits);
  void EmitGPhase(const ASTGPhaseExpressionNode *GPE,
                  const ASTIdentifierList *QArgs);
  void EmitGateGPhaseExpression(const ASTGateGPhaseExpressionNode *GG);
  void EmitFockControl(const ASTGateFockControlNode *FC);
  void EmitForStatement(const ASTForStatementNode *FS);

  std::ostream &Out() { return OS; }
};

} // namespace QASM

#endif // __QASM_AST_QASM_EMITTER_H
