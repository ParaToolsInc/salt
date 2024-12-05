/*
Copyright (C) 2024, ParaTools, Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "flang/Frontend/FrontendActions.h"
#include "flang/Frontend/FrontendPluginRegistry.h"
#include "flang/Parser/dump-parse-tree.h"
#include "flang/Parser/parsing.h"

using namespace Fortran::frontend;

class SaltInstrumentAction : public PluginParseTreeAction {

  // Visitor struct that defines Pre/Post functions for different types of nodes
  struct SaltInstrumentParseTreeVisitor {
    template <typename A> bool Pre(const A &) { return true; }
    template <typename A> void Post(const A &) {}

    bool Pre(const Fortran::parser::FunctionSubprogram &) {
      isInSubprogram_ = true;
      return true;
    }
    void Post(const Fortran::parser::FunctionStmt &f) {
      if (isInSubprogram_) {
        llvm::outs() << "Function:\t"
                     << std::get<Fortran::parser::Name>(f.t).ToString() << "\n";
        isInSubprogram_ = false;
      }
    }

    bool Pre(const Fortran::parser::SubroutineSubprogram &) {
      isInSubprogram_ = true;
      return true;
    }
    void Post(const Fortran::parser::SubroutineStmt &s) {
      if (isInSubprogram_) {
        llvm::outs() << "Subroutine:\t"
                     << std::get<Fortran::parser::Name>(s.t).ToString() << "\n";
        isInSubprogram_ = false;
      }
    }

  private:
    bool isInSubprogram_{false};
  };

  void executeAction() override {
    llvm::outs() << "==== SALT Instrumentor Plugin starting ====\n";

    SaltInstrumentParseTreeVisitor visitor;
    Fortran::parser::Walk(getParsing().parseTree(), visitor);

    llvm::outs() << "==== SALT Instrumentor Plugin finished ====\n";
  }
};

static FrontendPluginRegistry::Add<SaltInstrumentAction> X(
    "salt-instrument", "Apply SALT Instrumentation");
