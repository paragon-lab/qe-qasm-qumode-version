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

#include <qasm/AST/ASTArray.h>
#include <qasm/AST/ASTCBit.h>
#include <qasm/AST/ASTDuration.h>
#include <qasm/AST/ASTGPhase.h>
#include <qasm/AST/ASTGateFockControl.h>
#include <qasm/AST/ASTGateFor.h>
#include <qasm/AST/ASTGates.h>
#include <qasm/AST/ASTIdentifier.h>
#include <qasm/AST/ASTImplicitConversionExpr.h>
#include <qasm/AST/ASTLoops.h>
#include <qasm/AST/ASTMeasure.h>
#include <qasm/AST/ASTQasmEmitter.h>
#include <qasm/AST/ASTQubit.h>
#include <qasm/AST/ASTStatementBuilder.h>
#include <qasm/AST/ASTStretch.h>
#include <qasm/AST/ASTStringUtils.h>
#include <qasm/AST/ASTSymbolTable.h>
#include <qasm/AST/ASTTypeEnums.h>

#include <cstdio>
#include <sstream>

namespace QASM {

namespace {

std::string FormatFloatLiteral(double V) {
  // Prefer a short decimal that still round-trips common literals (0.3, 1.5).
  char Buf[64];
  std::snprintf(Buf, sizeof(Buf), "%.15g", V);
  return std::string(Buf);
}

/// Peel ASTImplicitConversionNode wrappers to the underlying expression.
const ASTExpressionNode *UnwrapExpr(const ASTExpressionNode *E) {
  while (const ASTImplicitConversionNode *IC =
             dynamic_cast<const ASTImplicitConversionNode *>(E)) {
    const ASTExpressionNode *Inner = nullptr;
    switch (IC->GetConvertFrom()) {
    case ASTTypeBool:
      Inner = IC->GetBool();
      break;
    case ASTTypeInt:
      Inner = IC->GetInt();
      break;
    case ASTTypeFloat:
      Inner = IC->GetFloat();
      break;
    case ASTTypeDouble:
      Inner = IC->GetDouble();
      break;
    case ASTTypeMPInteger:
      Inner = IC->GetMPInteger();
      break;
    case ASTTypeMPDecimal:
      Inner = IC->GetMPDecimal();
      break;
    case ASTTypeMPComplex:
      Inner = IC->GetMPComplex();
      break;
    case ASTTypeBitset:
      Inner = IC->GetCBit();
      break;
    case ASTTypeAngle:
      Inner = IC->GetAngle();
      break;
    case ASTTypeBinaryOp:
      Inner = IC->GetBinaryOp();
      break;
    case ASTTypeUnaryOp:
      Inner = IC->GetUnaryOp();
      break;
    case ASTTypeExpression:
      Inner = IC->GetExpression();
      break;
    default:
      Inner = nullptr;
      break;
    }
    if (!Inner || Inner == E)
      break;
    E = Inner;
  }
  return E;
}

bool IsZeroNumericLiteral(const ASTExpressionNode *E) {
  E = UnwrapExpr(E);
  if (!E)
    return false;
  switch (E->GetASTType()) {
  case ASTTypeInt:
  case ASTTypeUInt:
    if (const ASTIntNode *IN = dynamic_cast<const ASTIntNode *>(E)) {
      if (IN->IsExpression())
        return false;
      return IN->IsSigned() ? IN->GetSignedValue() == 0
                            : IN->GetUnsignedValue() == 0U;
    }
    break;
  case ASTTypeFloat:
    if (const ASTFloatNode *FN = dynamic_cast<const ASTFloatNode *>(E))
      return !FN->IsExpression() && FN->GetValue() == 0.0f;
    break;
  case ASTTypeDouble:
    if (const ASTDoubleNode *DB = dynamic_cast<const ASTDoubleNode *>(E))
      return !DB->IsExpression() && DB->GetValue() == 0.0;
    break;
  case ASTTypeMPDecimal:
    if (const ASTMPDecimalNode *MPD = dynamic_cast<const ASTMPDecimalNode *>(E))
      return !MPD->IsExpression() && MPD->IsZero();
    break;
  default:
    break;
  }
  return false;
}

bool IsFunctionUnary(ASTOpType Op) {
  switch (Op) {
  case ASTOpTypeSin:
  case ASTOpTypeCos:
  case ASTOpTypeTan:
  case ASTOpTypeArcSin:
  case ASTOpTypeArcCos:
  case ASTOpTypeArcTan:
  case ASTOpTypeExp:
  case ASTOpTypeLn:
  case ASTOpTypeSqrt:
  case ASTOpTypeRotl:
  case ASTOpTypeRotr:
  case ASTOpTypePopcount:
    return true;
  default:
    return false;
  }
}

bool IsArrayParamType(ASTType Ty) {
  switch (Ty) {
  case ASTTypeAngleArray:
  case ASTTypeMPDecimalArray:
  case ASTTypeFloatArray:
  case ASTTypeMPComplexArray:
  case ASTTypeIntArray:
  case ASTTypeUIntArray:
  case ASTTypeMPIntegerArray:
  case ASTTypeMPUIntegerArray:
  case ASTTypeBoolArray:
  case ASTTypeCBitArray:
  case ASTTypeCBitNArray:
  case ASTTypeDurationArray:
    return true;
  default:
    return false;
  }
}

} // namespace

void ASTQasmEmitter::Emit(const ASTRoot * /*Root*/) {
  Emit(ASTStatementBuilder::Instance().List());
}

void ASTQasmEmitter::Emit(const ASTStatementList *SL) {
  if (!SL)
    return;

  for (ASTStatementList::const_iterator I = SL->begin(); I != SL->end(); ++I)
    EmitStatement(*I);
}

void ASTQasmEmitter::EmitStatement(const ASTStatement *S) {
  if (!S)
    return;

  switch (S->GetASTType()) {
  case ASTTypeOpenQASMStatement:
    if (const ASTOpenQASMStatementNode *OQ =
            dynamic_cast<const ASTOpenQASMStatementNode *>(S))
      EmitOpenQASM(OQ);
    else
      EmitUnhandled(S);
    return;

  case ASTTypeBinaryOpStatement:
    if (const ASTBinaryOpStatementNode *BOS =
            dynamic_cast<const ASTBinaryOpStatementNode *>(S))
      EmitBinaryOpStatement(BOS);
    else
      EmitUnhandled(S);
    return;

  case ASTTypeMeasure:
    if (const ASTMeasureNode *MN = dynamic_cast<const ASTMeasureNode *>(S))
      EmitMeasure(MN);
    else
      EmitUnhandled(S);
    return;

  case ASTTypeGateDeclaration:
    if (const ASTGateDeclarationNode *GDN =
            dynamic_cast<const ASTGateDeclarationNode *>(S))
      EmitGateDeclaration(GDN);
    else
      EmitUnhandled(S);
    return;

  case ASTTypeForStatement:
    if (const ASTForStatementNode *FS =
            dynamic_cast<const ASTForStatementNode *>(S))
      EmitForStatement(FS);
    else
      EmitUnhandled(S);
    return;

  default:
    break;
  }

  // Gate calls / modifiers share the ASTGateQOpNode hierarchy.
  // Measure and gate-for are handled above / inside EmitGateQOp.
  if (const ASTGateQOpNode *GQ = dynamic_cast<const ASTGateQOpNode *>(S)) {
    EmitGateQOp(GQ);
    return;
  }

  if (const ASTDeclarationNode *DN =
          dynamic_cast<const ASTDeclarationNode *>(S)) {
    if (IsVariableDeclaration(DN->GetASTType())) {
      EmitDeclaration(DN);
      return;
    }
  }

  EmitUnhandled(S);
}

bool ASTQasmEmitter::IsVariableDeclaration(ASTType Ty) {
  switch (Ty) {
  case ASTTypeBitset:
  case ASTTypeBool:
  case ASTTypeInt:
  case ASTTypeUInt:
  case ASTTypeMPInteger:
  case ASTTypeMPUInteger:
  case ASTTypeFloat:
  case ASTTypeDouble:
  case ASTTypeMPDecimal:
  case ASTTypeAngle:
  case ASTTypeMPComplex:
  case ASTTypeDuration:
  case ASTTypeStretch:
  case ASTTypeQubit:
  case ASTTypeQubitContainer:
  case ASTTypeQumode:
  case ASTTypeQumodeContainer:
  case ASTTypeBoolArray:
  case ASTTypeIntArray:
  case ASTTypeUIntArray:
  case ASTTypeMPIntegerArray:
  case ASTTypeMPUIntegerArray:
  case ASTTypeFloatArray:
  case ASTTypeMPDecimalArray:
  case ASTTypeAngleArray:
  case ASTTypeMPComplexArray:
  case ASTTypeCBitArray:
  case ASTTypeCBitNArray:
  case ASTTypeDurationArray:
  case ASTTypeQubitArray:
    return true;
  default:
    return false;
  }
}

void ASTQasmEmitter::EmitOpenQASM(const ASTOpenQASMStatementNode *S) {
  assert(S && "Invalid ASTOpenQASMStatementNode!");

  double Ver = 3.0;
  if (const ASTOpenQASMExpressionNode *OQE =
          dynamic_cast<const ASTOpenQASMExpressionNode *>(S->GetExpression()))
    Ver = OQE->GetVersion();

  OS << "OPENQASM " << std::fixed << std::setprecision(1) << Ver << ";\n";
}

void ASTQasmEmitter::EmitSizedType(const char *Keyword, unsigned Bits,
                                   unsigned DefaultBits, bool AlwaysSized) {
  if (!AlwaysSized && Bits == DefaultBits)
    OS << Keyword;
  else
    OS << Keyword << '[' << Bits << ']';
}

void ASTQasmEmitter::EmitArrayType(const ASTArrayNode *AN) {
  assert(AN && "Invalid ASTArrayNode!");

  unsigned Len = AN->Size();
  unsigned ElemBits = AN->GetElementSize();
  ASTType ElemTy = AN->GetElementType();

  OS << "array[";
  switch (ElemTy) {
  case ASTTypeBitset:
    EmitSizedType("bit", ElemBits, 1U, false);
    break;
  case ASTTypeBool:
    OS << "bool";
    break;
  case ASTTypeInt:
    EmitSizedType("int", ElemBits, ASTIntNode::IntBits, false);
    break;
  case ASTTypeUInt:
    EmitSizedType("uint", ElemBits, ASTIntNode::IntBits, false);
    break;
  case ASTTypeMPInteger:
    EmitSizedType("int", ElemBits, ASTIntNode::IntBits, true);
    break;
  case ASTTypeMPUInteger:
    EmitSizedType("uint", ElemBits, ASTIntNode::IntBits, true);
    break;
  case ASTTypeFloat:
    EmitSizedType("float", ElemBits, ASTFloatNode::FloatBits, false);
    break;
  case ASTTypeMPDecimal:
  case ASTTypeDouble:
    EmitSizedType("float", ElemBits, ASTFloatNode::FloatBits, true);
    break;
  case ASTTypeAngle:
    EmitSizedType("angle", ElemBits, ASTAngleNode::AngleBits, false);
    break;
  case ASTTypeMPComplex:
    OS << "complex[";
    EmitSizedType("float", ElemBits, ASTMPComplexNode::DefaultBits, true);
    OS << ']';
    break;
  case ASTTypeDuration:
    OS << "duration";
    break;
  case ASTTypeQubit:
  case ASTTypeQubitContainer:
    EmitSizedType("qubit", ElemBits, 1U, false);
    break;
  default:
    OS << "/*unsupported-array-element*/";
    break;
  }
  OS << ", " << Len << ']';
}

bool ASTQasmEmitter::EmitType(const ASTDeclarationNode *DN) {
  assert(DN && "Invalid ASTDeclarationNode!");

  ASTType Ty = DN->GetASTType();
  const ASTExpressionNode *EX = DN->GetExpression();

  switch (Ty) {
  case ASTTypeBitset: {
    unsigned Bits = 1U;
    if (const ASTCBitNode *CB = dynamic_cast<const ASTCBitNode *>(EX))
      Bits = static_cast<unsigned>(CB->Size());
    else if (DN->GetIdentifier())
      Bits =
          DN->GetIdentifier()->GetBits() ? DN->GetIdentifier()->GetBits() : 1U;
    EmitSizedType("bit", Bits, 1U, false);
    return true;
  }

  case ASTTypeBool:
    OS << "bool";
    return true;

  case ASTTypeInt:
  case ASTTypeUInt: {
    bool Unsigned = false;
    unsigned Bits = ASTIntNode::IntBits;
    if (const ASTIntNode *IN = dynamic_cast<const ASTIntNode *>(EX)) {
      Unsigned = !IN->IsSigned();
      Bits = IN->GetBits();
    } else if (Ty == ASTTypeUInt) {
      Unsigned = true;
    }
    EmitSizedType(Unsigned ? "uint" : "int", Bits, ASTIntNode::IntBits, false);
    return true;
  }

  case ASTTypeMPInteger:
  case ASTTypeMPUInteger: {
    bool Unsigned = Ty == ASTTypeMPUInteger;
    unsigned Bits = ASTMPIntegerNode::DefaultBits;
    if (const ASTMPIntegerNode *MPI =
            dynamic_cast<const ASTMPIntegerNode *>(EX)) {
      Unsigned = !MPI->IsSigned();
      Bits = MPI->GetBits();
    } else if (DN->GetIdentifier()) {
      Bits = DN->GetIdentifier()->GetBits();
    }
    EmitSizedType(Unsigned ? "uint" : "int", Bits, ASTIntNode::IntBits, true);
    return true;
  }

  case ASTTypeFloat: {
    unsigned Bits = ASTFloatNode::FloatBits;
    if (const ASTFloatNode *FN = dynamic_cast<const ASTFloatNode *>(EX))
      Bits = FN->GetBits();
    EmitSizedType("float", Bits, ASTFloatNode::FloatBits, false);
    return true;
  }

  case ASTTypeDouble:
  case ASTTypeMPDecimal: {
    unsigned Bits = ASTMPDecimalNode::DefaultBits;
    if (const ASTMPDecimalNode *MPD =
            dynamic_cast<const ASTMPDecimalNode *>(EX))
      Bits = MPD->GetBits();
    else if (const ASTDoubleNode *DBN = dynamic_cast<const ASTDoubleNode *>(EX))
      Bits = DBN->GetBits();
    else if (DN->GetIdentifier())
      Bits = DN->GetIdentifier()->GetBits();
    EmitSizedType("float", Bits, ASTFloatNode::FloatBits, true);
    return true;
  }

  case ASTTypeAngle: {
    unsigned Bits = ASTAngleNode::AngleBits;
    if (const ASTAngleNode *AN = dynamic_cast<const ASTAngleNode *>(EX))
      Bits = AN->GetBits();
    else if (DN->GetIdentifier())
      Bits = DN->GetIdentifier()->GetBits();
    EmitSizedType("angle", Bits, ASTAngleNode::AngleBits, false);
    return true;
  }

  case ASTTypeMPComplex: {
    unsigned Bits = ASTMPComplexNode::DefaultBits;
    if (const ASTMPComplexNode *CX = dynamic_cast<const ASTMPComplexNode *>(EX))
      Bits = CX->GetBits();
    else if (DN->GetIdentifier())
      Bits = DN->GetIdentifier()->GetBits();
    if (Bits == ASTMPComplexNode::DefaultBits) {
      OS << "complex";
    } else {
      OS << "complex[";
      EmitSizedType("float", Bits, ASTMPComplexNode::DefaultBits, true);
      OS << ']';
    }
    return true;
  }

  case ASTTypeDuration:
    OS << "duration";
    return true;

  case ASTTypeStretch:
    OS << "stretch";
    return true;

  case ASTTypeQubit:
  case ASTTypeQubitContainer: {
    unsigned Size = 1U;
    if (const ASTQubitContainerNode *QCN =
            dynamic_cast<const ASTQubitContainerNode *>(EX))
      Size = QCN->Size() ? QCN->Size() : QCN->GetBits();
    else if (DN->GetIdentifier())
      Size =
          DN->GetIdentifier()->GetBits() ? DN->GetIdentifier()->GetBits() : 1U;
    if (Size <= 1U)
      OS << "qubit";
    else
      OS << "qubit[" << Size << ']';
    return true;
  }

  case ASTTypeQumode:
  case ASTTypeQumodeContainer: {
    unsigned Size = 1U;
    if (const ASTQumodeContainerNode *QMC =
            dynamic_cast<const ASTQumodeContainerNode *>(EX))
      Size = QMC->Size() ? QMC->Size() : QMC->GetBits();
    else if (const ASTQubitContainerNode *QBC =
                 dynamic_cast<const ASTQubitContainerNode *>(EX))
      Size = QBC->Size() ? QBC->Size() : QBC->GetBits();
    else if (DN->GetIdentifier())
      Size =
          DN->GetIdentifier()->GetBits() ? DN->GetIdentifier()->GetBits() : 1U;
    if (Size <= 1U)
      OS << "qumode";
    else
      OS << "qumode[" << Size << ']';
    return true;
  }

  case ASTTypeBoolArray:
  case ASTTypeIntArray:
  case ASTTypeUIntArray:
  case ASTTypeMPIntegerArray:
  case ASTTypeMPUIntegerArray:
  case ASTTypeFloatArray:
  case ASTTypeMPDecimalArray:
  case ASTTypeAngleArray:
  case ASTTypeMPComplexArray:
  case ASTTypeCBitArray:
  case ASTTypeCBitNArray:
  case ASTTypeDurationArray:
  case ASTTypeQubitArray:
    if (const ASTArrayNode *AN = dynamic_cast<const ASTArrayNode *>(EX)) {
      EmitArrayType(AN);
      return true;
    }
    return false;

  default:
    return false;
  }
}

const ASTExpressionNode *
ASTQasmEmitter::GetDeclInitExpression(const ASTExpressionNode *Value) {
  if (!Value || Value->IsError())
    return nullptr;

  switch (Value->GetASTType()) {
  case ASTTypeInt:
  case ASTTypeUInt:
    if (const ASTIntNode *IN = dynamic_cast<const ASTIntNode *>(Value)) {
      if (IN->IsExpression())
        return UnwrapExpr(IN->GetExpression());
      if (IN->IsConstantFolded() || IN->IsConst())
        return IN;
    }
    return nullptr;

  case ASTTypeMPInteger:
  case ASTTypeMPUInteger:
    if (const ASTMPIntegerNode *MPI =
            dynamic_cast<const ASTMPIntegerNode *>(Value)) {
      if (MPI->IsExpression())
        return UnwrapExpr(MPI->GetExpression());
      if (MPI->IsConstantFolded() || MPI->IsConst())
        return MPI;
    }
    return nullptr;

  case ASTTypeFloat:
    if (const ASTFloatNode *FN = dynamic_cast<const ASTFloatNode *>(Value)) {
      if (FN->IsExpression())
        return UnwrapExpr(FN->GetExpression());
      if (FN->IsConstantFolded() || FN->IsConst())
        return FN;
    }
    return nullptr;

  case ASTTypeDouble:
    if (const ASTDoubleNode *DB = dynamic_cast<const ASTDoubleNode *>(Value)) {
      if (DB->IsExpression())
        return UnwrapExpr(DB->GetExpression());
      if (DB->IsConstantFolded() || DB->IsConst())
        return DB;
    }
    return nullptr;

  case ASTTypeMPDecimal:
    if (const ASTMPDecimalNode *MPD =
            dynamic_cast<const ASTMPDecimalNode *>(Value)) {
      if (MPD->IsExpression())
        return UnwrapExpr(MPD->GetExpression());
      if (MPD->IsConstantFolded() || MPD->IsConst())
        return MPD;
    }
    return nullptr;

  case ASTTypeBool:
    if (const ASTBoolNode *BN = dynamic_cast<const ASTBoolNode *>(Value)) {
      if (BN->IsExpression())
        return UnwrapExpr(BN->GetExpression());
      if (BN->IsConstantFolded() || BN->IsConst())
        return BN;
    }
    return nullptr;

  case ASTTypeAngle:
    if (const ASTAngleNode *AN = dynamic_cast<const ASTAngleNode *>(Value)) {
      if (AN->IsExpression())
        return AN->GetExpression();
      if (!AN->IsNan() &&
          (AN->IsConstantFolded() || AN->IsConst() || AN->IsValue()))
        return AN;
    }
    return nullptr;

  case ASTTypeMPComplex:
    if (const ASTMPComplexNode *CX =
            dynamic_cast<const ASTMPComplexNode *>(Value)) {
      if (CX->IsExpression())
        return UnwrapExpr(CX->GetExpression());
      if (CX->IsConstantFolded() || CX->IsConst())
        return CX;
    }
    return nullptr;

  case ASTTypeDuration:
    if (const ASTDurationNode *DUR =
            dynamic_cast<const ASTDurationNode *>(Value)) {
      if (DUR->GetDuration() != static_cast<uint64_t>(~0x0UL) &&
          DUR->GetLengthUnit() != LengthUnspecified)
        return DUR;
    }
    return nullptr;

  case ASTTypeBitset:
    if (const ASTCBitNode *CB = dynamic_cast<const ASTCBitNode *>(Value)) {
      if (CB->IsConstantFolded() || CB->IsConst())
        return CB;
    }
    return nullptr;

  default:
    return nullptr;
  }
}

void ASTQasmEmitter::EmitInitializer(const ASTDeclarationNode *DN) {
  assert(DN && "Invalid ASTDeclarationNode!");

  const ASTExpressionNode *Init = GetDeclInitExpression(DN->GetExpression());
  if (!Init)
    return;

  OS << " = ";
  EmitExpression(Init);
}

void ASTQasmEmitter::EmitBinaryOpStatement(const ASTBinaryOpStatementNode *S) {
  assert(S && "Invalid ASTBinaryOpStatementNode!");

  const ASTBinaryOpNode *BOP = S->GetBinaryOp();
  if (!BOP || BOP->IsError())
    return;

  EmitExpression(BOP);
  OS << ";\n";
}

namespace {

bool IsAnonymousMeasureResult(const ASTCBitNode *CB) {
  if (!CB || !CB->GetIdentifier())
    return true;
  const std::string &Name = CB->GetName();
  if (Name.empty())
    return true;
  if (Name.rfind("ast-measure-result-", 0) == 0)
    return true;
  const std::string &GP = CB->GetIdentifier()->GetGateParamName();
  return GP == "measure-result";
}

void EmitIndexedName(std::ostream &OS, const std::string &Base, bool HasIndex,
                     unsigned Index) {
  // Some identifiers already carry a `[N]` suffix in their name.
  if (Base.find('[') != std::string::npos) {
    OS << Base;
    return;
  }
  if (HasIndex)
    OS << Base << '[' << Index << ']';
  else
    OS << Base;
}

/// Map internal qubit spellings (`%qs:0`) to OpenQASM (`qs[0]`).
std::string FormatQuantumOperandName(const std::string &Name) {
  if (Name.empty())
    return Name;
  if (ASTStringUtils::Instance().IsQCElement(Name) ||
      ASTStringUtils::Instance().IsIndexedQubit(Name)) {
    const std::string Bracketed =
        ASTStringUtils::Instance().BracketedQubit(Name);
    if (!Bracketed.empty())
      return Bracketed;
  }
  return Name;
}

} // namespace

void ASTQasmEmitter::EmitMeasure(const ASTMeasureNode *MN) {
  assert(MN && "Invalid ASTMeasureNode!");

  if (MN->IsError())
    return;

  // `b = measure …` when the result is a named bit (not the synthetic one).
  if (MN->HasResult()) {
    const ASTCBitNode *CB = MN->GetResult();
    if (CB && !IsAnonymousMeasureResult(CB)) {
      const bool HasRI = MN->HasResultVector();
      EmitIndexedName(OS, CB->GetName(), HasRI,
                      HasRI ? MN->GetResultIndex(0) : 0U);
      OS << " = ";
    }
  }

  OS << "measure";

  const ASTMeasureKeywordArgList &Kwargs = MN->GetKeywordArgs();
  if (!Kwargs.Empty()) {
    OS << '(';
    bool First = true;
    for (const ASTMeasureKeywordArg &KA : Kwargs.GetArgs()) {
      if (!First)
        OS << ", ";
      First = false;
      OS << KA.GetKey() << '=';
      EmitExpression(KA.GetValue());
    }
    OS << ')';
  }

  OS << ' ';

  if (const ASTQubitContainerNode *T = MN->GetTarget()) {
    const bool HasTI = MN->HasTargetVector();
    EmitIndexedName(OS, T->GetName(), HasTI,
                    HasTI ? MN->GetTargetIndex(0) : 0U);
  }

  OS << ";\n";
}

void ASTQasmEmitter::EmitIndent() {
  for (unsigned I = 0; I < IndentLevel; ++I)
    OS << "  ";
}

void ASTQasmEmitter::EmitGateParamName(const ASTExpressionNode *E) {
  if (!E)
    return;
  if (const ASTIdentifierNode *Id = E->GetIdentifier()) {
    const std::string &Name = Id->GetName();
    if (!Name.empty() && Name.find("ast-") != 0)
      OS << Name;
    else if (!E->GetName().empty() && E->GetName().find("ast-") != 0)
      OS << E->GetName();
  } else if (!E->GetName().empty() && E->GetName().find("ast-") != 0) {
    OS << E->GetName();
  }
}

void ASTQasmEmitter::EmitGateFormalType(const ASTGateNode *GN, unsigned Index) {
  assert(GN && "Invalid ASTGateNode!");
  const std::vector<ASTType> &FTs = GN->GetFormalParamTypes();
  if (Index >= FTs.size())
    return;

  const ASTType Ty = FTs[Index];
  const std::vector<unsigned> &Sizes = GN->GetFormalParamArraySizes();
  const std::vector<unsigned> &TplIdxs =
      GN->GetFormalParamArraySizeTemplateIndices();
  const std::vector<ASTGateTemplateParam> &TPs = GN->GetTemplateParams();

  auto EmitArraySize = [&]() {
    if (Index < TplIdxs.size() &&
        TplIdxs[Index] != static_cast<unsigned>(~0U) &&
        TplIdxs[Index] < TPs.size())
      OS << TPs[TplIdxs[Index]].Name;
    else if (Index < Sizes.size() && Sizes[Index] != 0U)
      OS << Sizes[Index];
    else
      OS << '0';
  };

  auto EmitFloatBits = [&](unsigned Bits) {
    if (Bits && Bits != 64U)
      OS << "float[" << Bits << ']';
    else
      OS << "float[64]";
  };

  unsigned ElemBits = 64U;
  if (Index < GN->GetParams().size() && GN->GetParams()[Index].Expr) {
    if (const ASTArrayNode *AN =
            dynamic_cast<const ASTArrayNode *>(GN->GetParams()[Index].Expr))
      ElemBits = AN->GetElementSize() ? AN->GetElementSize() : 64U;
    else if (const ASTMPComplexNode *CX =
                 dynamic_cast<const ASTMPComplexNode *>(
                     GN->GetParams()[Index].Expr))
      ElemBits = CX->GetRealBits() ? CX->GetRealBits() : 64U;
  }

  switch (Ty) {
  case ASTTypeAngle:
    OS << "angle";
    break;
  case ASTTypeMPDecimal:
  case ASTTypeFloat:
  case ASTTypeDouble:
    EmitFloatBits(ElemBits);
    break;
  case ASTTypeMPComplex:
    OS << "complex[";
    EmitFloatBits(ElemBits);
    OS << ']';
    break;
  case ASTTypeAngleArray:
    OS << "array[angle, ";
    EmitArraySize();
    OS << ']';
    break;
  case ASTTypeMPDecimalArray:
  case ASTTypeFloatArray:
    OS << "array[";
    EmitFloatBits(ElemBits);
    OS << ", ";
    EmitArraySize();
    OS << ']';
    break;
  case ASTTypeMPComplexArray:
    OS << "array[complex[";
    EmitFloatBits(ElemBits);
    OS << "], ";
    EmitArraySize();
    OS << ']';
    break;
  default:
    OS << PrintTypeName(Ty);
    break;
  }
}

void ASTQasmEmitter::EmitQuantumOperandList(const ASTGateNode *GN) {
  assert(GN && "Invalid ASTGateNode!");

  const std::vector<const ASTSymbolTableEntry *> &OPs = GN->GetOperandParams();
  const std::map<unsigned, const ASTIdentifierNode *> &OPIds =
      GN->GetOperandParamIds();
  if (!OPs.empty()) {
    OS << ' ';
    for (std::size_t I = 0; I < OPs.size(); ++I) {
      if (I)
        OS << ", ";
      if (!OPs[I] || !OPs[I]->GetIdentifier())
        continue;
      const std::string &QN = OPs[I]->GetIdentifier()->GetName();
      auto MI = OPIds.find(static_cast<unsigned>(I));
      if (MI != OPIds.end() && MI->second) {
        const std::string Base =
            ASTStringUtils::Instance().IsIndexed(QN)
                ? ASTStringUtils::Instance().GetIdentifierBase(QN)
                : QN;
        OS << FormatQuantumOperandName(Base) << '[' << MI->second->GetName()
           << ']';
      } else {
        OS << FormatQuantumOperandName(QN);
      }
    }
  } else if (GN->GetNumOperands() > 0) {
    OS << ' ';
    for (unsigned I = 0; I < GN->GetNumOperands(); ++I) {
      if (I)
        OS << ", ";
      if (const ASTQubitNode *QN = GN->GetOperand(I)) {
        const std::string &GON = QN->GetGateOperandName();
        if (!GON.empty())
          OS << FormatQuantumOperandName(GON);
        else
          OS << FormatQuantumOperandName(QN->GetName());
      }
    }
  }
}

void ASTQasmEmitter::EmitIdentifierList(const ASTIdentifierList &IL) {
  bool First = true;
  for (ASTIdentifierList::const_iterator I = IL.begin(); I != IL.end(); ++I) {
    if (!*I)
      continue;
    if (!First)
      OS << ", ";
    First = false;
    OS << FormatQuantumOperandName((*I)->GetName());
  }
}

void ASTQasmEmitter::EmitCtrlPrefix(const char *Keyword, unsigned Bits) {
  OS << Keyword;
  if (Bits > 1U)
    OS << '(' << Bits << ')';
  OS << " @ ";
}

void ASTQasmEmitter::EmitGPhase(const ASTGPhaseExpressionNode *GPE,
                                const ASTIdentifierList *QArgs) {
  assert(GPE && "Invalid ASTGPhaseExpressionNode!");
  OS << "gphase(";
  if (const ASTBinaryOpNode *BOP = GPE->GetBinaryOp())
    EmitBinaryOp(BOP);
  else if (const ASTUnaryOpNode *UOP = GPE->GetUnaryOp())
    EmitUnaryOp(UOP);
  else if (const ASTIdentifierNode *Id = GPE->GetTargetIdentifier())
    OS << Id->GetName();
  OS << ')';
  if (QArgs && !QArgs->Empty()) {
    OS << ' ';
    EmitIdentifierList(*QArgs);
  }
}

void ASTQasmEmitter::EmitGateGPhaseExpression(
    const ASTGateGPhaseExpressionNode *GG) {
  assert(GG && "Invalid ASTGateGPhaseExpressionNode!");

  if (const ASTGateControlNode *CN = GG->GetControlNode()) {
    EmitCtrlPrefix("ctrl", CN->GetControlBits());
    if (const ASTGPhaseExpressionNode *GPE = CN->GetGPhaseNode()) {
      EmitGPhase(GPE, &GG->GetIdentifierList());
      OS << ";\n";
      return;
    }
    if (const ASTGateQOpNode *Inner = CN->GetGateQOpNode()) {
      if (Inner->GetGateNode() && Inner->GetGateNode()->IsCall()) {
        EmitGateCall(Inner);
        return;
      }
      if (const ASTGPhaseExpressionNode *IG = Inner->GetGPhaseNode()) {
        EmitGPhase(IG, &GG->GetIdentifierList());
        OS << ";\n";
        return;
      }
    }
    if (const ASTGateNode *GN = CN->GetGateNode()) {
      ASTGateQOpNode Tmp(GN->GetIdentifier(), GN);
      EmitGateCall(&Tmp);
      return;
    }
  } else if (const ASTGateNegControlNode *NCN = GG->GetNegControlNode()) {
    EmitCtrlPrefix("negctrl", NCN->GetControlBits());
    if (const ASTGPhaseExpressionNode *GPE = NCN->GetGPhaseNode()) {
      EmitGPhase(GPE, &GG->GetIdentifierList());
      OS << ";\n";
      return;
    }
    if (const ASTGateQOpNode *Inner = NCN->GetGateQOpNode()) {
      if (Inner->GetGateNode() && Inner->GetGateNode()->IsCall()) {
        EmitGateCall(Inner);
        return;
      }
      if (const ASTGPhaseExpressionNode *IG = Inner->GetGPhaseNode()) {
        EmitGPhase(IG, &GG->GetIdentifierList());
        OS << ";\n";
        return;
      }
    }
  } else if (const ASTGPhaseExpressionNode *GPE = GG->GetGPhaseExpression()) {
    EmitGPhase(GPE, &GG->GetIdentifierList());
    OS << ";\n";
    return;
  } else if (const ASTGateQOpNode *Inner = GG->GetGateQOpNode()) {
    if (Inner->GetGateNode() && Inner->GetGateNode()->IsCall()) {
      EmitGateCall(Inner);
      return;
    }
  }
  OS << ";\n";
}

void ASTQasmEmitter::EmitFockControl(const ASTGateFockControlNode *FC) {
  assert(FC && "Invalid ASTGateFockControlNode!");

  OS << "ctrl[";
  if (const ASTIdentifierNode *Id = FC->GetLevelIdentifier())
    OS << Id->GetName();
  else if (const ASTIntNode *IN = FC->GetLevelInt())
    OS << IN->GetString();
  else if (const ASTBinaryOpNode *BOP = FC->GetLevelBinaryOp())
    EmitBinaryOp(BOP);
  else if (const ASTUnaryOpNode *UOP = FC->GetLevelUnaryOp())
    EmitUnaryOp(UOP);
  OS << "] @ ";

  if (const ASTGateGPhaseExpressionNode *GG = FC->GetGateGPhaseNode()) {
    // `ctrl[i] @ gphase(…)` is stored as fock-ctrl around a qubit-ctrl
    // wrapper; the outer fock level already supplies `ctrl[…]`.
    if (const ASTGateControlNode *CN = GG->GetControlNode()) {
      if (const ASTGPhaseExpressionNode *GPE = CN->GetGPhaseNode()) {
        EmitGPhase(GPE, &GG->GetIdentifierList());
        OS << ";\n";
        return;
      }
    }
    EmitGateGPhaseExpression(GG);
    return;
  }
  if (const ASTGateQOpNode *Inner = FC->GetGateQOpNode()) {
    EmitGateQOp(Inner);
    return;
  }
  if (const ASTGateNode *GN = FC->GetGateNode()) {
    ASTGateQOpNode Tmp(GN->GetIdentifier(), GN);
    EmitGateCall(&Tmp);
    return;
  }
  OS << ";\n";
}

void ASTQasmEmitter::EmitForStatement(const ASTForStatementNode *FS) {
  assert(FS && "Invalid ASTForStatementNode!");
  const ASTForLoopNode *Loop = FS->GetLoop();
  if (!Loop)
    return;

  EmitIndent();
  OS << "for ";
  if (const ASTIntNode *IV = Loop->GetIndVar()) {
    if (const ASTIdentifierNode *Id = IV->GetIdentifier())
      OS << Id->GetName();
    else
      OS << IV->GetName();
  }
  OS << " in [";
  const std::vector<const ASTExpression *> &Bounds = Loop->GetIndexBounds();
  for (std::size_t I = 0; I < Bounds.size(); ++I) {
    if (I)
      OS << ':';
    if (!Bounds[I])
      continue;
    if (const ASTExpressionNode *EN =
            dynamic_cast<const ASTExpressionNode *>(Bounds[I]))
      EmitExpression(EN);
    else if (const ASTIdentifierNode *Id =
                 dynamic_cast<const ASTIdentifierNode *>(Bounds[I]))
      OS << Id->GetName();
    else if (const ASTIntNode *IN = dynamic_cast<const ASTIntNode *>(Bounds[I]))
      OS << IN->GetString();
  }
  OS << "] {\n";
  ++IndentLevel;
  Emit(&Loop->GetStatementList());
  --IndentLevel;
  EmitIndent();
  OS << "}\n";
}

void ASTQasmEmitter::EmitGateDeclaration(const ASTGateDeclarationNode *GDN) {
  assert(GDN && "Invalid ASTGateDeclarationNode!");
  if (GDN->IsError())
    return;

  const ASTGateNode *GN = GDN->GetGateNode();
  if (!GN || GN->IsCall())
    return;

  EmitIndent();
  OS << "gate " << GN->GetName();

  const std::vector<ASTGateTemplateParam> &TPs = GN->GetTemplateParams();
  if (!TPs.empty()) {
    OS << '[';
    for (std::size_t I = 0; I < TPs.size(); ++I) {
      if (I)
        OS << ", ";
      OS << "uint " << TPs[I].Name;
    }
    OS << ']';
  }

  const std::vector<ASTGateParam> &Params = GN->GetParams();
  const bool FullyTyped = GN->IsFullyTyped();
  if (!Params.empty() || FullyTyped) {
    OS << '(';
    for (std::size_t I = 0; I < Params.size(); ++I) {
      if (I)
        OS << ", ";
      if (FullyTyped) {
        EmitGateFormalType(GN, static_cast<unsigned>(I));
        OS << ' ';
      }
      EmitGateParamName(Params[I].Expr);
    }
    OS << ')';
  }

  const std::vector<const ASTSymbolTableEntry *> &OPs = GN->GetOperandParams();
  const std::vector<ASTType> &QTys = GN->GetFormalQuantumTypes();
  if (!OPs.empty()) {
    OS << ' ';
    for (std::size_t I = 0; I < OPs.size(); ++I) {
      if (I)
        OS << ", ";
      if (FullyTyped && I < QTys.size())
        OS << PrintTypeName(QTys[I]) << ' ';
      if (OPs[I] && OPs[I]->GetIdentifier())
        OS << FormatQuantumOperandName(OPs[I]->GetIdentifier()->GetName());
    }
  } else if (GN->GetNumOperands() > 0) {
    OS << ' ';
    for (unsigned I = 0; I < GN->GetNumOperands(); ++I) {
      if (I)
        OS << ", ";
      if (FullyTyped && I < QTys.size())
        OS << PrintTypeName(QTys[I]) << ' ';
      if (const ASTQubitNode *QN = GN->GetOperand(I)) {
        const std::string &GON = QN->GetGateOperandName();
        OS << FormatQuantumOperandName(!GON.empty() ? GON : QN->GetName());
      }
    }
  }

  if (GN->IsOpaque()) {
    OS << ";\n";
    return;
  }

  OS << " {\n";
  ++IndentLevel;
  const ASTGateQOpList &Ops = GN->GetOpList();
  for (ASTGateQOpList::const_iterator I = Ops.begin(); I != Ops.end(); ++I)
    if (*I)
      EmitGateQOp(*I);
  --IndentLevel;
  EmitIndent();
  OS << "}\n";
}

void ASTQasmEmitter::EmitGateQOp(const ASTGateQOpNode *GQ) {
  assert(GQ && "Invalid ASTGateQOpNode!");
  if (GQ->IsError())
    return;

  if (const ASTGateForOpNode *For =
          dynamic_cast<const ASTGateForOpNode *>(GQ)) {
    if (const ASTForStatementNode *FS = For->GetForStatement())
      EmitForStatement(FS);
    return;
  }

  if (const ASTGateFockControlNode *FC =
          dynamic_cast<const ASTGateFockControlNode *>(
              GQ->GetExpression() ? GQ->GetExpression() : nullptr)) {
    EmitIndent();
    EmitFockControl(FC);
    return;
  }

  if (const ASTGateGPhaseExpressionNode *GG = GQ->GetGateGPhaseNode()) {
    EmitIndent();
    EmitGateGPhaseExpression(GG);
    return;
  }

  if (const ASTGateControlNode *CN = GQ->GetGateControlNode()) {
    EmitIndent();
    EmitCtrlPrefix("ctrl", CN->GetControlBits());
    if (const ASTGateQOpNode *Inner = CN->GetGateQOpNode()) {
      // EmitGateCall / QOp already includes indent+semi; emit call only.
      if (Inner->GetGateNode() && Inner->GetGateNode()->IsCall())
        EmitGateCall(Inner);
      else
        EmitGateQOp(Inner);
    } else if (const ASTGPhaseExpressionNode *GPE = CN->GetGPhaseNode()) {
      EmitGPhase(GPE, nullptr);
      OS << ";\n";
    } else if (const ASTGateNode *GN = CN->GetGateNode()) {
      ASTGateQOpNode Tmp(GN->GetIdentifier(), GN);
      EmitGateCall(&Tmp);
    } else {
      OS << ";\n";
    }
    return;
  }

  if (const ASTGateNegControlNode *NCN = GQ->GetGateNegControlNode()) {
    EmitIndent();
    EmitCtrlPrefix("negctrl", NCN->GetControlBits());
    if (const ASTGateQOpNode *Inner = NCN->GetGateQOpNode()) {
      if (Inner->GetGateNode() && Inner->GetGateNode()->IsCall())
        EmitGateCall(Inner);
      else
        EmitGateQOp(Inner);
    } else if (const ASTGPhaseExpressionNode *GPE = NCN->GetGPhaseNode()) {
      EmitGPhase(GPE, nullptr);
      OS << ";\n";
    } else if (const ASTGateNode *GN = NCN->GetGateNode()) {
      ASTGateQOpNode Tmp(GN->GetIdentifier(), GN);
      EmitGateCall(&Tmp);
    } else {
      OS << ";\n";
    }
    return;
  }

  if (const ASTGPhaseExpressionNode *GPE = GQ->GetGPhaseNode()) {
    EmitIndent();
    EmitGPhase(GPE, nullptr);
    OS << ";\n";
    return;
  }

  if (GQ->GetGateNode() && GQ->GetGateNode()->IsCall()) {
    EmitIndent();
    EmitGateCall(GQ);
    return;
  }
}

void ASTQasmEmitter::EmitGateCall(const ASTGateQOpNode *GQ) {
  assert(GQ && "Invalid ASTGateQOpNode!");

  if (GQ->IsError())
    return;

  const ASTGateNode *GN = GQ->GetGateNode();
  if (!GN || !GN->IsCall())
    return;

  if (const ASTIdentifierNode *GD = GN->GetGateDefinitionId())
    OS << GD->GetName();
  else
    OS << GN->GetName();

  const std::vector<ASTGateParam> &Params = GN->GetParams();

  // Template bounds inferred from array-literal args are redundant with the
  // array length (`snap([…])` vs `snap[5]([…])`); only emit `[N]` when no
  // array parameter carries that size.
  bool HasArrayParam = false;
  for (std::size_t I = 0; I < Params.size(); ++I) {
    if (IsArrayParamType(Params[I].Ty) ||
        (Params[I].Expr && IsArrayParamType(Params[I].Expr->GetASTType()))) {
      HasArrayParam = true;
      break;
    }
  }

  const std::vector<ASTGateTemplateParam> &TPs = GN->GetTemplateParams();
  bool EmitTemplates = false;
  if (!HasArrayParam) {
    for (std::size_t I = 0; I < TPs.size(); ++I) {
      if (TPs[I].HasBound()) {
        EmitTemplates = true;
        break;
      }
    }
  }
  if (EmitTemplates) {
    OS << '[';
    for (std::size_t I = 0; I < TPs.size(); ++I) {
      if (I)
        OS << ", ";
      if (TPs[I].HasBound())
        OS << *TPs[I].Bound;
      else
        OS << TPs[I].Name;
    }
    OS << ']';
  }

  if (!Params.empty()) {
    OS << '(';
    for (std::size_t I = 0; I < Params.size(); ++I) {
      if (I)
        OS << ", ";
      EmitExpression(Params[I].Expr);
    }
    OS << ')';
  }

  EmitQuantumOperandList(GN);
  OS << ";\n";
}

void ASTQasmEmitter::EmitBinaryOp(const ASTBinaryOpNode *BOP) {
  assert(BOP && "Invalid ASTBinaryOpNode!");

  const bool Parens = BOP->HasParens();
  if (Parens)
    OS << '(';

  if (BOP->IsImaginaryPart()) {
    // Complex literals are stored as `re ± im` with the imag flag on RHS.
    // Pure imaginary `±Xim` uses a zero real part.
    if (!IsZeroNumericLiteral(BOP->GetLeft())) {
      EmitExpression(BOP->GetLeft());
      OS << ' ' << PrintOpTypeOperator(BOP->GetOpType()) << ' ';
    } else if (BOP->GetOpType() == ASTOpTypeSub) {
      OS << '-';
    }
    EmitExpression(BOP->GetRight());
    OS << "im";
  } else {
    EmitExpression(BOP->GetLeft());
    OS << ' ' << PrintOpTypeOperator(BOP->GetOpType()) << ' ';
    EmitExpression(BOP->GetRight());
  }

  if (Parens)
    OS << ')';
}

void ASTQasmEmitter::EmitUnaryOp(const ASTUnaryOpNode *UOP) {
  assert(UOP && "Invalid ASTUnaryOpNode!");

  const ASTOpType Op = UOP->GetOpType();
  const bool Parens = UOP->HasParens();
  if (Parens)
    OS << '(';

  if (Op == ASTOpTypePreInc) {
    OS << "++";
    EmitExpression(UOP->GetExpression());
  } else if (Op == ASTOpTypePreDec) {
    OS << "--";
    EmitExpression(UOP->GetExpression());
  } else if (Op == ASTOpTypePostInc) {
    EmitExpression(UOP->GetExpression());
    OS << "++";
  } else if (Op == ASTOpTypePostDec) {
    EmitExpression(UOP->GetExpression());
    OS << "--";
  } else if (IsFunctionUnary(Op)) {
    OS << PrintOpTypeOperator(Op) << '(';
    EmitExpression(UOP->GetExpression());
    OS << ')';
  } else {
    OS << PrintOpTypeOperator(Op);
    EmitExpression(UOP->GetExpression());
  }

  if (Parens)
    OS << ')';
}

void ASTQasmEmitter::EmitExpression(const ASTExpressionNode *E) {
  if (!E || E->IsError())
    return;

  E = UnwrapExpr(E);
  if (!E)
    return;

  switch (E->GetASTType()) {
  case ASTTypeIdentifier:
  case ASTTypeIdentifierRef:
    if (const ASTIdentifierNode *Id = E->GetIdentifier())
      OS << Id->GetName();
    break;

  case ASTTypeInt:
  case ASTTypeUInt:
    if (const ASTIntNode *IN = dynamic_cast<const ASTIntNode *>(E)) {
      if (IN->IsExpression())
        EmitExpression(UnwrapExpr(IN->GetExpression()));
      else
        OS << IN->GetString();
    }
    break;

  case ASTTypeMPInteger:
  case ASTTypeMPUInteger:
    if (const ASTMPIntegerNode *MPI =
            dynamic_cast<const ASTMPIntegerNode *>(E)) {
      if (MPI->IsExpression())
        EmitExpression(UnwrapExpr(MPI->GetExpression()));
      else
        OS << MPI->GetValue();
    }
    break;

  case ASTTypeFloat:
    if (const ASTFloatNode *FN = dynamic_cast<const ASTFloatNode *>(E)) {
      if (FN->IsExpression())
        EmitExpression(UnwrapExpr(FN->GetExpression()));
      else
        OS << FormatFloatLiteral(FN->GetValue());
    }
    break;

  case ASTTypeDouble:
    if (const ASTDoubleNode *DB = dynamic_cast<const ASTDoubleNode *>(E)) {
      if (DB->IsExpression())
        EmitExpression(UnwrapExpr(DB->GetExpression()));
      else
        OS << FormatFloatLiteral(DB->GetValue());
    }
    break;

  case ASTTypeMPDecimal:
    if (const ASTMPDecimalNode *MPD =
            dynamic_cast<const ASTMPDecimalNode *>(E)) {
      if (MPD->IsExpression())
        EmitExpression(UnwrapExpr(MPD->GetExpression()));
      else
        OS << FormatFloatLiteral(MPD->ToDouble());
    }
    break;

  case ASTTypeBool:
    if (const ASTBoolNode *BN = dynamic_cast<const ASTBoolNode *>(E)) {
      if (BN->IsExpression())
        EmitExpression(UnwrapExpr(BN->GetExpression()));
      else
        OS << (BN->GetValue() ? "true" : "false");
    }
    break;

  case ASTTypeAngle:
    if (const ASTAngleNode *AN = dynamic_cast<const ASTAngleNode *>(E)) {
      if (AN->GetExprType() == ASTTypeIdentifier) {
        if (const ASTIdentifierNode *Id = AN->GetValueIdentifier())
          OS << Id->GetName();
      } else if (AN->IsExpression()) {
        EmitExpression(AN->GetExpression());
      } else if (!AN->IsNan()) {
        OS << FormatFloatLiteral(AN->AsDouble());
      }
    }
    break;

  case ASTTypeAngleArray:
    if (const ASTAngleArrayNode *AAN =
            dynamic_cast<const ASTAngleArrayNode *>(E)) {
      OS << '[';
      for (unsigned I = 0; I < AAN->Size(); ++I) {
        if (I)
          OS << ", ";
        EmitExpression(AAN->GetElement(I));
      }
      OS << ']';
    }
    break;

  case ASTTypeMPDecimalArray:
    if (const ASTMPDecimalArrayNode *DAN =
            dynamic_cast<const ASTMPDecimalArrayNode *>(E)) {
      OS << '[';
      for (unsigned I = 0; I < DAN->Size(); ++I) {
        if (I)
          OS << ", ";
        EmitExpression(DAN->GetElement(I));
      }
      OS << ']';
    }
    break;

  case ASTTypeMPComplexArray:
    if (const ASTMPComplexArrayNode *CAN =
            dynamic_cast<const ASTMPComplexArrayNode *>(E)) {
      OS << '[';
      for (unsigned I = 0; I < CAN->Size(); ++I) {
        if (I)
          OS << ", ";
        EmitExpression(CAN->GetElement(I));
      }
      OS << ']';
    }
    break;

  case ASTTypeMPComplex:
    if (const ASTMPComplexNode *CX =
            dynamic_cast<const ASTMPComplexNode *>(E)) {
      if (CX->IsExpression())
        EmitExpression(UnwrapExpr(CX->GetExpression()));
      else if (const ASTIdentifierNode *Id = CX->GetIdentifier()) {
        // Gate-body `disp(alpha)` clones the formal into a NaN temporary
        // named complexN with PolymorphicName = alpha.
        const std::string &Poly = Id->GetPolymorphicName();
        const std::string &Name = Id->GetName();
        const bool Synthetic =
            Name.rfind("complex", 0) == 0 || Name.rfind("ast-", 0) == 0;
        if (CX->IsNan() && !Poly.empty() && Poly != Name)
          OS << Poly;
        else if (!Synthetic && !Name.empty())
          OS << Name;
        else if (!Poly.empty() && Poly != Name)
          OS << Poly;
        else if (!CX->IsNan())
          OS << CX->GetValue();
      } else if (!CX->IsNan()) {
        OS << CX->GetValue();
      }
    }
    break;

  case ASTTypeDuration:
    if (const ASTDurationNode *DUR = dynamic_cast<const ASTDurationNode *>(E)) {
      if (DUR->GetDuration() != static_cast<uint64_t>(~0x0UL) &&
          DUR->GetLengthUnit() != LengthUnspecified) {
        OS << DUR->GetDuration();
        switch (DUR->GetLengthUnit()) {
        case Nanoseconds:
          OS << "ns";
          break;
        case Microseconds:
          OS << "us";
          break;
        case Milliseconds:
          OS << "ms";
          break;
        case Seconds:
          OS << "s";
          break;
        case DT:
        default:
          OS << "dt";
          break;
        }
      }
    }
    break;

  case ASTTypeBinaryOp:
    if (const ASTBinaryOpNode *BOP = dynamic_cast<const ASTBinaryOpNode *>(E))
      EmitBinaryOp(BOP);
    break;

  case ASTTypeUnaryOp:
    if (const ASTUnaryOpNode *UOP = dynamic_cast<const ASTUnaryOpNode *>(E))
      EmitUnaryOp(UOP);
    break;

  case ASTTypeComplexExpression:
    if (const ASTComplexExpressionNode *CE =
            dynamic_cast<const ASTComplexExpressionNode *>(E)) {
      if (const ASTBinaryOpNode *BOP = CE->GetBinaryOp())
        EmitBinaryOp(BOP);
      else if (const ASTUnaryOpNode *UOP = CE->GetUnaryOp())
        EmitUnaryOp(UOP);
    }
    break;

  case ASTTypeImaginary:
    if (const ASTImaginaryNode *IM =
            dynamic_cast<const ASTImaginaryNode *>(E)) {
      if (const ASTExpressionNode *Inner = IM->GetExpression())
        EmitExpression(Inner);
      OS << "im";
    }
    break;

  default:
    // Fall back to the identifier name when present (e.g. wrapped SSA ids).
    if (const ASTIdentifierNode *Id = E->GetIdentifier()) {
      const std::string &Name = Id->GetName();
      if (!Name.empty() && Name.find("binary-op") == std::string::npos &&
          Name.find("unary-op") == std::string::npos && Name.find("ast-") != 0)
        OS << Name;
    }
    break;
  }
}

void ASTQasmEmitter::EmitDeclaration(const ASTDeclarationNode *DN) {
  assert(DN && "Invalid ASTDeclarationNode!");

  if (DN->IsError())
    return;

  if (DN->IsConst())
    OS << "const ";

  if (!EmitType(DN)) {
    EmitUnhandled(DN);
    return;
  }

  OS << ' ' << DN->GetName();
  EmitInitializer(DN);
  OS << ";\n";
}

} // namespace QASM
