/* -*- coding: utf-8 -*-
 *
 * Copyright 2023 IBM RESEARCH. All Rights Reserved.
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
#include <qasm/AST/ASTExpressionValidator.h>
#include <qasm/AST/ASTGateType.h>
#include <qasm/AST/ASTSymbolTable.h>
#include <qasm/AST/ASTTypes.h>

#include <any>
#include <cassert>
#include <optional>

namespace QASM {

namespace {

ASTType ResolveCallClassicalArgType(const ASTArgumentNode *Arg) {
  assert(Arg && "Invalid ASTArgumentNode!");
  ASTType Ty = Arg->GetValueType();
  if (Ty != ASTTypeIdentifier && Ty != ASTTypeIdentifierRef)
    return Ty;

  try {
    const ASTExpressionNode *EN =
        std::any_cast<const ASTExpressionNode *>(Arg->GetValue());
    if (!EN)
      return Ty;
    const ASTIdentifierNode *Id = EN->DynCast<const ASTIdentifierNode>();
    if (!Id)
      return Ty;

    const ASTSymbolTableEntry *STE =
        ASTSymbolTable::Instance().FindLocal(Id->GetName());
    if (!STE)
      STE = ASTSymbolTable::Instance().FindGlobal(Id->GetName());
    if (!STE)
      STE = ASTSymbolTable::Instance().FindAngle(Id->GetName());
    if (!STE)
      STE = Id->GetSymbolTableEntry();
    if (STE)
      return STE->GetValueType();
  } catch (...) {
  }
  return Ty;
}

ASTType ResolveElementType(const ASTExpressionNode *EN) {
  assert(EN && "Invalid ASTExpressionNode!");
  ASTType ElTy = EN->GetASTType();
  if (ElTy == ASTTypeIdentifier || ElTy == ASTTypeIdentifierRef) {
    if (const ASTIdentifierNode *EId = EN->GetIdentifier()) {
      ElTy = EId->GetSymbolType();
      if (const ASTSymbolTableEntry *ESTE = EId->GetSymbolTableEntry())
        ElTy = ESTE->GetValueType();
    }
  }
  return ElTy;
}

bool IsComplexCompatibleCallArg(ASTType ArgTy) {
  switch (ArgTy) {
  case ASTTypeMPComplex:
  case ASTTypeComplexExpression:
  case ASTTypeBinaryOp:
  case ASTTypeUnaryOp:
  case ASTTypeFloat:
  case ASTTypeDouble:
  case ASTTypeMPDecimal:
  case ASTTypeInt:
  case ASTTypeUInt:
  case ASTTypeMPInteger:
  case ASTTypeMPUInteger:
  case ASTTypeAngle:
  case ASTTypeLambdaAngle:
  case ASTTypePhiAngle:
  case ASTTypeThetaAngle:
    return true;
  default:
    return false;
  }
}

bool IsScalarCompatibleCallArg(ASTType ArgTy) {
  switch (ArgTy) {
  case ASTTypeFloat:
  case ASTTypeDouble:
  case ASTTypeMPDecimal:
  case ASTTypeInt:
  case ASTTypeUInt:
  case ASTTypeMPInteger:
  case ASTTypeMPUInteger:
  case ASTTypeBool:
  case ASTTypeBitset:
  case ASTTypeDuration:
  case ASTTypeAngle:
  case ASTTypeLambdaAngle:
  case ASTTypePhiAngle:
  case ASTTypeThetaAngle:
  case ASTTypeBinaryOp:
  case ASTTypeUnaryOp:
    return true;
  default:
    return false;
  }
}

std::optional<unsigned> LiteralArraySize(const ASTArgumentNode *Arg) {
  if (!Arg || !Arg->IsExpression())
    return std::nullopt;

  const ASTExpressionNode *AEN = nullptr;
  try {
    AEN = std::any_cast<const ASTExpressionNode *>(Arg->GetValue());
  } catch (...) {
  }
  if (!AEN) {
    try {
      AEN = std::any_cast<ASTExpressionNode *>(Arg->GetValue());
    } catch (...) {
      return std::nullopt;
    }
  }
  if (!AEN)
    return std::nullopt;
  if (const ASTAngleArrayNode *AAN =
          dynamic_cast<const ASTAngleArrayNode *>(AEN))
    return AAN->Size();
  if (const ASTMPComplexArrayNode *CAN =
          dynamic_cast<const ASTMPComplexArrayNode *>(AEN))
    return CAN->Size();
  if (const ASTArrayNode *ARN = dynamic_cast<const ASTArrayNode *>(AEN))
    return ARN->Size();
  return std::nullopt;
}

} // namespace

bool ASTGateType::IsRealArrayFamily() const {
  return Ty == ASTTypeAngleArray || Ty == ASTTypeFloatArray ||
         Ty == ASTTypeMPDecimalArray;
}

bool ASTGateType::IsComplexArray() const { return Ty == ASTTypeMPComplexArray; }

bool ASTGateType::IsArray() const {
  return IsRealArrayFamily() || IsComplexArray();
}

bool ASTGateType::IsComplexScalar() const { return Ty == ASTTypeMPComplex; }

bool ASTGateType::IsRealScalarFamily() const {
  switch (Ty) {
  case ASTTypeFloat:
  case ASTTypeDouble:
  case ASTTypeMPDecimal:
  case ASTTypeInt:
  case ASTTypeUInt:
  case ASTTypeMPInteger:
  case ASTTypeMPUInteger:
  case ASTTypeBool:
  case ASTTypeBitset:
  case ASTTypeDuration:
  case ASTTypeAngle:
  case ASTTypeLambdaAngle:
  case ASTTypePhiAngle:
  case ASTTypeThetaAngle:
    return true;
  default:
    return false;
  }
}

ASTGateType ASTGateType::ClassifyFormal(ASTType Ty,
                                        std::optional<unsigned> Size) {
  if (Ty == ASTTypeAngleArray || Ty == ASTTypeFloatArray ||
      Ty == ASTTypeMPDecimalArray || Ty == ASTTypeMPComplexArray)
    return ASTGateType(Ty, Size);
  return ASTGateType(Ty, std::nullopt);
}

ASTGateType ASTGateType::ClassifyArg(const ASTArgumentNode *Arg) {
  assert(Arg && "Invalid ASTArgumentNode!");
  ASTType Ty = ResolveCallClassicalArgType(Arg);
  std::optional<unsigned> SZ = LiteralArraySize(Arg);
  if (Ty == ASTTypeAngleArray || Ty == ASTTypeFloatArray ||
      Ty == ASTTypeMPDecimalArray || Ty == ASTTypeMPComplexArray)
    return ASTGateType(Ty, SZ);
  return ASTGateType(Ty, std::nullopt);
}

bool ASTGateType::Compatible(const ASTGateType &F, const ASTGateType &A) {
  if (F.HasArraySize() && A.HasArraySize() &&
      F.GetArraySize() != A.GetArraySize())
    return false;

  if (F.IsComplexScalar()) {
    if (A.IsRealArrayFamily() || A.IsComplexArray())
      return false;
    return IsComplexCompatibleCallArg(A.GetType());
  }

  if (F.IsRealArrayFamily()) {
    if (A.IsComplexArray())
      return false;
    return A.IsRealArrayFamily();
  }

  if (F.IsComplexArray())
    return A.IsComplexArray() || A.IsRealArrayFamily();

  if (F.IsRealScalarFamily()) {
    if (A.IsArray() || A.IsComplexScalar() ||
        A.GetType() == ASTTypeComplexExpression)
      return false;
    return IsScalarCompatibleCallArg(A.GetType());
  }

  return false;
}

bool ASTGateType::ArgHasComplexElements(const ASTArgumentNode *Arg) {
  if (!Arg || !Arg->IsExpression())
    return false;

  const ASTExpressionNode *AEN = nullptr;
  try {
    AEN = std::any_cast<const ASTExpressionNode *>(Arg->GetValue());
  } catch (...) {
  }
  if (!AEN) {
    try {
      AEN = std::any_cast<ASTExpressionNode *>(Arg->GetValue());
    } catch (...) {
      return false;
    }
  }
  if (!AEN)
    return false;
  const ASTAngleArrayNode *AAN = dynamic_cast<const ASTAngleArrayNode *>(AEN);
  if (!AAN)
    return false;
  for (unsigned J = 0; J < AAN->Size(); ++J) {
    const ASTAngleNode *AN = AAN->GetElement(J);
    if (AN &&
        ASTExpressionValidator::Instance().IsComplexType(AN->GetExprType()))
      return true;
  }
  return false;
}

bool ASTGateType::ExpressionListHasComplex(const ASTExpressionList *EL) {
  if (!EL)
    return false;
  for (ASTExpressionList::const_iterator I = EL->begin(); I != EL->end(); ++I) {
    const ASTExpressionNode *EN = dynamic_cast<const ASTExpressionNode *>(*I);
    if (!EN)
      continue;
    if (ASTExpressionValidator::Instance().IsComplexType(
            ResolveElementType(EN)))
      return true;
  }
  return false;
}

} // namespace QASM
