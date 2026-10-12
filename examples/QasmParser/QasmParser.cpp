/* -*- coding: utf-8 -*-
 *
 * Copyright 2021 IBM RESEARCH. All Rights Reserved.
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

#include <qasm/AST/AST.h>
#include <qasm/AST/ASTObjectTracker.h>
#include <qasm/AST/ASTQasmEmitter.h>
#include <qasm/Frontend/QasmParser.h>

#include <cstring>
#include <iostream>
#include <vector>

static void Usage() {
  std::cerr << "Usage: QasmParser [-keep-temps] [-emit-qasm] ";
  std::cerr << "[-I<include-dir> [ -I<include-dir> ...]] ";
  std::cerr << "\n                  <translation-unit>" << std::endl;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    Usage();
    return 1;
  }

  bool EmitQasm = false;
  std::vector<char *> FilteredArgs;
  FilteredArgs.reserve(static_cast<std::size_t>(argc));
  FilteredArgs.push_back(argv[0]);
  for (int I = 1; I < argc; ++I) {
    if (std::strcmp(argv[I], "-emit-qasm") == 0) {
      EmitQasm = true;
      continue;
    }
    FilteredArgs.push_back(argv[I]);
  }

  // Enabling the ASTObjectTracker is optional.
  // Nothing bad will happen if it's not enabled. By default, the memory
  // allocated by the AST Generator is handed over unmanaged, and will be
  // automatically released at program exit.
  // The ASTObjectTracker manages the memory dynamically allocated by the
  // AST Generator. It is enabled here for illustration purposes.
  QASM::ASTObjectTracker::Instance().Enable();

  QASM::ASTParser Parser;
  Parser.ParseCommandLineArguments(static_cast<int>(FilteredArgs.size()),
                                   FilteredArgs.data());
  QASM::ASTRoot *Root = Parser.ParseAST();
  if (EmitQasm) {
    QASM::ASTQasmEmitter Emitter(std::cout);
    Emitter.Emit(Root);
  } else {
    Root->print();
  }

  // If the ASTObjectTracker is not enabled, this is a no-op.
  QASM::ASTObjectTracker::Instance().Release();

  return 0;
}
