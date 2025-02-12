/*
Copyright (C) 2024-2025, ParaTools, Inc.

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
#include <algorithm>
#include <filesystem>


#define RYML_SINGLE_HDR_DEFINE_NOW
#define RYML_SHARED

#include <dprint.hpp>
#include <ryml_all.hpp>

#include <clang/Basic/SourceLocation.h>

#include "flang/Frontend/FrontendActions.h"
#include "flang/Frontend/FrontendPluginRegistry.h"
#include "flang/Parser/dump-parse-tree.h"
#include "flang/Parser/parsing.h"
#include "flang/Parser/source.h"
#include "flang/Common/indirection.h"

#include "flang_instrumentation_constants.hpp"
#include "selectfile.hpp"
#include "flang_source_location.hpp"
#include "flang_instrumentation_point.hpp"

// TODO Put debug output behind verbose flag

using namespace std::string_literals;
using namespace Fortran::frontend;

/**
 * The main action of the Salt instrumentor.
 * Visits each node in the parse tree.
 */
namespace salt::fortran {
    class SaltInstrumentAction final : public PluginParseTreeAction {
        struct SaltInstrumentParseTreeVisitor {
            explicit SaltInstrumentParseTreeVisitor(Fortran::parser::Parsing *parsing,
                                                    const bool skipInstrument = false)
                : mainProgramLine_(0), subProgramLine_(0), skipInstrumentFile_(skipInstrument), parsing(parsing) {
            }

            bool shouldInstrument() const {
                return !skipInstrumentFile_ && !skipInstrumentSubprogram_;
            }

            void addProgramBeginInstrumentation(const int start_line, const std::string &timer_name) {
                if (shouldInstrument()) {
                    instrumentationPoints_.emplace_back(
                        std::make_unique<ProgramBeginInstrumentationPoint>(start_line, timer_name));
                }
            }

            void addProcedureBeginInstrumentation(const int start_line, const std::string &timer_name) {
                if (shouldInstrument()) {
                    instrumentationPoints_.emplace_back(
                        std::make_unique<ProcedureBeginInstrumentationPoint>(start_line, timer_name));
                }
            }

            void addProcedureEndInstrumentation(const int end_line, const std::string &timer_name) {
                if (shouldInstrument()) {
                    instrumentationPoints_.emplace_back(
                        std::make_unique<ProcedureEndInstrumentationPoint>(end_line, timer_name));
                }
            }

            void addReturnStmtInstrumentation(const int end_line) {
                if (shouldInstrument()) {
                    instrumentationPoints_.emplace_back(std::make_unique<ReturnStmtInstrumentationPoint>(end_line));
                }
            }

            void addIfReturnStmtInstrumentation(const int end_line, const int conditional_column) {
                if (shouldInstrument()) {
                    instrumentationPoints_.emplace_back(
                        std::make_unique<IfReturnStmtInstrumentationPoint>(end_line, conditional_column));
                }
            }

            [[nodiscard]] const auto &getInstrumentationPoints() const {
                return instrumentationPoints_;
            }

            [[nodiscard]] std::string dumpInstrumentationPoints() const {
                std::stringstream ss;
                for (const auto &instPt: getInstrumentationPoints()) {
                    ss << instPt->toString() << "\n";
                }
                return ss.str();
            }

            [[nodiscard]] static std::string convertWildcardToRegexForm(const std::string &wildString) {
                // Escape all regex special characters
                static const std::regex metacharacters(R"([\.\^\$\+\(\)\[\]\{\}\|\?\*])");
                const std::string escapedString{std::regex_replace(wildString, metacharacters, R"(\$&)")};
                // Convert lines in TAU select file format (where "#" means zero or more characters)
                // to regex version (where ".*" means zero or more characters).
                // "#" is used for wildcard in routine names in TAU selective instrumentation files
                // because "*" can be used in C/C++ function identifiers as part of pointer types.
                static const std::regex hashRegex{R"(#)"};
                return std::regex_replace(escapedString, hashRegex, ".*");
            }

            [[nodiscard]] static bool shouldInstrumentSubprogram(const std::string &subprogramName) {
                // Check if this subprogram should be instrumented.
                // It should if:
                //   - No include or exclude list is specified
                //   - An exclude list is present and the subprogram is not in it
                //   - An include list is present and the subprogram is in it (and not on the exclude list)

                if (includelist.empty() && excludelist.empty()) {
                    return true;
                }

                for (const auto &excludeEntry: excludelist) {
                    if (const std::regex excludeRegex{convertWildcardToRegexForm(excludeEntry)}; std::regex_search(
                        subprogramName, excludeRegex)) {
                        return false;
                    }
                }

                bool subprogramInIncludeList{false};
                for (const auto &includeEntry: includelist) {
                    if (const std::regex includeRegex{convertWildcardToRegexForm(includeEntry)}; std::regex_search(
                        subprogramName, includeRegex)) {
                        subprogramInIncludeList = true;
                        break;
                    }
                }

                if (!includelist.empty()) {
                    if (subprogramInIncludeList) {
                        return true;
                    }
                    return false;
                }

                return true;
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

            // Never descend into InterfaceSpecification nodes, they can't contain executable statements.
            static bool Pre(const Fortran::parser::InterfaceSpecification &) { return false; }

            bool Pre(const Fortran::parser::MainProgram &) {
                isInMainProgram_ = true;
                return true;
            }

            void Post(const Fortran::parser::MainProgram &) {
                verboseStream() << "Exit main program: " << mainProgramName_ << "\n";
                isInMainProgram_ = false;
            }

            void Post(const Fortran::parser::ProgramStmt &program) {
                mainProgramName_ = program.v.ToString();
                mainProgramLine_ = parsing->allCooked().GetSourcePositionRange(program.v.source)->first.line;
                verboseStream() << "Enter main program: " << mainProgramName_ << "\n";
            }

            bool Pre(const Fortran::parser::SubroutineStmt &subroutineStmt) {
                isInMainProgram_ = false;
                const auto &name = std::get<Fortran::parser::Name>(subroutineStmt.t);
                subprogramName_ = name.ToString();
                subProgramLine_ = parsing->allCooked().GetSourcePositionRange(name.source)->first.line;
                verboseStream() << "Enter Subroutine: " << subprogramName_ << "\n";
                if (!shouldInstrumentSubprogram(subprogramName_)) {
                    verboseStream() << "Skipping instrumentation of " << subprogramName_ <<
                            " due to selective instrumentation\n";
                    skipInstrumentSubprogram_ = true;
                }
                return true;
            }

            void Post(const Fortran::parser::SubroutineSubprogram &) {
                verboseStream() << "Exit Subroutine: " << subprogramName_ << "\n";
                skipInstrumentSubprogram_ = false;
                subprogramName_.clear();
            }

            bool Pre(const Fortran::parser::FunctionStmt &functionStmt) {
                isInMainProgram_ = false;
                const auto &name = std::get<Fortran::parser::Name>(functionStmt.t);
                subprogramName_ = name.ToString();
                subProgramLine_ = parsing->allCooked().GetSourcePositionRange(name.source)->first.line;
                verboseStream() << "Enter Function: " << subprogramName_ << "\n";
                if (!shouldInstrumentSubprogram(subprogramName_)) {
                    verboseStream() << "Skipping instrumentation of " << subprogramName_ <<
                            " due to selective instrumentation\n";
                    skipInstrumentSubprogram_ = true;
                }
                return true;
            }

            void Post(const Fortran::parser::FunctionSubprogram &) {
                verboseStream() << "Exit Function: " << subprogramName_ << "\n";
                skipInstrumentSubprogram_ = false;
                subprogramName_.clear();
                subProgramLine_ = 0;
            }

            // Split handling of ExecutionPart into two phases
            // so that we insert Instrumentation Points in order
            // even if we separately insert them in visitors for
            // children of ExecutionPart.
            bool Pre(const Fortran::parser::ExecutionPart &executionPart) {
                handleExecutionPart(executionPart, true);
                return true;
            }

            void Post(const Fortran::parser::ExecutionPart &executionPart) {
                handleExecutionPart(executionPart, false);
            }

            void handleExecutionPart(const Fortran::parser::ExecutionPart &executionPart, bool pre) {
                if (const Fortran::parser::Block &block = executionPart.v; block.empty()) {
                    verboseStream() << "WARNING: Execution part empty.\n";
                } else {
                    const std::optional startLocOpt{getLocation(parsing, block.front(), false)};
                    const std::optional endLocOpt{getLocation(parsing, block.back(), true)};

                    if (!startLocOpt.has_value()) {
                        llvm::errs() << "ERROR: execution part had no start source location!\n";
                    }
                    if (!endLocOpt.has_value()) {
                        llvm::errs() << "ERROR: execution part had no end source location!\n";
                    }

                    const auto &startLoc{startLocOpt.value()};
                    const auto &endLoc{endLocOpt.value()};
                    std::stringstream ss;
                    ss << (isInMainProgram_ ? mainProgramName_ : subprogramName_);
                    ss << " [{" << startLoc.sourceFile->path() << "} {";
                    ss << (isInMainProgram_ ? mainProgramLine_ : subProgramLine_);
                    ss << ",1}-{"; // TODO column number, first char of program/subroutine/function stmt
                    ss << endLoc.line + 1;
                    ss << ",1}]"; // TODO column number, last char of end stmt

                    const std::string timerName{ss.str()};

                    // Split the timerName string so that it will fit between Fortran 77's 72-character limit,
                    // and use character string line continuation syntax compatible with Fortran 77 and modern
                    // Fortran.
                    std::stringstream ss2;
                    for (size_t i = 0; i < timerName.size(); i += SALT_F77_LINE_LENGTH) {
                        ss2 << SALT_FORTRAN_STRING_SPLITTER;
                        ss2 << timerName.substr(i, SALT_F77_LINE_LENGTH);
                    }

                    const std::string splitTimerName{ss2.str()};
                    // Insert the timer start in the Pre phase (when we first visit the node)
                    // and the timer stop in the Post phase (when we return after visiting the node's children).
                    if (pre) {
                        // TODO this assumes that the program end statement ends the next line after
                        //      the last statement, but there could be whitespace/comments. Need to actually
                        //      find the end statement. End statement may not have source position if name
                        //      not listed -- need to find workaround.


                        if (isInMainProgram_) {
                            verboseStream() << "Program begin \"" << mainProgramName_ << "\" at " << startLoc.line <<
                                    ", "
                                    <<
                                    startLoc.column << "\n";
                            addProgramBeginInstrumentation(startLoc.line, splitTimerName);
                        } else {
                            verboseStream() << "Subprogram begin \"" << subprogramName_ << "\" at " << startLoc.line <<
                                    ", " <<
                                    startLoc.column << "\n";
                            addProcedureBeginInstrumentation(startLoc.line, splitTimerName);
                        }
                    } else {
                        verboseStream() << "End at " << endLoc.line << ", " << endLoc.column << "\n";
                        addProcedureEndInstrumentation(endLoc.line, splitTimerName);
                    }
                }
            }

            // A ReturnStmt does not have a source, so we instead need to get access to the wrapper Statement that does.
            // Here we get the ReturnStmt through ExecutableConstruct -> Statement<ActionStmt> -> Indirection<ReturnStmt>
            bool Pre(const Fortran::parser::ExecutableConstruct &execConstruct) {
                if (const auto actionStmt = std::get_if<Fortran::parser::Statement<Fortran::parser::ActionStmt> >(
                    &execConstruct.u)) {
                    if (std::holds_alternative<Fortran::common::Indirection<Fortran::parser::ReturnStmt> >(
                        actionStmt->statement.u)) {
                        const std::optional returnPos{locationFromSource(parsing, actionStmt->source, false)};
                        const int returnLine{returnPos.value().line};
                        verboseStream() << "Return statement at " << returnLine << "\n";
                        addReturnStmtInstrumentation(returnLine);
                    }
                }
                return true;
            }

            bool Pre(const Fortran::parser::IfStmt &ifStmt) {
                if (const auto &ifAction{
                        std::get<Fortran::parser::UnlabeledStatement<Fortran::parser::ActionStmt> >(ifStmt.t)
                    };
                    std::holds_alternative<Fortran::common::Indirection<
                        Fortran::parser::ReturnStmt> >(ifAction.statement.u)) {
                    const auto startPos{
                        locationFromSource(parsing,
                                           std::get<Fortran::parser::ScalarLogicalExpr>(ifStmt.t).thing.thing.value().
                                           source,
                                           false).value()
                    };
                    const auto endPos{
                        locationFromSource(parsing,
                                           std::get<Fortran::parser::ScalarLogicalExpr>(ifStmt.t).thing.thing.value().
                                           source,
                                           true).value()
                    };
                    verboseStream() << "If-return, conditional: (" << startPos.line << "," << startPos.column << ") - "
                            << "(" << endPos.line << "," << endPos.column << ")\n";
                    // TODO handle multi-line
                    // TODO handle line continuation if too long
                    addIfReturnStmtInstrumentation(startPos.line, endPos.column);
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

            bool skipInstrumentFile_;
            bool skipInstrumentSubprogram_{false};

            std::vector<std::unique_ptr<const InstrumentationPoint> > instrumentationPoints_;

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

        static std::string lineDirective(const int line, const std::string &file) {
            return "#line " + std::to_string(line) + " \"" + file + "\"";
        }

        static void instrumentFile(const std::string &inputFilePath, llvm::raw_pwrite_stream &outputStream,
                                   const SaltInstrumentParseTreeVisitor &visitor,
                                   const InstrumentationMap &instMap) {
            std::ifstream inputStream{inputFilePath};
            if (!inputStream) {
                llvm::errs() << "ERROR: Could not open input file" << inputFilePath << "\n";
                std::exit(-2);
            }
            std::string lineText;
            int lineNum{0};
            const auto &instPts{visitor.getInstrumentationPoints()};

            verboseStream() << "Will perform instrumentation:\n" << visitor.dumpInstrumentationPoints();

            // Sanity check: are instrumentation points in the right order?
            if (!std::is_sorted(instPts.cbegin(), instPts.cend(), [&](const auto &p1, const auto &p2) {
                return *p1 < *p2;
            })) {
                DIE("ERROR: Instrumentation points not sorted by line number!\n");
            }

            outputStream << lineDirective(1, inputFilePath) << "\n";

            auto instIter{instPts.cbegin()};
            while (std::getline(inputStream, lineText)) {
                bool lineWasInstrumentedBefore{false};
                bool lineWasInstrumentedAfter{false};
                bool shouldOutputLine{true};

                ++lineNum;

                // First, process instrumentation points that come BEFORE this line.
                while (instIter != instPts.cend() && (*instIter)->line() == lineNum && (*instIter)->location() ==
                       InstrumentationLocation::BEFORE) {
                    outputStream << (*instIter)->instrumentationString(instMap, lineText) << "\n";
                    lineWasInstrumentedBefore = true;
                    ++instIter;
                }

                // Then, process instrumentation points that REPLACE this line.
                while (instIter != instPts.cend() && (*instIter)->line() == lineNum && (*instIter)->location() ==
                       InstrumentationLocation::REPLACE) {
                    outputStream << lineDirective(lineNum, inputFilePath) << "\n";
                    outputStream << (*instIter)->instrumentationString(instMap, lineText) << "\n";
                    lineWasInstrumentedAfter = true;
                    shouldOutputLine = false;
                    ++instIter;
                }

                // If line was instrumented, output a #line directive
                if (lineWasInstrumentedBefore) {
                    outputStream << lineDirective(lineNum, inputFilePath) << "\n";
                }

                // Output the current line, if not replaced.
                if (shouldOutputLine) {
                    outputStream << lineText << "\n";
                }

                // Finally, process instrumentation points that come AFTER this line.
                while (instIter != instPts.cend() && (*instIter)->line() == lineNum && (*instIter)->location() ==
                       InstrumentationLocation::AFTER) {
                    outputStream << (*instIter)->instrumentationString(instMap, lineText) << "\n";
                    lineWasInstrumentedAfter = true;
                    ++instIter;
                }

                // If line was instrumented, output a #line directive
                if (lineWasInstrumentedAfter) {
                    outputStream << lineDirective(lineNum+1, inputFilePath) << "\n";
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

        [[nodiscard]] static std::optional<std::string> getSelectFilePath() {
            if (const char *val = getenv(SALT_FORTRAN_SELECT_FILE_VAR)) {
                if (std::string selectFile{val}; !selectFile.empty()) {
                    return selectFile;
                }
            }
            return std::nullopt;
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
            ryml::ConstNodeRef fortranNode = tree[SALT_FORTRAN_KEY];

            // Validate that the "Fortran" node exists
            if (fortranNode.invalid()) {
                llvm::errs() << "ERROR: '" << SALT_FORTRAN_KEY << "' key not found in the configuration file.\n";
                std::exit(-3);
            }

            // Access and process the "program_begin_insert" node
            ryml::ConstNodeRef programBeginNode = fortranNode[SALT_FORTRAN_PROGRAM_BEGIN_KEY];
            if (programBeginNode.invalid()) {
                llvm::errs() << "ERROR: '" << SALT_FORTRAN_PROGRAM_BEGIN_KEY << "' key not found under 'Fortran'.\n";
                std::exit(-3);
            }
            for (const ryml::ConstNodeRef child: programBeginNode.children()) {
                ss << child.val() << "\n";
            }
            map.emplace(InstrumentationPointType::PROGRAM_BEGIN, ss.str());
            ss.str(""s);

            // Access and process the "procedure_begin_insert" node
            ryml::ConstNodeRef procedureBeginNode = fortranNode[SALT_FORTRAN_PROCEDURE_BEGIN_KEY];
            if (procedureBeginNode.invalid()) {
                llvm::errs() << "ERROR: '" << SALT_FORTRAN_PROCEDURE_BEGIN_KEY << "' key not found under 'Fortran'.\n";
                std::exit(-3);
            }
            for (const ryml::ConstNodeRef child: procedureBeginNode.children()) {
                ss << child.val() << "\n";
            }
            map.emplace(InstrumentationPointType::PROCEDURE_BEGIN, ss.str());
            ss.str(""s);

            // Access and process the "procedure_end_insert" node
            ryml::ConstNodeRef procedureEndNode = fortranNode[SALT_FORTRAN_PROCEDURE_END_KEY];
            if (procedureEndNode.invalid()) {
                llvm::errs() << "ERROR: '" << SALT_FORTRAN_PROCEDURE_END_KEY << "' key not found under 'Fortran'.\n";
                std::exit(-3);
            }
            for (const ryml::ConstNodeRef child: procedureEndNode.children()) {
                ss << child.val() << "\n";
            }
            map.emplace(InstrumentationPointType::PROCEDURE_END, ss.str());
            // The return statement uses the same text as procedure end,
            // but is inserted before the line instead of after.
            map.emplace(InstrumentationPointType::RETURN_STMT, ss.str());
            // The if-return statement uses the same text as procedure end,
            // but requires transformation to if-then-endif
            map.emplace(InstrumentationPointType::IF_RETURN, ss.str());

            return map;
        }

        [[nodiscard]] static std::string convertGlobToRegexForm(const std::string &globString) {
            // Convert lines in shell glob format (where "*" means zero or more characters)
            // to regex version (where ".*" means zero or more characters).
            // This is used for files in TAU selective instrumentation files.
            static std::regex starRegex{R"(\*)"};
            const std::string starString{std::regex_replace(globString, starRegex, ".*")};
            // Escape all special regex characters except for "*" which was previously handled.
            static const std::regex metacharacters(R"([\.\^\$\+\(\)\[\]\{\}\|\?])");
            return std::regex_replace(starString, metacharacters, R"(\$&)");
        }

        [[nodiscard]] static bool shouldInstrumentFile(const std::filesystem::path &filePath) {
            // Check if this file should be instrumented.
            // It should if:
            //   - No file include or file exclude list is specified
            //   - An exclude list is present and the file is not in it
            //   - An include list is present and the file is in it

            if (fileincludelist.empty() && fileexcludelist.empty()) {
                return true;
            }

            const auto filePart{filePath.filename()};
            for (const auto &excludeEntry: fileexcludelist) {
                if (const std::regex excludeRegex{convertGlobToRegexForm(excludeEntry)}; std::regex_search(
                    filePart.string(), excludeRegex)) {
                    return false;
                }
            }

            bool fileInIncludeList{false};
            for (const auto &includeEntry: fileincludelist) {
                if (const std::regex includeRegex{convertGlobToRegexForm(includeEntry)}; std::regex_search(
                    filePart.string(), includeRegex)) {
                    fileInIncludeList = true;
                    break;
                }
            }

            if (!fileincludelist.empty()) {
                if (fileInIncludeList) {
                    return true;
                }
                return false;
            }

            return true;
        }

        static void dumpSelectiveRequests() {
            const auto printStr = [&](const auto &a) { verboseStream() << a << "\n"; };
            verboseStream() << "File include list:\n";
            std::for_each(fileincludelist.cbegin(), fileincludelist.cend(), printStr);
            verboseStream() << "File exclude list:\n";
            std::for_each(fileexcludelist.cbegin(), fileexcludelist.cend(), printStr);
            verboseStream() << "Include list:\n";
            std::for_each(includelist.cbegin(), includelist.cend(), printStr);
            verboseStream() << "Exclude list:\n";
            std::for_each(excludelist.cbegin(), excludelist.cend(), printStr);
        }

        /**
         * This is the entry point for the plugin.
         */
        void executeAction() override {
            if (const char *val = getenv(SALT_FORTRAN_VERBOSE_VAR)) {
                if (const std::string verboseFlag{val}; !verboseFlag.empty() && verboseFlag != "0"s) {
                    enableVerbose();
                }
            }

            verboseStream() << "==== SALT Instrumentor Plugin starting ====\n";

            // This is the object through which we access the parse tree
            // and the source
            Fortran::parser::Parsing &parsing = getParsing();

            // Get the path to the input file
            const auto inputFilePathStr = getInputFilePath(parsing);
            if (!inputFilePathStr) {
                llvm::errs() << "ERROR: Unable to find input file name!\n";
                std::exit(-1);
            }

            verboseStream() << "Have input file: " << *inputFilePathStr << "\n";

            const std::filesystem::path inputFilePath{inputFilePathStr.value()};

            // Read and parse the yaml configuration file
            const std::string configPath{getConfigPath()};
            const ryml::Tree yamlTree = getConfigYamlTree(configPath);
            const InstrumentationMap instMap = getInstrumentationMap(yamlTree);

            if (const auto selectPath{getSelectFilePath()}; selectPath.has_value()) {
                if (processInstrumentationRequests(selectPath->c_str())) {
                    dumpSelectiveRequests();
                } else {
                    llvm::errs() << "ERROR: Unable to read selective instrumentation file at " << selectPath << "\n";
                    std::exit(-4);
                }
            }

            // Get the extension of the input file
            // For input file 'filename.ext' we will output to 'filename.inst.Ext'
            // Since we are adding preprocessor directives in the emitted code,
            // the file extension should be capitalized.
            std::string inputFileExtension;
            if (auto const extPos = inputFilePath.string().find_last_of('.'); extPos == std::string::npos) {
                inputFileExtension = "F90"; // Default if for some reason file has no extension
            } else {
                inputFileExtension = inputFilePath.string().substr(extPos + 1); // Part of string past last '.'
                // Capitalize the first character of inputFileExtension
                if (!inputFileExtension.empty()) {
                    inputFileExtension[0] = static_cast<char>(std::toupper(inputFileExtension[0]));
                }
            }

            // Open an output file for writing the instrumented code
            const std::string outputFileExtension = "inst."s + inputFileExtension;
            const auto outputFileStream = createOutputFile(outputFileExtension);

            // If visitor has skipInstrument set, no instrumentation points are added
            // so the file is output into the .inst file unchanged.
            bool skipInstrument{false};
            if (!shouldInstrumentFile(inputFilePath)) {
                verboseStream() << "Skipping instrumentation of " << inputFilePath
                        << " due to selective instrumentation.\n";
                skipInstrument = true;
            }
            // Walk the parse tree -- marks nodes for instrumentation
            SaltInstrumentParseTreeVisitor visitor{&parsing, skipInstrument};
            Walk(parsing.parseTree(), visitor);

            // Use the instrumentation points stored in the Visitor to write the instrumented file.
            instrumentFile(inputFilePath, *outputFileStream, visitor, instMap);

            outputFileStream->flush();

            verboseStream() << "==== SALT Instrumentor Plugin finished ====\n";
        }
    };
}

[[maybe_unused]] static FrontendPluginRegistry::Add<salt::fortran::SaltInstrumentAction> X(
    "salt-instrument", "Apply SALT Instrumentation");
