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

#include <qasm/Diagnostic/QasmDiagnostic.h>

#include <variant>

namespace QASM {

std::string ComplexInitPayload::message() const {
  return std::string("Cannot initialize complex from real component of type ") +
         PrintTypeName(RTy) + " and imaginary component of type " +
         PrintTypeName(ITy) + ".";
}

std::string GateParamArraySizeMismatchPayload::message() const {
  return "Gate '" + GateName + "' parameter " + std::to_string(ParamIndex) +
         " expects an array of size " + std::to_string(ExpectedArraySize) +
         ", but got size " + std::to_string(GotArraySize) + ".";
}

std::string GateParamTypeMismatchPayload::message() const {
  return "Gate '" + GateName + "' parameter " + std::to_string(ParamIndex) +
         " expects " + PrintTypeName(ExpectedTy) + ", but got " +
         PrintTypeName(GotTy) + ".";
}

std::string GateParamUnsupportedFormalPayload::message() const {
  return std::string("Unsupported fully-typed classical formal type ") +
         PrintTypeName(ExpectedTy) + " on gate '" + GateName + "'.";
}

std::string UnitaryMatrixRowConstructPayload::message() const {
  return "Could not construct complex matrix elements from unitary "
         "initializer row.";
}

std::string UnitaryMatrixInvalidComplexPayload::message() const {
  return "Invalid complex expression in unitary matrix at column " +
         std::to_string(Column) + ".";
}

void EmitDiagnostic(const Diagnostic &Diag) {
  QasmDiagnosticEmitter::Instance().EmitDiagnostic(
      Diag.Loc,
      std::visit([](const auto &K) { return K.message(); }, Diag.Kind),
      Diag.Level);
}

} // namespace QASM
