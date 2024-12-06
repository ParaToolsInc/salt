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

/* SALT-FM Flang Fortran Instrumentor Plugin */

// See https://flang.llvm.org/docs/FlangDriver.html#frontend-driver-plugins
// for documentation of the Flang frontend plugin interface

#include "flang/Frontend/FrontendActions.h"
#include "flang/Frontend/FrontendPluginRegistry.h"
#include "flang/Parser/dump-parse-tree.h"
#include "flang/Parser/parsing.h"

using namespace Fortran::frontend;

/**
 * The main action of the Salt instrumentor.
 * Visits each node in the parse tree.
 */
class SaltInstrumentAction : public PluginParseTreeAction {
  struct SaltInstrumentParseTreeVisitor {

    explicit SaltInstrumentParseTreeVisitor(Fortran::parser::Parsing *parsing)
      : parsing(parsing) {
    }

    // Pass in the parser object from the Action to the Visitor
    // so that we can use it while processing parse tree nodes.
    Fortran::parser::Parsing *parsing{nullptr};

    // Default empty visit functions for otherwise unhandled types.
    template <typename A> bool Pre(const A &) { return true; }
    template <typename A> void Post(const A &) {}

    // Override all types that we want to visit.
    // Pre occurs when first visiting a node.
    // Post occurs when returning from the node's children.
    // See https://flang.llvm.org/docs/Parsing.html for information on the parse tree.
    // Parse tree types are defined in: include/flang/Parser/parse-tree.h
    // There are three types of parse tree nodes:
    // Wrappers, with a single data member, always named `v`.
    // Tuples, encapsulating multiple values in a data member named `t` of type std::tuple.
    // Discriminated unions, one of several types stored in data member named `u` of type std::variant.
    // Use std::get() to retrieve value from `t` or `u`

    bool Pre(const Fortran::parser::FunctionSubprogram &) {
      isInSubprogram_ = true;
      return true;
    }

    // See https://github.com/llvm/llvm-project/blob/main/flang/examples/FlangOmpReport/FlangOmpReportVisitor.cpp
    // for examples of getting source position for a parse tree node
    void Post(const Fortran::parser::ProgramStmt & program) {
      const auto & pos = parsing->allCooked().GetSourcePositionRange(program.v.source);
      llvm::outs() << "Program: \t"
                   <<  program.v.ToString()
                   << "\t (" << pos->first.line << ", " << pos->first.column << ")"
                   << "\t (" << pos->second.line << ", " << pos->second.column << ")"
                   << "\n";
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

    Fortran::parser::Parsing & parsing = getParsing();
    SaltInstrumentParseTreeVisitor visitor{&parsing};
    Fortran::parser::Walk(parsing.parseTree(), visitor);

    llvm::outs() << "==== SALT Instrumentor Plugin finished ====\n";
  }
};

static FrontendPluginRegistry::Add<SaltInstrumentAction> X(
    "salt-instrument", "Apply SALT Instrumentation");
