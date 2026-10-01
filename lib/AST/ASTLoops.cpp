/* -*- coding: utf-8 -*-
 *
 * Copyright 2022 IBM RESEARCH. All Rights Reserved.
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

#include <qasm/AST/ASTLoops.h>
#include <qasm/AST/ASTMangler.h>
#include <qasm/AST/ASTSymbolTable.h>

#include <any>

namespace QASM {

bool ASTForRangePrefix::TryIntValue(const ASTIdentifierNode *Id, int32_t &Out) {
  if (!Id)
    return false;

  ASTSymbolTableEntry *STE = ASTSymbolTable::Instance().Lookup(Id->GetName());
  if (!STE || !STE->HasValue())
    return false;

  try {
    if (STE->GetValueType() == ASTTypeInt) {
      ASTIntNode *IN = STE->GetValue()->GetValue<ASTIntNode *>();
      if (!IN)
        return false;
      Out = IN->IsSigned() ? IN->GetSignedValue()
                           : static_cast<int32_t>(IN->GetUnsignedValue());
      return true;
    }
    if (STE->GetValueType() == ASTTypeMPInteger) {
      ASTMPIntegerNode *MPI = STE->GetValue()->GetValue<ASTMPIntegerNode *>();
      if (!MPI)
        return false;
      Out = MPI->ToSignedInt();
      return true;
    }
  } catch (const std::bad_any_cast &) {
    return false;
  }

  return false;
}

void ASTForRangePrefix::AppendValue(int32_t V) {
  Element E;
  E.Symbol = nullptr;
  E.HasValue = true;
  E.Value = V;
  Elems.push_back(E);
}

void ASTForRangePrefix::AppendSymbol(const ASTIdentifierNode *Id) {
  Element E;
  E.Symbol = Id;
  E.Value = 0;
  E.HasValue = TryIntValue(Id, E.Value);
  Elems.push_back(E);
}

std::vector<const ASTExpression *> ASTForRangePrefix::ToExpressions() const {
  std::vector<const ASTExpression *> Out;
  Out.reserve(Elems.size());
  for (const Element &E : Elems)
    if (E.Symbol)
      Out.push_back(E.Symbol);
    else if (E.HasValue)
      Out.push_back(new ASTIntNode(E.Value));
  return Out;
}

ASTIntegerList *ASTForRangePrefix::ToIntegerList() const {
  ASTIntegerList *IL = new ASTIntegerList();
  IL->SetSeparator(':');
  for (const Element &E : Elems)
    if (E.Symbol || !E.HasValue)
      return IL;
  for (const Element &E : Elems)
    IL->Append(E.Value);
  return IL;
}

void ASTForLoopRangeExpressionNode::ApplyPrefix(const ASTForRangePrefix *P) {
  if (!P || P->Size() == 0)
    return;

  const std::vector<ASTForRangePrefix::Element> &E = P->GetElements();
  StartSymbol = E[0].Symbol;
  if (E.size() >= 2) {
    ExplicitStep = true;
    StepSymbol = E[1].Symbol;
    StepValue = E[1].HasValue ? E[1].Value : 0;
  }
}

void ASTForLoopRangeExpressionNode::Mangle() {
  ASTMangler M;
  M.Start();
  M.Type(ASTTypeForLoopRange);
  M.EndExpression();
  M.End();
}

} // namespace QASM
