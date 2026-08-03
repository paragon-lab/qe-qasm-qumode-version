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

#ifndef __QASM_AST_GATE_TEMPLATE_PARAM_BUILDER_H
#define __QASM_AST_GATE_TEMPLATE_PARAM_BUILDER_H

#include <qasm/AST/ASTTypes.h>

#include <string>
#include <utility>
#include <vector>

namespace QASM {

/// Pending gate template parameters while parsing a gate declaration
/// (`gate foo[uint N](...)`). Cleared at the start of each
/// OptGateTemplateParams.
class ASTGateTemplateParamBuilder {
private:
  static ASTGateTemplateParamBuilder TPB;
  std::vector<std::pair<ASTType, std::string>> Params;

protected:
  ASTGateTemplateParamBuilder() = default;

public:
  static ASTGateTemplateParamBuilder &Instance() { return TPB; }

  void Clear() { Params.clear(); }

  void Add(ASTType Ty, const std::string &Name) {
    Params.emplace_back(Ty, Name);
  }

  bool Empty() const { return Params.empty(); }

  std::size_t Size() const { return Params.size(); }

  bool IsTemplateParam(const std::string &Name) const {
    for (std::size_t I = 0; I < Params.size(); ++I)
      if (Params[I].second == Name)
        return true;
    return false;
  }

  /// Index into Params, or ~0U if Name is not a template param.
  unsigned IndexOf(const std::string &Name) const {
    for (std::size_t I = 0; I < Params.size(); ++I)
      if (Params[I].second == Name)
        return static_cast<unsigned>(I);
    return static_cast<unsigned>(~0U);
  }

  const std::vector<std::pair<ASTType, std::string>> &GetParams() const {
    return Params;
  }
};

} // namespace QASM

#endif // __QASM_AST_GATE_TEMPLATE_PARAM_BUILDER_H
