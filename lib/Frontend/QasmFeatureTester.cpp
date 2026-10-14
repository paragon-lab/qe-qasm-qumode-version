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

#include <qasm/AST/ASTOpenQASMVersionTracker.h>
#include <qasm/Diagnostic/DIAGLineCounter.h>
#include <qasm/Diagnostic/QasmDiagnostic.h>
#include <qasm/Frontend/QasmFeatureTester.h>

#include "QasmParser.tab.h"

#include <set>
#include <string>

namespace QASM {

QasmFeatureTester QasmFeatureTester::QFT;
const std::set<std::string> QasmFeatureTester::FS2 = {
    "dirty",
    "opaque",
    "measure",
    "reset",
};
const std::set<std::string> QasmFeatureTester::FS3 = {
    "angle",    "array",      "barrier",   "measure",
    "defcal",   "extern",     "def",       "defcalgrammar",
    "duration", "durationof", "port",      "frame",
    "waveform", "cal",        "box",       "boxas",
    "if",       "else if",    "else",      "while",
    "for",      "stretch",    "reset",     "pragma",
    "input",    "output",     "let",       "delay",
    "complex",  "return",     "length",    "function",
    "gphase",   "boxto",      "sin",       "cos",
    "tan",      "exp",        "ln",        "pow",
    "asin",     "acos",       "atan",      "arcsin",
    "arccos",   "arctan",     "popcount",  "rotl",
    "rotr",     "sizeof",     "inv",       "ctrl",
    "negcrl",   "sqrt",       "fixed",     "implements",
    "in",       "stretchinf", "verbatim",  "case",
    "default",  "switch",     "do",        "dowhile",
    "@",        "mpinteger",  "mpdecimal", "mpcomplex",
};

using DiagLevel = QasmDiagnosticEmitter::DiagLevel;

void QasmFeatureTester::ValidateFeature(const std::string &E) {
  const ASTLocation Loc = DIAGLineCounter::Instance().GetLocation();
  const double Version = ASTOpenQASMVersionTracker::Instance().GetVersion();

  if (E == "measure" || E == "reset" || E == "barrier")
    return;

  if (Version < 3.0) {
    std::set<std::string>::const_iterator I3 = FS3.find(E);

    if (I3 != FS3.end()) {
      QASM::EmitDiagnostic(Diagnostic{
          Loc, DiagLevel::Error, TokenInvalidInVersionPayload{*I3, Version}});
    }
  } else if (Version >= 3.0) {
    std::set<std::string>::const_iterator I2 = FS2.find(E);

    if (I2 != FS2.end()) {
      QASM::EmitDiagnostic(
          Diagnostic{Loc, DiagLevel::Warning, FeatureDeprecatedPayload{*I2}});
    }
  } else {
    QASM::EmitDiagnostic(Diagnostic{Loc, DiagLevel::Error,
                                    UnknownOpenQASMVersionPayload{Version}});
  }
}

void QasmFeatureTester::ValidateFeature(int Token) {
  const ASTLocation Loc = DIAGLineCounter::Instance().GetLocation();
  const double Version = ASTOpenQASMVersionTracker::Instance().GetVersion();

  if (Version < 3.0) {
    switch (Token) {
    case Parser::token::TOK_BARRIER:
      QASM::EmitDiagnostic(
          Diagnostic{Loc, DiagLevel::Error,
                     TokenInvalidInVersionPayload{"barrier", Version}});
      break;
    case Parser::token::TOK_MEASURE:
      QASM::EmitDiagnostic(
          Diagnostic{Loc, DiagLevel::Error,
                     TokenInvalidInVersionPayload{"measure", Version}});
      break;
    case Parser::token::TOK_MEASURE_PHOTON_COUNT:
      QASM::EmitDiagnostic(Diagnostic{
          Loc, DiagLevel::Error,
          TokenInvalidInVersionPayload{"measure_photon_count", Version}});
      break;
    case Parser::token::TOK_DEFCAL:
      QASM::EmitDiagnostic(
          Diagnostic{Loc, DiagLevel::Error,
                     TokenInvalidInVersionPayload{"defcal", Version}});
      break;
    default:
      break;
    }
  } else if (Version >= 3.0) {
    switch (Token) {
    case Parser::token::TOK_OPAQUE:
      QASM::EmitDiagnostic(
          Diagnostic{Loc, DiagLevel::Warning, FeatureRemovalPayload{"opaque"}});
      break;
    default:
      break;
    }
  } else {
    QASM::EmitDiagnostic(Diagnostic{Loc, DiagLevel::Error,
                                    UnknownOpenQASMVersionPayload{Version}});
  }
}

void QasmFeatureTester::EmitDiagnostic(ASTLocation L, const std::string &M,
                                       QasmDiagnosticEmitter::DiagLevel DL) {
  QasmDiagnosticEmitter::Instance().EmitDiagnostic(L, M, DL);
}

} // namespace QASM
