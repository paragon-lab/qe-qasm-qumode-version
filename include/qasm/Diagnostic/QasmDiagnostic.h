/* -*- coding: utf-8 -*-
 *
 * Copyright 2026 IBM RESEARCH. All Rights Reserved.
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

#ifndef __QASM_DIAGNOSTIC_H
#define __QASM_DIAGNOSTIC_H

#include <qasm/AST/ASTBase.h>
#include <qasm/AST/ASTTypeEnums.h>
#include <qasm/Frontend/QasmDiagnosticEmitter.h>

#include <string>
#include <variant>

namespace QASM {

/// Message-specific payload. Loc/Level live on Diagnostic so every emit has
/// them. Add new alternatives to DiagnosticKind as diagnostics are refactored.
struct ComplexInitPayload {
  ASTType RTy;
  ASTType ITy;

  std::string message() const;
};

struct GateParamArraySizeMismatchPayload {
  std::string GateName;
  unsigned ParamIndex;
  unsigned ExpectedArraySize;
  unsigned GotArraySize;

  std::string message() const;
};

struct GateParamTypeMismatchPayload {
  std::string GateName;
  unsigned ParamIndex;
  ASTType ExpectedTy;
  ASTType GotTy;

  std::string message() const;
};

struct GateParamUnsupportedFormalPayload {
  std::string GateName;
  ASTType ExpectedTy;

  std::string message() const;
};

struct UnitaryMatrixRowConstructPayload {
  std::string message() const;
};

struct UnitaryMatrixInvalidComplexPayload {
  unsigned Column;

  std::string message() const;
};

using DiagnosticKind = std::variant<
    ComplexInitPayload, GateParamArraySizeMismatchPayload,
    GateParamTypeMismatchPayload, GateParamUnsupportedFormalPayload,
    UnitaryMatrixRowConstructPayload, UnitaryMatrixInvalidComplexPayload>;

struct Diagnostic {
  ASTLocation Loc;
  QasmDiagnosticEmitter::DiagLevel Level =
      QasmDiagnosticEmitter::DiagLevel::Error;
  DiagnosticKind Kind;
};

void EmitDiagnostic(const Diagnostic &Diag);

} // namespace QASM

#endif // __QASM_DIAGNOSTIC_H
