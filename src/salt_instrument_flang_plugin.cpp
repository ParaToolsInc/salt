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
#include <regex>


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
// TODO Put debug output behind verbose flag

#define SALT_FORTRAN_CONFIG_FILE_VAR "SALT_FORTRAN_CONFIG_FILE"
#define SALT_FORTRAN_CONFIG_DEFAULT_PATH "config_files/tau_config.yaml"

#define SALT_FORTRAN_KEY "Fortran"
#define SALT_FORTRAN_PROGRAM_BEGIN_KEY "program_insert"
#define SALT_FORTRAN_PROCEDURE_BEGIN_KEY "procedure_begin_insert"
#define SALT_FORTRAN_PROCEDURE_END_KEY "procedure_end_insert"

#define SALT_FORTRAN_TIMER_NAME_TEMPLATE R"(\$\{full_timer_name\})"
#define SALT_FORTRAN_STRING_SPLITTER "&\n     &"
#define SALT_F77_LINE_LENGTH 64

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

    typedef std::map<SaltInstrumentationPointType, const std::string> InstrumentationMap;

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
            : mainProgramLine_(0), subProgramLine_(0), parsing(parsing) {
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
            mainProgramLine_ = parsing->allCooked().GetSourcePositionRange(program.v.source)->first.line;
            llvm::outs() << "Enter main program: " << mainProgramName_ << "\n";
        }

        bool Pre(const Fortran::parser::SubroutineStmt &subroutineStmt) {
            const auto & name =std::get<Fortran::parser::Name>(subroutineStmt.t);
            subprogramName_ = name.ToString();
            subProgramLine_ = parsing->allCooked().GetSourcePositionRange(name.source)->first.line;
            llvm::outs() << "Enter Subroutine: " << subprogramName_ << "\n";
            return true;
        }

        void Post(const Fortran::parser::SubroutineSubprogram &) {
            llvm::outs() << "Exit Subroutine: " << subprogramName_ << "\n";
            subprogramName_.clear();
        }

        bool Pre(const Fortran::parser::FunctionStmt &functionStmt) {
            const auto &name = std::get<Fortran::parser::Name>(functionStmt.t);
            subprogramName_ = name.ToString();
            subProgramLine_ = parsing->allCooked().GetSourcePositionRange(name.source)->first.line;
            llvm::outs() << "Enter Function: " << subprogramName_ << "\n";
            return true;
        }

        void Post(const Fortran::parser::FunctionSubprogram &) {
            llvm::outs() << "Exit Function: " << subprogramName_ << "\n";
            subprogramName_.clear();
            subProgramLine_ = 0;
        }

        // TODO split location-getting routines into a separate file

        // TODO The source position functions can fail if no source position exists
        //      Need to handle that case better.

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
                // TODO this assumes that the program end statement ends the next line after
                //      the last statement, but there could be whitespace/comments. Need to actually
                //      find the end statement. End statement may not have source position if name
                //      not listed -- need to find workaround.
                std::stringstream ss;
                ss << (isInMainProgram_ ? mainProgramName_ : subprogramName_);
                ss << " [{" << startLoc.sourceFile->path() << "} {";
                ss << (isInMainProgram_ ? mainProgramLine_ : subProgramLine_);
                ss << ",1}-{"; // TODO column number, first char of program/subroutine/function stmt
                ss << endLoc.line + 1;
                ss << ",1}]";  // TODO column number, last char of end stmt

                const std::string timerName{ss.str()};

                // Split the timername string so that it will fit between Fortran 77's 72 character limit,
                // and use character string line continuation syntax compatible with Fortran 77 and modern
                // Fortran.
                std::stringstream ss2;
                for (size_t i = 0; i < timerName.size(); i += SALT_F77_LINE_LENGTH) {
                    ss2 << SALT_FORTRAN_STRING_SPLITTER;
                    ss2 << timerName.substr(i, SALT_F77_LINE_LENGTH);
                }

                const std::string splitTimerName{ss2.str()};

                if (isInMainProgram_) {
                    llvm::outs() << "Program begin \"" << mainProgramName_ << "\" at " << startLoc.line << ", " <<
                            startLoc.column << "\n";
                    addInstrumentationPoint(SaltInstrumentationPointType::PROGRAM_BEGIN, startLoc.line,
                                            splitTimerName);
                } else {
                    llvm::outs() << "Subprogram begin \"" << subprogramName_ << "\" at " << startLoc.line << ", " <<
                            startLoc.column << "\n";
                    addInstrumentationPoint(SaltInstrumentationPointType::PROCEDURE_BEGIN, startLoc.line,
                                            splitTimerName);
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
        int mainProgramLine_;
        std::string subprogramName_;
        int subProgramLine_;

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


    [[nodiscard]] static std::string getInstrumentationPointString(const SaltInstrumentationPoint & instPt,
                                                                   const InstrumentationMap &instMap) {
        std::string instTemplate = instMap.at(instPt.instrumentationPointType);
        if (instPt.timerName.has_value()) {
            instTemplate = std::regex_replace(instTemplate, std::regex(SALT_FORTRAN_TIMER_NAME_TEMPLATE),
                                              instPt.timerName.value());
        }
        return instTemplate;
    }

    static void instrumentFile(const std::string &inputFilePath, llvm::raw_pwrite_stream &outputStream,
                               const SaltInstrumentParseTreeVisitor &visitor,
                               const InstrumentationMap & instMap) {
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
                outputStream << getInstrumentationPointString(*instIter, instMap) << "\n";
                ++instIter;
            }
            outputStream << line << "\n";
            if (instIter != instPts.cend() && instIter->startLine == lineNum && !instIter->instrumentBefore()) {
                outputStream << getInstrumentationPointString(*instIter, instMap) << "\n";
                ++instIter;
            }
        }
    }

    [[nodiscard]] static std::string getConfigPath() {
        // If config path env var is set and non-empty, use that;
        // otherwise use default.
        if (const char *val = getenv(SALT_FORTRAN_CONFIG_FILE_VAR)) {
            if (const std::string configPath{val}; !configPath.empty()) {
                return configPath;
            }
        }
        return SALT_FORTRAN_CONFIG_DEFAULT_PATH;
    }

    [[nodiscard]] static ryml::Tree getConfigYamlTree(const std::string &configPath) {
        std::ifstream inputStream{configPath};
        if (!inputStream) {
            llvm::errs() << "ERROR: Could not open configuration file " << configPath << "\n"
                    << "Set $" SALT_FORTRAN_CONFIG_FILE_VAR " to path to desired configuration file.\n";
            std::exit(-3);
        }
        std::stringstream configStream;
        configStream << inputStream.rdbuf();
        // TODO handle errors if config yaml doesn't parse
        return ryml::parse_in_arena(ryml::to_csubstr(configStream.str()));
    }

    [[nodiscard]] static InstrumentationMap getInstrumentationMap(const ryml::Tree &tree) {
        InstrumentationMap map;
        std::stringstream ss;

        // Access the "Fortran" node
        ryml::NodeRef fortranNode = tree[SALT_FORTRAN_KEY];

        // Validate that the "Fortran" node exists
        if (!fortranNode.valid()) {
            llvm::errs() << "ERROR: '" << SALT_FORTRAN_KEY << "' key not found in the configuration file.\n";
            std::exit(-3);
        }

        // Access and process the "program_begin_insert" node
        ryml::NodeRef programBeginNode = fortranNode[SALT_FORTRAN_PROGRAM_BEGIN_KEY];
        if (!programBeginNode.valid()) {
            llvm::errs() << "ERROR: '" << SALT_FORTRAN_PROGRAM_BEGIN_KEY << "' key not found under 'Fortran'.\n";
            std::exit(-3);
        }
        for (const ryml::NodeRef child : programBeginNode.children()) {
            ss << child.val() << "\n";
        }
        map.emplace(SaltInstrumentationPointType::PROGRAM_BEGIN, ss.str());
        ss.str(""s);

        // Access and process the "procedure_begin_insert" node
        ryml::NodeRef procedureBeginNode = fortranNode[SALT_FORTRAN_PROCEDURE_BEGIN_KEY];
        if (!procedureBeginNode.valid()) {
            llvm::errs() << "ERROR: '" << SALT_FORTRAN_PROCEDURE_BEGIN_KEY << "' key not found under 'Fortran'.\n";
            std::exit(-3);
        }
        for (const ryml::NodeRef child : procedureBeginNode.children()) {
            ss << child.val() << "\n";
        }
        map.emplace(SaltInstrumentationPointType::PROCEDURE_BEGIN, ss.str());
        ss.str(""s);

        // Access and process the "procedure_end_insert" node
        ryml::NodeRef procedureEndNode = fortranNode[SALT_FORTRAN_PROCEDURE_END_KEY];
        if (!procedureEndNode.valid()) {
            llvm::errs() << "ERROR: '" << SALT_FORTRAN_PROCEDURE_END_KEY << "' key not found under 'Fortran'.\n";
            std::exit(-3);
        }
        for (const ryml::NodeRef child : procedureEndNode.children()) {
            ss << child.val() << "\n";
        }
        map.emplace(SaltInstrumentationPointType::PROCEDURE_END, ss.str());

        return map;
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

        // Read and parse the yaml configuration file
        const std::string configPath{getConfigPath()};
        const ryml::Tree yamlTree = getConfigYamlTree(configPath);
        const InstrumentationMap instMap = getInstrumentationMap(yamlTree);

        // Get the extension of the input file
        // For input file 'filename.ext' we will output to 'filename.inst.Ext'
        // Since we are adding preprocessor directives in the emitted code,
        // the file extension should be capitalized.
        std::string inputFileExtension;
        if (auto const extPos = inputFilePath->find_last_of('.'); extPos == std::string::npos) {
            inputFileExtension = "F90"; // Default if for some reason file has no extension
        } else {
            inputFileExtension = inputFilePath->substr(extPos + 1); // Part of string past last '.'
                // Capitalize the first character of inputFileExtension
            if (!inputFileExtension.empty()) {
                inputFileExtension[0] = static_cast<char>(std::toupper(inputFileExtension[0]));
            }
        }

        // Open an output file for writing the instrumented code
        const std::string outputFileExtension = "inst."s + inputFileExtension;
        const auto outputFileStream = createOutputFile(outputFileExtension);

        // Walk the parse tree -- marks nodes for instrumentation
        SaltInstrumentParseTreeVisitor visitor{&parsing};
        Walk(parsing.parseTree(), visitor);

        // Use the instrumentation points stored in the Visitor to write the instrumented file.
        instrumentFile(*inputFilePath, *outputFileStream, visitor, instMap);

        outputFileStream->flush();

        llvm::outs() << "==== SALT Instrumentor Plugin finished ====\n";
    }
};

[[maybe_unused]] static FrontendPluginRegistry::Add<SaltInstrumentAction> X(
    "salt-instrument", "Apply SALT Instrumentation");
