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

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <variant>
#include <optional>
#include <tuple>


#define RYML_SINGLE_HDR_DEFINE_NOW
#define RYML_SHARED

#include <ryml_all.hpp>

#include <clang/Basic/SourceLocation.h>

#include "flang/Frontend/FrontendActions.h"
#include "flang/Frontend/FrontendPluginRegistry.h"
#include "flang/Parser/dump-parse-tree.h"
#include "flang/Parser/parsing.h"
#include "flang/Parser/source.h"
#include "flang/Common/indirection.h"

// TODO Split declarations into a separate header file.

#define SALT_FORTRAN_CONFIG_FILE_VAR "SALT_FORTRAN_CONFIG_FILE"
#define SALT_FORTRAN_CONFIG_DEFAULT_PATH "config_files/fortran_config.yaml"

using namespace Fortran::frontend;


/**
 * The main action of the Salt instrumentor.
 * Visits each node in the parse tree.
 */
class SaltInstrumentAction final : public PluginParseTreeAction {
    enum class SaltInstrumentationPointType {
        PROGRAM_BEGIN, // Declare profiler, initialize TAU, set node, start timer
        PROCEDURE_BEGIN, // Declare profiler, start timer
        PROCEDURE_END // Stop timer
    };

    struct SaltInstrumentationPoint {
        SaltInstrumentationPoint(const SaltInstrumentationPointType instrumentation_point_type,
                                 const int start_line,
                                 const std::optional<std::string> &timer_name = std::nullopt)
            : instrumentationPointType(instrumentation_point_type),
              startLine(start_line),
              timerName(timer_name) {
        }

        [[nodiscard]] bool instrumentBefore() const {
            return instrumentationPointType == SaltInstrumentationPointType::PROGRAM_BEGIN || instrumentationPointType
                   == SaltInstrumentationPointType::PROCEDURE_BEGIN;
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

        [[nodiscard]] const auto &getInstrumentationPoints() const {
            return instrumentationPoints_;
        }

        /**
         * From a CharBlock object (generally held in the `source` field of a parse tree node,
         * get the source position (file, line, column).
         * If `end` is set, returns the ending position of the block.
         * If `end` is not set (and by default), returns the starting position of the block.
         */
        [[nodiscard]] Fortran::parser::SourcePosition locationFromSource(
            const Fortran::parser::CharBlock &charBlock, const bool end) const {
            const auto &sourceRange{parsing->allCooked().GetSourcePositionRange(charBlock)};
            if (end) {
                return sourceRange->second;
            }
            return sourceRange->first;
        }

        // Default empty visit functions for otherwise unhandled types.
        template<typename A>
        static bool Pre(const A &) { return true; }

        template<typename A>
        static void Post(const A &) {
            // this space intentionally left blank
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

        // TODO split location-getting routines into a separate file

        [[nodiscard]] Fortran::parser::SourcePosition getLocation(
            const Fortran::parser::OpenMPDeclarativeConstruct &construct,
            const bool end) {
            // This function is based on the equivalent function in
            // flang/examples/FlangOmpReport/FlangOmpReportVisitor.cpp
            return std::visit(
                [&](const auto &o) -> Fortran::parser::SourcePosition {
                    return locationFromSource(o.source, end);
                },
                construct.u);
        }

        [[nodiscard]] Fortran::parser::SourcePosition getLocation(const Fortran::parser::OpenMPConstruct &construct,
                                                                  const bool end) {
            // This function is based on the equivalent function in
            // flang/examples/FlangOmpReport/FlangOmpReportVisitor.cpp
            return std::visit(
                Fortran::common::visitors{
                    [&](const Fortran::parser::OpenMPStandaloneConstruct &c) -> Fortran::parser::SourcePosition {
                        return locationFromSource(c.source, end);
                    },
                    // OpenMPSectionsConstruct, OpenMPLoopConstruct,
                    // OpenMPBlockConstruct, OpenMPCriticalConstruct Get the source from
                    // the directive field.
                    [&](const auto &c) -> Fortran::parser::SourcePosition {
                        const Fortran::parser::CharBlock &source{std::get<0>(c.t).source};
                        return locationFromSource(source, end);
                    },
                    [&](const Fortran::parser::OpenMPAtomicConstruct &c) -> Fortran::parser::SourcePosition {
                        return std::visit(
                            [&](const auto &o) -> Fortran::parser::SourcePosition {
                                const Fortran::parser::CharBlock &source{
                                    std::get<Fortran::parser::Verbatim>(o.t).source
                                };
                                return locationFromSource(source, end);
                            },
                            c.u);
                    },
                    [&](const Fortran::parser::OpenMPSectionConstruct &c) -> Fortran::parser::SourcePosition {
                        const Fortran::parser::CharBlock &source{c.source};
                        return locationFromSource(source, end);
                    },
                },
                construct.u);
        }

        [[nodiscard]] Fortran::parser::SourcePosition
        getLocation(const Fortran::parser::OpenACCConstruct &construct, const bool end) {
            // This function is based on the equivalent function in
            // flang/examples/FlangOmpReport/FlangOmpReportVisitor.cpp
            return std::visit(
                Fortran::common::visitors{
                    [&](const auto &c) -> Fortran::parser::SourcePosition {
                        return locationFromSource(c.source, end);
                    },
                    [&](const Fortran::parser::OpenACCBlockConstruct &c) -> Fortran::parser::SourcePosition {
                        if (end) {
                            return locationFromSource(std::get<Fortran::parser::AccEndBlockDirective>(c.t).source,
                                                      end);
                        }
                        return locationFromSource(std::get<Fortran::parser::AccBeginBlockDirective>(c.t).source, end);
                    },
                    [&](const Fortran::parser::OpenACCLoopConstruct &c) -> Fortran::parser::SourcePosition {
                        // TODO handle end case (complicated because end statement and do construct are optional)
                        return locationFromSource(std::get<Fortran::parser::AccBeginLoopDirective>(c.t).source, end);
                    },
                }, construct.u);
        }

        [[nodiscard]] Fortran::parser::SourcePosition getLocation(const Fortran::parser::ExecutableConstruct &construct,
                                                                  const bool end) {
            /* Possibilities for ExecutableConstruct:
                 Statement<ActionStmt>
                 common::Indirection<AssociateConstruct>
                 common::Indirection<BlockConstruct>
                 common::Indirection<CaseConstruct>
                 common::Indirection<ChangeTeamConstruct>
                 common::Indirection<CriticalConstruct>
                 Statement<common::Indirection<LabelDoStmt>>
                 Statement<common::Indirection<EndDoStmt>>
                 common::Indirection<DoConstruct>
                 common::Indirection<IfConstruct>
                 common::Indirection<SelectRankConstruct>
                 common::Indirection<SelectTypeConstruct>
                 common::Indirection<WhereConstruct>
                 common::Indirection<ForallConstruct>
                 common::Indirection<CompilerDirective>
                 common::Indirection<OpenACCConstruct>
                 common::Indirection<AccEndCombinedDirective>
                 common::Indirection<OpenMPConstruct>
                 common::Indirection<OmpEndLoopDirective>
                 common::Indirection<CUFKernelDoConstruct>
            */
            return std::visit(
                Fortran::common::visitors{
                    [&](const auto &c) -> Fortran::parser::SourcePosition {
                        return locationFromSource(c.source, end);
                    },
                    [&](const Fortran::common::Indirection<Fortran::parser::CUFKernelDoConstruct> &c) ->
                Fortran::parser::SourcePosition {
                        if (end) {
                            const auto &optionalConstruct = std::get<std::optional<Fortran::parser::DoConstruct> >(
                                c.value().t);
                            if (optionalConstruct.has_value()) {
                                return locationFromSource(
                                    std::get<Fortran::parser::Statement<Fortran::parser::EndDoStmt> >(
                                        optionalConstruct.value().t).source, end);
                            }
                        }
                        return locationFromSource(
                            std::get<Fortran::parser::CUFKernelDoConstruct::Directive>(c.value().t).source, end);
                    },
                    [&](const Fortran::common::Indirection<Fortran::parser::OmpEndLoopDirective> &c) ->
                Fortran::parser::SourcePosition {
                        return locationFromSource(c.value().source, end);
                    },
                    [&](const Fortran::common::Indirection<Fortran::parser::OpenMPConstruct> &c) ->
                Fortran::parser::SourcePosition {
                        return getLocation(c.value(), end);
                    },
                    [&](const Fortran::common::Indirection<Fortran::parser::AccEndCombinedDirective> &c) ->
                Fortran::parser::SourcePosition {
                        return locationFromSource(c.value().source, end);
                    },
                    [&](const Fortran::common::Indirection<Fortran::parser::OpenACCConstruct> &c) ->
                Fortran::parser::SourcePosition {
                        return getLocation(c.value(), end);
                    },
                    [&](const Fortran::common::Indirection<Fortran::parser::CompilerDirective> &c)->
                Fortran::parser::SourcePosition {
                        return locationFromSource(c.value().source, end);
                    },
                    [&](const Fortran::common::Indirection<Fortran::parser::ForallConstruct> &c) ->
                Fortran::parser::SourcePosition {
                        if (end) {
                            return locationFromSource(
                                std::get<Fortran::parser::Statement<Fortran::parser::EndForallStmt> >(c.value().t).
                                source, end);
                        }
                        return locationFromSource(
                            std::get<Fortran::parser::Statement<Fortran::parser::ForallConstructStmt> >(c.value().t).
                            source, end);
                    },
                    [&](const Fortran::common::Indirection<Fortran::parser::WhereConstruct> &c) ->
                Fortran::parser::SourcePosition {
                        if (end) {
                            return locationFromSource(
                                std::get<Fortran::parser::Statement<Fortran::parser::EndWhereStmt> >(c.value().t).
                                source, end);
                        }
                        return locationFromSource(
                            std::get<Fortran::parser::Statement<Fortran::parser::WhereConstructStmt> >(c.value().t).
                            source, end);
                    },
                    [&](const Fortran::common::Indirection<Fortran::parser::SelectTypeConstruct> &c) ->
                Fortran::parser::SourcePosition {
                        if (end) {
                            return locationFromSource(
                                std::get<Fortran::parser::Statement<Fortran::parser::EndSelectStmt> >(c.value().t).
                                source, end);
                        }
                        return locationFromSource(
                            std::get<Fortran::parser::Statement<Fortran::parser::SelectTypeStmt> >(c.value().t).source,
                            end);
                    },
                    [&](const Fortran::common::Indirection<Fortran::parser::SelectRankConstruct> &c) ->
                Fortran::parser::SourcePosition {
                        if (end) {
                            return locationFromSource(
                                std::get<Fortran::parser::Statement<Fortran::parser::EndSelectStmt> >(c.value().t).
                                source, end);
                        }
                        return locationFromSource(
                            std::get<Fortran::parser::Statement<Fortran::parser::SelectRankStmt> >(c.value().t).
                            source, end);
                    },
                    [&](const Fortran::common::Indirection<Fortran::parser::IfConstruct> &c) ->
                Fortran::parser::SourcePosition {
                        if (end) {
                            return locationFromSource(
                                std::get<Fortran::parser::Statement<Fortran::parser::EndIfStmt> >(c.value().t).source,
                                end);
                        }
                        return locationFromSource(
                            std::get<Fortran::parser::Statement<Fortran::parser::IfThenStmt> >(c.value().t).source,
                            end);
                    },
                    [&](const Fortran::common::Indirection<Fortran::parser::DoConstruct> &c) ->
                Fortran::parser::SourcePosition {
                        if (end) {
                            return locationFromSource(
                                std::get<Fortran::parser::Statement<Fortran::parser::EndDoStmt> >(c.value().t).source,
                                end);
                        }
                        return locationFromSource(
                            std::get<Fortran::parser::Statement<Fortran::parser::NonLabelDoStmt> >(c.value().t).source,
                            end);
                    },
                    [&](const Fortran::common::Indirection<Fortran::parser::CriticalConstruct> &c) ->
                Fortran::parser::SourcePosition {
                        if (end) {
                            return locationFromSource(
                                std::get<Fortran::parser::Statement<Fortran::parser::EndCriticalStmt> >(c.value().t).
                                source,
                                end);
                        }
                        return locationFromSource(
                            std::get<Fortran::parser::Statement<Fortran::parser::CriticalStmt> >(c.value().t).source,
                            end);
                    },
                    [&](const Fortran::common::Indirection<Fortran::parser::ChangeTeamConstruct> &c) ->
                Fortran::parser::SourcePosition {
                        if (end) {
                            return locationFromSource(
                                std::get<Fortran::parser::Statement<Fortran::parser::EndChangeTeamStmt> >(c.value().t).
                                source,
                                end);
                        }
                        return locationFromSource(
                            std::get<Fortran::parser::Statement<Fortran::parser::ChangeTeamStmt> >(c.value().t).source,
                            end);
                    },
                    [&](const Fortran::common::Indirection<Fortran::parser::CaseConstruct> &c) ->
                Fortran::parser::SourcePosition {
                        if (end) {
                            return locationFromSource(
                                std::get<Fortran::parser::Statement<Fortran::parser::EndSelectStmt> >(c.value().t).
                                source, end);
                        }
                        return locationFromSource(
                            std::get<Fortran::parser::Statement<Fortran::parser::SelectCaseStmt> >(c.value().t).source,
                            end);
                    },
                    [&](const Fortran::common::Indirection<Fortran::parser::BlockConstruct> &c) ->
                Fortran::parser::SourcePosition {
                        if (end) {
                            return locationFromSource(
                                std::get<Fortran::parser::Statement<Fortran::parser::EndBlockStmt> >(c.value().t).
                                source,
                                end);
                        }
                        return locationFromSource(
                            std::get<Fortran::parser::Statement<Fortran::parser::BlockStmt> >(c.value().t).source, end);
                    },
                    [&](const Fortran::common::Indirection<Fortran::parser::AssociateConstruct> &c) ->
                Fortran::parser::SourcePosition {
                        if (end) {
                            return locationFromSource(
                                std::get<Fortran::parser::Statement<Fortran::parser::EndAssociateStmt> >(c.value().t).
                                source, end);
                        }
                        return locationFromSource(
                            std::get<Fortran::parser::Statement<Fortran::parser::AssociateStmt> >(c.value().t).
                            source, end);
                    }
                }, construct.u);
        }

        [[nodiscard]] Fortran::parser::SourcePosition getLocation(
            const Fortran::parser::ExecutionPartConstruct &construct,
            const bool end) {
            /* Possibilities for ExecutionPartConstruct:
             *   ExecutableConstruct
             *   Statement<common::Indirection<FormatStmt>>
             *   Statement<common::Indirection<EntryStmt>>
             *   Statement<common::Indirection<DataStmt>>
             *   Statement<common::Indirection<NamelistStmt>>
             *   ErrorRecovery
             */
            return std::visit(
                Fortran::common::visitors{
                    [&](const Fortran::parser::ExecutableConstruct &c) -> Fortran::parser::SourcePosition {
                        return getLocation(c, end);
                    },
                    [&](const auto &c) -> Fortran::parser::SourcePosition {
                        return locationFromSource(c.source, end);
                    },
                    [&](const Fortran::parser::ErrorRecovery &) -> Fortran::parser::SourcePosition {
                        DIE("Should not encounter ErrorRecovery in parse tree");
                    }
                }, construct.u);
        }

        bool Pre(const Fortran::parser::ExecutionPart &executionPart) {
            if (const Fortran::parser::Block &block = executionPart.v; block.empty()) {
                llvm::outs() << "WARNING: Execution part empty.\n";
            } else {
                llvm::outs() << "ExecutionPart num blocks: " << block.size() << "\n";
                const Fortran::parser::SourcePosition startLoc{getLocation(block.front(), false)};
                const Fortran::parser::SourcePosition endLoc{getLocation(block.back(), true)};
                if (isInMainProgram_) {
                    llvm::outs() << "Program begin \"" << mainProgramName_ << "\" at " << startLoc.line << ", " <<
                            startLoc.column << "\n";
                    addInstrumentationPoint(SaltInstrumentationPointType::PROGRAM_BEGIN, startLoc.line,
                                            mainProgramName_);
                } else {
                    llvm::outs() << "Subprogram begin \"" << subprogramName_ << "\" at " << startLoc.line << ", " <<
                            startLoc.column << "\n";
                    addInstrumentationPoint(SaltInstrumentationPointType::PROCEDURE_BEGIN, startLoc.line,
                                            subprogramName_);
                }
                llvm::outs() << "End at " << endLoc.line << ", " << endLoc.column << "\n";
                addInstrumentationPoint(SaltInstrumentationPointType::PROCEDURE_END, endLoc.line);
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
        Fortran::parser::Parsing *parsing{nullptr};
    }; // SaltInstrumentParseTreeVisitor

    /**
     * Get the source file represented by a given parse tree
     *
     * See function BuildRuntimeDerivedTypeTables() in
     * flang/lib/Semantics/runtime-type-info.cpp for example
     * of getting the source file name.
     */
    [[nodiscard]] static std::optional<std::string> getInputFilePath(Fortran::parser::Parsing &parsing) {
        const auto &allSources{parsing.allCooked().allSources()};
        if (const auto firstProv{allSources.GetFirstFileProvenance()}) {
            if (const auto *srcFile{allSources.GetSourceFile(firstProv->start())}) {
                return srcFile->path();
            }
        }
        return std::nullopt;
    }



    [[nodiscard]] static std::string getInstrumentationPointString(const SaltInstrumentationPointType type) {
        switch (type) {
            case SaltInstrumentationPointType::PROCEDURE_BEGIN:
                return "! PROCEDURE BEGIN";
            case SaltInstrumentationPointType::PROGRAM_BEGIN:
                return "! PROGRAM BEGIN";
            case SaltInstrumentationPointType::PROCEDURE_END:
                return "! PROCEDURE END";
            default:
                CRASH_NO_CASE;
        }
    }

    static void instrumentFile(const std::string &inputFilePath, llvm::raw_pwrite_stream &outputStream,
                               const SaltInstrumentParseTreeVisitor &visitor) {
        std::ifstream inputStream{inputFilePath};
        if (!inputStream) {
            llvm::errs() << "ERROR: Could not open input file" << inputFilePath << "\n";
            std::exit(-2);
        }
        std::string line;
        int lineNum{0};
        const auto &instPts{visitor.getInstrumentationPoints()};
        auto instIter{instPts.cbegin()};
        while (std::getline(inputStream, line)) {
            ++lineNum;
            if (instIter != instPts.cend() && instIter->startLine == lineNum && instIter->instrumentBefore()) {
                outputStream << getInstrumentationPointString(instIter->instrumentationPointType) << "\n";
                ++instIter;
            }
            outputStream << line << "\n";
            if (instIter != instPts.cend() && instIter->startLine == lineNum && !instIter->instrumentBefore()) {
                outputStream << getInstrumentationPointString(instIter->instrumentationPointType) << "\n";
                ++instIter;
            }
        }
    }

    [[nodiscard]] static std::string getConfigPath() {
        if (const char *val = getenv(SALT_FORTRAN_CONFIG_FILE_VAR)) {
            return std::string{val};
        }
        return SALT_FORTRAN_CONFIG_DEFAULT_PATH;
    }

    [[nodiscard]] static ryml::Tree getConfigYamlTree(const std::string &configPath) {
        std::ifstream inputStream{configPath};
        if (!inputStream) {
            llvm::errs() << "ERROR: Could not open configuration file " << configPath << "\n"
                    << "Set " SALT_FORTRAN_CONFIG_FILE_VAR " to path to desired configuration file.\n";
            std::exit(-3);
        }
        std::stringstream configStream;
        configStream << inputStream.rdbuf();
        // TODO handle errors if config yaml doesn't parse
        return ryml::parse_in_arena(ryml::to_csubstr(configStream.str()));
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
            llvm::errs() << "ERROR: Unable to find input file name!\n";
            std::exit(-1);
        }
        llvm::outs() << "Have input file: " << *inputFilePath << "\n";

        const std::string configPath{getConfigPath()};
        ryml::Tree yamlTree = getConfigYamlTree(configPath);
        //TODO call read yaml func


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
        const auto outputFileStream = createOutputFile(outputFileExtension);

        // Walk the parse tree -- marks nodes for instrumentation
        SaltInstrumentParseTreeVisitor visitor{&parsing};
        Walk(parsing.parseTree(), visitor);

        // Use the instrumentation points stored in the Visitor to write the instrumented file.
        instrumentFile(*inputFilePath, *outputFileStream, visitor);

        outputFileStream->flush();

        llvm::outs() << "==== SALT Instrumentor Plugin finished ====\n";
    }
};

[[maybe_unused]] static FrontendPluginRegistry::Add<SaltInstrumentAction> X(
    "salt-instrument", "Apply SALT Instrumentation");
