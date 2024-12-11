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

#include <clang/Basic/SourceLocation.h>

#include "flang/Frontend/FrontendActions.h"
#include "flang/Frontend/FrontendPluginRegistry.h"
#include "flang/Parser/dump-parse-tree.h"
#include "flang/Parser/parsing.h"
#include "flang/Parser/source.h"
#include "flang/Common/indirection.h"

using namespace Fortran::frontend;

/**
 * The main action of the Salt instrumentor.
 * Visits each node in the parse tree.
 */
class SaltInstrumentAction : public PluginParseTreeAction {
    enum class SaltInstrumentationPointType {
        PROGRAM_BEGIN,
        PROCEDURE_BEGIN,
        PROCEDURE_END
    };

    struct SaltInstrumentationPoint {
        SaltInstrumentationPoint(SaltInstrumentationPointType instrumentation_point_type,
                                 int start_line,
                                 const std::optional<std::string> &timer_name = std::nullopt)
            : instrumentationPointType(instrumentation_point_type),
              startLine(start_line),
              timerName(timer_name) {
        }


        SaltInstrumentationPointType instrumentationPointType;
        int startLine;
        std::optional<std::string> timerName;
    };

    struct SaltInstrumentParseTreeVisitor {
        explicit SaltInstrumentParseTreeVisitor(Fortran::parser::Parsing *parsing)
            : parsing(parsing) {
        }

        /**
         * Mark a line where a given type of instrumentation is needed.
         * For PROGRAM_BEGIN and PROCEDURE_BEGIN, a timer name is needed.
         * For PROCEDURE_END, a timer name is not needed.
         * Instrumentation will be added after start_line.
         */
        void addInstrumentationPoint(SaltInstrumentationPointType instrumentation_point_type,
                                     int start_line,
                                     const std::optional<std::string> &timer_name = std::nullopt) {
            instrumentationPoints_.emplace_back(
                instrumentation_point_type, start_line, timer_name);
        }

        auto &getInstrumentationPoints() {
            return instrumentationPoints_;
        }

        Fortran::parser::SourcePosition locationFromSource(const Fortran::parser::CharBlock &charBlock) const {
            return parsing->allCooked().GetSourcePositionRange(charBlock)->first;
        }

        // Default empty visit functions for otherwise unhandled types.
        template<typename A>
        static bool Pre(const A &) { return true; }

        template<typename A>
        static void Post(const A &) {
        }

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

        // See https://github.com/llvm/llvm-project/blob/main/flang/examples/FlangOmpReport/FlangOmpReportVisitor.cpp
        // for examples of getting source position for a parse tree node

        bool Pre(const Fortran::parser::MainProgram &) {
            isInMainProgram_ = true;
            return true;
        }

        void Post(const Fortran::parser::MainProgram &) {
            llvm::outs() << "Exit main program: " << mainProgramName_ << "\n";
            isInMainProgram_ = false;
        }

        void Post(const Fortran::parser::ProgramStmt &program) {
            mainProgramName_ = program.v.ToString();
            //const auto &pos = parsing->allCooked().GetSourcePositionRange(program.v.source);
            llvm::outs() << "Enter main program: " << mainProgramName_ << "\n";
        }

        bool Pre(const Fortran::parser::SubroutineStmt &subroutineStmt) {
            subprogramName_ = std::get<Fortran::parser::Name>(subroutineStmt.t).ToString();
            llvm::outs() << "Enter Subroutine: " << subprogramName_ << "\n";
            return true;
        }

        void Post(const Fortran::parser::SubroutineSubprogram &) {
            llvm::outs() << "Exit Subroutine: " << subprogramName_ << "\n";
            subprogramName_.clear();
        }

        bool Pre(const Fortran::parser::FunctionStmt &functionStmt) {
            subprogramName_ = std::get<Fortran::parser::Name>(functionStmt.t).ToString();
            llvm::outs() << "Enter Function: " << subprogramName_ << "\n";
            return true;
        }

        void Post(const Fortran::parser::FunctionSubprogram &) {
            llvm::outs() << "Exit Function: " << subprogramName_ << "\n";
            subprogramName_.clear();
        }

        Fortran::parser::SourcePosition getLocation(const Fortran::parser::ExecutableConstruct &construct) {
            return std::visit(
                Fortran::common::visitors{
                    [&](const auto &c) -> Fortran::parser::SourcePosition {
                        return locationFromSource(c.source);
                    }
                }, construct.u);

            /*
            std::variant<Statement<ActionStmt>, common::Indirection<AssociateConstruct>,
      common::Indirection<BlockConstruct>, common::Indirection<CaseConstruct>,
      common::Indirection<ChangeTeamConstruct>,
      common::Indirection<CriticalConstruct>,
      Statement<common::Indirection<LabelDoStmt>>,
      Statement<common::Indirection<EndDoStmt>>,
      common::Indirection<DoConstruct>, common::Indirection<IfConstruct>,
      common::Indirection<SelectRankConstruct>,
      common::Indirection<SelectTypeConstruct>,
      common::Indirection<WhereConstruct>, common::Indirection<ForallConstruct>,
      common::Indirection<CompilerDirective>,
      common::Indirection<OpenACCConstruct>,
      common::Indirection<AccEndCombinedDirective>,
      common::Indirection<OpenMPConstruct>,
      common::Indirection<OmpEndLoopDirective>,
      common::Indirection<CUFKernelDoConstruct>>
      u;
            */
        }

        Fortran::parser::SourcePosition getLocation(const Fortran::parser::ExecutionPartConstruct &construct) {
            // Possibilities for ExecutionPartConstruct:
            //   ExecutableConstruct
            //   Statement<common::Indirection<FormatStmt>>
            //   Statement<common::Indirection<EntryStmt>>
            //   Statement<common::Indirection<DataStmt>>
            //   Statement<common::Indirection<NamelistStmt>>
            //   ErrorRecovery
            return std::visit(
                Fortran::common::visitors{
                    [&](const Fortran::parser::ExecutableConstruct &c) -> Fortran::parser::SourcePosition {
                        return getLocation(c);
                    },
                    [&](const auto &c) -> Fortran::parser::SourcePosition {
                        return locationFromSource(c.source);
                    },
                    [&](const Fortran::parser::ErrorRecovery &c) -> Fortran::parser::SourcePosition {
                        DIE("Should not encounter ErrorRecovery in parse tree");
                    }
                }, construct.u);
        }

        bool Pre(const Fortran::parser::ExecutionPart &executionPart) {
            (void) executionPart; // TODO handle execution part
            // Need to get the FIRST and the LAST components
            // Insert timer start before first component
            // Use main program insert if in main program, else subprogram insert
            // Insert timer end after last component

            const Fortran::parser::Block &block = executionPart.v;
            if (block.empty()) {
                llvm::outs() << "WARNING: Execution part empty.\n";
                return true;
            }

            return true;
        }

    private:
        // Keeps track of current state of traversal
        bool isInMainProgram_{false};
        std::string mainProgramName_;
        std::string subprogramName_;

        std::vector<SaltInstrumentationPoint> instrumentationPoints_;

        // Pass in the parser object from the Action to the Visitor
        // so that we can use it while processing parse tree nodes.
        [[maybe_unused]] Fortran::parser::Parsing *parsing{nullptr};
    };

    /**
     * Get the source file represented by a given parse tree
     *
     * See function BuildRuntimeDerivedTypeTables() in
     * flang/lib/Semantics/runtime-type-info.cpp for example
     * of getting the source file name.
     */
    static std::optional<std::string> getInputFilePath(Fortran::parser::Parsing &parsing) {
        const auto &allSources{parsing.allCooked().allSources()};
        if (auto firstProv{allSources.GetFirstFileProvenance()}) {
            if (const auto *srcFile{allSources.GetSourceFile(firstProv->start())}) {
                return srcFile->path();
            }
        }
        return std::nullopt;
    }


    /**
     * This is the entry point for the plugin.
     */
    void executeAction() override {
        llvm::outs() << "==== SALT Instrumentor Plugin starting ====\n";

        // This is the object through which we access the parse tree
        // and the source
        Fortran::parser::Parsing &parsing = getParsing();

        // Get the path to the input file
        const auto inputFilePath = getInputFilePath(parsing);
        if (!inputFilePath) {
            llvm::outs() << "ERROR: Unable to find input file name!\n";
            std::exit(-1);
        }
        llvm::outs() << "Have input file: " << *inputFilePath << "\n";

        // Get the extension of the input file
        // For input file 'filename.ext' we will output to 'filename.inst.ext'
        std::string inputFileExtension;
        if (auto const extPos = inputFilePath->find_last_of('.'); extPos == std::string::npos) {
            inputFileExtension = "f90"; // Default if for some reason file has no extension
        } else {
            inputFileExtension = inputFilePath->substr(extPos + 1); // Part of string past last '.'
        }

        // Open an output file for writing the instrumented code
        const std::string outputFileExtension = "inst."s + inputFileExtension;
        auto outputFile = createOutputFile(outputFileExtension);

        // Walk the parse tree
        SaltInstrumentParseTreeVisitor visitor{&parsing};
        Walk(parsing.parseTree(), visitor);

        // TODO write the instrumented code

        llvm::outs() << "==== SALT Instrumentor Plugin finished ====\n";
    }
};

static FrontendPluginRegistry::Add<SaltInstrumentAction> X(
    "salt-instrument", "Apply SALT Instrumentation");
