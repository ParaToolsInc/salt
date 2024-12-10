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

        void setInputFileNameIfNeeded(const std::string &inputName) {
            if (inputFileName_.empty()) {
                inputFileName_ = inputName;
            }
        }

         std::string getInputFileName() {
            return inputFileName_;
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
            llvm::outs() << "Entering main program\n";
            isInMainProgram_ = true;
            return true;
        }

        void Post(const Fortran::parser::MainProgram &) {
            llvm::outs() << "Exiting main program: " << mainProgramName_ << "\n";
            isInMainProgram_ = false;
        }

        void Post(const Fortran::parser::ProgramStmt &program) {
            mainProgramName_ = program.v.ToString();
            const auto &pos = parsing->allCooked().GetSourcePositionRange(program.v.source);
            setInputFileNameIfNeeded(pos->first.sourceFile->path());
            llvm::outs() << "Program: \t"
                    << mainProgramName_
                    << "\t" << inputFileName_
                    << "\t (" << pos->first.line << ", " << pos->first.column << ")"
                    << "\t (" << pos->second.line << ", " << pos->second.column << ")"
                    << "\n";
        }

        void Post(const Fortran::parser::FunctionStmt &f) {
            auto &name = std::get<Fortran::parser::Name>(f.t);
            setInputFileNameIfNeeded(
                parsing->allCooked().GetSourcePositionRange(name.source)->first.sourceFile->path());
            if (isInSubprogram_) {
                llvm::outs() << "Function:\t"
                        << name.ToString() << "\n";
                isInSubprogram_ = false;
            }
        }

        bool Pre(const Fortran::parser::FunctionSubprogram &) {
            isInSubprogram_ = true;
            return true;
        }

        bool Pre(const Fortran::parser::SubroutineSubprogram &) {
            isInSubprogram_ = true;
            return true;
        }

        void Post(const Fortran::parser::SubroutineStmt &s) {
            auto &name = std::get<Fortran::parser::Name>(s.t);
            setInputFileNameIfNeeded(
                parsing->allCooked().GetSourcePositionRange(name.source)->first.sourceFile->path());
            if (isInSubprogram_) {
                llvm::outs() << "Subroutine:\t"
                        << name.ToString() << "\n";
                isInSubprogram_ = false;
            }
        }

        void Post(const Fortran::parser::EndProgramStmt & endProgram) {
            setInputFileNameIfNeeded(
                parsing->allCooked().GetSourcePositionRange(endProgram.v->source)->first.sourceFile->path());
        }

    private:
        // Keeps track of current state of traversal
        bool isInSubprogram_{false};
        bool isInMainProgram_{false};
        std::string mainProgramName_;
        std::string inputFileName_;

        std::vector<SaltInstrumentationPoint> instrumentationPoints_;

        // Pass in the parser object from the Action to the Visitor
        // so that we can use it while processing parse tree nodes.
        Fortran::parser::Parsing *parsing{nullptr};
    };

    void executeAction() override {
        llvm::outs() << "==== SALT Instrumentor Plugin starting ====\n";

        Fortran::parser::Parsing &parsing = getParsing();
        parsing.parseTree()
        // TODO figure out the actual extension of the input and reuse in extension of output file
        // currently we just use inst.f always
        SaltInstrumentParseTreeVisitor visitor{&parsing};
        Walk(parsing.parseTree(), visitor);

        const std::string inputFile = visitor.getInputFileName();
        llvm::outs() << "File: " << inputFile << "\n";
        auto const extPos = inputFile.find_last_of('.');
        const auto inputFileExt = inputFile.substr(extPos + 1);
        llvm::outs() << inputFileExt << "\n";

        auto outputFile = createOutputFile("inst.f");

        llvm::outs() << "==== SALT Instrumentor Plugin finished ====\n";
    }
};

static FrontendPluginRegistry::Add<SaltInstrumentAction> X(
    "salt-instrument", "Apply SALT Instrumentation");
