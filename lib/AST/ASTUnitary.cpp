/* -*- coding: utf-8 -*-
 *
 * Copyright 2022 IBM RESEARCH. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include <qasm/AST/ASTUnitary.h>

#include <qasm/AST/ASTMangler.h>

#include <iostream>

namespace QASM {

void ASTUnitaryNode::Mangle() {
  ASTMangler M;
  M.Start();
  M.TypeIdentifier(GetASTType(), GetName());
  M.EndExpression();
  M.End();

  const_cast<ASTIdentifierNode *>(GetIdentifier())
      ->SetMangledName(M.AsString());
}

void ASTUnitaryNode::print() const {
  std::cout << "<Unitary>" << std::endl;
  std::cout << "<Identifier>" << GetName() << "</Identifier>" << std::endl;
  std::cout << "<MangledName>" << GetMangledName() << "</MangledName>"
            << std::endl;
  std::cout << "</Unitary>" << std::endl;
}

} // namespace QASM