/* -*- coding: utf-8 -*-
 *
 * Copyright 2022 IBM RESEARCH. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include <qasm/AST/ASTExpression.h>
#include <qasm/AST/ASTExpressionNodeList.h>
#include <qasm/AST/ASTInitializerNode.h>
#include <qasm/AST/ASTMPComplexList.h>
#include <qasm/AST/ASTMangler.h>
#include <qasm/AST/ASTUnitary.h>
#include <qasm/Diagnostic/QasmDiagnostic.h>

#include <cassert>
#include <iostream>
#include <vector>

namespace QASM {

using DiagLevel = QasmDiagnosticEmitter::DiagLevel;

namespace {

ASTLocation RowLocation(const ASTExpressionNodeList *Row) {
  if (Row && !Row->Empty() && Row->front())
    return DIAGLineCounter::Instance().GetLocation(Row->front());
  return DIAGLineCounter::Instance().GetLocation();
}

bool MaterializeComplexRow(ASTExpressionNodeList *Row) {
  assert(Row && "Invalid ASTExpressionNodeList argument!");

  const ASTLocation Loc = RowLocation(Row);

  ASTExpressionList EL;
  for (ASTExpressionNodeList::const_iterator I = Row->begin(); I != Row->end();
       ++I)
    EL.Append(*I);

  ASTMPComplexList CXL(EL);
  if (CXL.Size() != Row->Size()) {
    EmitDiagnostic(
        Diagnostic{Loc, DiagLevel::Error, UnitaryMatrixRowConstructPayload{}});
    return false;
  }

  std::vector<ASTMPComplexNode *> Complexes;
  Complexes.reserve(CXL.Size());
  for (unsigned I = 0; I < CXL.Size(); ++I) {
    ASTMPComplexNode *MPC = CXL.GetComplex(I);
    if (!MPC) {
      EmitDiagnostic(Diagnostic{Loc, DiagLevel::Error,
                                UnitaryMatrixInvalidComplexPayload{I}});
      return false;
    }
    MPC->Mangle();
    Complexes.push_back(MPC);
  }

  Row->Clear();
  for (ASTMPComplexNode *MPC : Complexes)
    Row->Append(MPC);

  return true;
}

} // namespace

bool ASTUnitaryNode::MaterializeComplexCells(const ASTInitializerList *IL) {
  if (!IL)
    return true;

  for (ASTInitializerList::const_iterator I = IL->begin(); I != IL->end();
       ++I) {
    switch ((*I).index()) {
    case 0: {
      if (!MaterializeComplexCells(std::get<0>(*I)))
        return false;
    } break;
    case 1: {
      ASTExpressionNodeList *Row =
          const_cast<ASTExpressionNodeList *>(std::get<1>(*I));
      if (!MaterializeComplexRow(Row))
        return false;
    } break;
    default:
      break;
    }
  }

  return true;
}

void ASTUnitaryNode::Mangle() {
  ASTMangler M;
  M.Start();
  M.TypeIdentifier(GetASTType(), GetName());
  M.EndExpression();
  M.End();

  const_cast<ASTIdentifierNode *>(GetIdentifier())
      ->SetMangledName(M.AsString());
}

ASTUnitaryNode *ASTUnitaryNode::CloneCall(const ASTIdentifierNode *Id,
                                          const ASTArgumentNodeList &AL,
                                          const ASTAnyTypeList &QL) {
  assert(Id && "Invalid ASTIdentifierNode argument!");

  ASTIdentifierNode *UId =
      GateCallIdentifier(Id->GetName(), Id->GetSymbolType(), Id->GetBits());

  assert(UId && "Could not create a valid Unitary Call ASTIdentifierNode!");

  UId->SetSymbolTableEntry(
      const_cast<ASTSymbolTableEntry *>(Id->GetSymbolTableEntry()));

  ASTUnitaryNode *RU = new ASTUnitaryNode(UId, AL, QL, true);

  RU->OpList = OpList;
  RU->GDId = Id;
  RU->Void = Void;
  RU->ControlType = ControlType;
  RU->Opaque = Opaque;
  RU->GateCall = true;

  RU->Mangle();
  return RU;
}

void ASTUnitaryNode::print() const {
  std::cout << "<Unitary>" << std::endl;
  std::cout << "<Identifier>" << GetName() << "</Identifier>" << std::endl;
  std::cout << "<MangledName>" << GetMangledName() << "</MangledName>"
            << std::endl;

  if (INL)
    INL->print();

  std::cout << "</Unitary>" << std::endl;
}

} // namespace QASM
