/* -*- coding: utf-8 -*-
 *
 * Copyright 2022 IBM RESEARCH. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef __QASM_AST_UNITARY_H
#define __QASM_AST_UNITARY_H

#include <qasm/AST/ASTTypes.h>
#include <qasm/AST/ASTGates.h>

namespace QASM {

// class ASTUnitaryNode : public ASTExpressionNode {
class ASTUnitaryNode : public ASTGateNode {
private:
  ASTUnitaryNode() = delete;

public:
  explicit ASTUnitaryNode(const ASTIdentifierNode *Id)
      // : ASTExpressionNode(Id, ASTTypeUnitary) {}
        : ASTGateNode(Id) {}

        //added this block of code
  ASTUnitaryNode(const ASTIdentifierNode *Id,
                 const ASTArgumentNodeList &AL,
                 const ASTAnyTypeList &QL,
                 bool IsGateCall = false)
      : ASTGateNode(Id, AL, QL, IsGateCall) {}


  virtual ~ASTUnitaryNode() = default;

  virtual ASTType GetASTType() const override {
    return ASTTypeUnitary;
  }

  virtual ASTSemaType GetSemaType() const override {
    return SemaTypeExpression;
  }
  //added this block of code
  virtual ASTUnitaryNode *
  CloneCall(const ASTIdentifierNode *Id,
            const ASTArgumentNodeList &AL,
            const ASTAnyTypeList &QL) override;


  virtual void Mangle() override;

  virtual const ASTIdentifierNode *GetIdentifier() const override {
    // return ASTExpressionNode::Ident;
    return ASTGateNode::GetIdentifier();
  }

  virtual void print() const override;
};

} // namespace QASM

#endif