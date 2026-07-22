/* -*- coding: utf-8 -*-
 *
 * Copyright 2022 IBM RESEARCH. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef __QASM_AST_UNITARY_H
#define __QASM_AST_UNITARY_H

#include <qasm/AST/ASTTypes.h>

namespace QASM {

class ASTUnitaryNode : public ASTExpressionNode {
private:
  ASTUnitaryNode() = delete;

public:
  explicit ASTUnitaryNode(const ASTIdentifierNode *Id)
      : ASTExpressionNode(Id, ASTTypeUnitary) {}

  virtual ~ASTUnitaryNode() = default;

  virtual ASTType GetASTType() const override {
    return ASTTypeUnitary;
  }

  virtual ASTSemaType GetSemaType() const override {
    return SemaTypeExpression;
  }

  virtual void Mangle() override;

  virtual const ASTIdentifierNode *GetIdentifier() const override {
    return ASTExpressionNode::Ident;
  }

  virtual void print() const override;
};

} // namespace QASM

#endif