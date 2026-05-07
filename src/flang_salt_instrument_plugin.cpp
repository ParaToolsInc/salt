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

// On macOS, ryml_all.hpp transitively includes <sys/param.h>, which
// defines MAX(a,b) and MIN(a,b) as function-like macros.  Those collide
// with the MAX/MIN member functions of Fortran::evaluate::value::Integer
// (flang/Evaluate/integer.h, included via flang/Evaluate/tools.h),
// surfacing as "too few arguments provided to function-like macro
// invocation" on the Integer member declarations.  Undef before pulling
// in the flang headers below.
#ifdef MAX
#undef MAX
#endif
#ifdef MIN
#undef MIN
#endif

#include <clang/Basic/SourceLocation.h>

#include "flang/Frontend/FrontendActions.h"
#include "flang/Frontend/FrontendPluginRegistry.h"
#include "flang/Parser/dump-parse-tree.h"
#include "flang/Parser/parsing.h"
#include "flang/Parser/source.h"
#include "flang/Common/indirection.h"
#include "flang/Semantics/symbol.h"
#include "flang/Evaluate/tools.h"

#include "flang_instrumentation_constants.hpp"
#include "selectfile.hpp"
#include "flang_source_location.hpp"
#include "flang_instrumentation_point.hpp"

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

            void addProcedureEndInstrumentation(const int end_line) {
                if (shouldInstrument()) {
                    instrumentationPoints_.emplace_back(std::make_unique<ProcedureEndInstrumentationPoint>(end_line));
                }
            }

            void addReturnStmtInstrumentation(const int end_line) {
                if (shouldInstrument()) {
                    instrumentationPoints_.emplace_back(std::make_unique<ReturnStmtInstrumentationPoint>(end_line));
                }
            }

            void addIfReturnStmtInstrumentation(const int start_line, const int end_line,
                                                std::string condition_text,
                                                std::string return_expr_text,
                                                std::optional<long> label) {
                if (shouldInstrument()) {
                    instrumentationPoints_.emplace_back(
                        std::make_unique<IfReturnStmtInstrumentationPoint>(start_line, end_line,
                                                                           std::move(condition_text),
                                                                           std::move(return_expr_text),
                                                                           std::move(label)));
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
                    if (const std::regex excludeRegex{convertWildcardToRegexForm(excludeEntry)}; std::regex_match(
                        subprogramName, excludeRegex)) {
                        return false;
                    }
                }

                bool subprogramInIncludeList{false};
                for (const auto &includeEntry: includelist) {
                    if (const std::regex includeRegex{convertWildcardToRegexForm(includeEntry)}; std::regex_match(
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

            // Capture the line number that delimits the END of the
            // procedure's executable section, derived from the wrapping
            // subprogram/main-program tuple instead of from the last
            // ExecutionPartConstruct's source.  The last construct's source
            // can fail to map back through CookedSource for certain
            // statements (issue #31: labeled CONTINUE terminating a label-DO
            // at the end of the body, see llvm-project#196291), so we use
            // the wrapper's bookkeeping as the single source of truth.
            //
            // Also captures the column of the last character of the End
            // statement, used by the TAU timer-name source-range field
            // (PDT convention: inclusive last-char column).  Note: flang's
            // GetSourcePositionRange().second.column is one-past-last, so
            // we subtract 1.
            //
            // When an InternalSubprogramPart is present (a `contains`
            // section), the procedure's body ends just before the
            // ContainsStmt; otherwise it ends just before the End statement.
            template <typename SubprogramT, typename EndStmtT>
            void captureBodyEndLines(const SubprogramT &subprogram, int &endLineOut, int &endColOut,
                                     int &containsLineOut) {
                // Reset so a procedure that has no `contains` does not inherit
                // its parent's containsLine bookkeeping.  Without this, an
                // internal procedure walked from inside an outer procedure
                // that *does* have a `contains` would see a stale
                // containsLineOut and emit PROCEDURE_END at the outer's
                // boundary -- breaking the line-sorted instrumentation
                // invariant.
                endLineOut = 0;
                endColOut = 1;
                containsLineOut = 0;
                const auto &endStmt = std::get<Fortran::parser::Statement<EndStmtT> >(subprogram.t);
                if (const auto pos = locationFromSource(parsing, endStmt.source, false); pos.has_value()) {
                    endLineOut = pos.value().line;
                }
                if (const auto pos = locationFromSource(parsing, endStmt.source, true); pos.has_value()) {
                    endColOut = pos.value().column > 1 ? pos.value().column - 1 : 1;
                }
                const auto &maybeInternalPart =
                    std::get<std::optional<Fortran::parser::InternalSubprogramPart> >(subprogram.t);
                if (maybeInternalPart.has_value()) {
                    const auto &containsStmt =
                        std::get<Fortran::parser::Statement<Fortran::parser::ContainsStmt> >(
                            maybeInternalPart.value().t);
                    if (const auto pos = locationFromSource(parsing, containsStmt.source, false); pos.has_value()) {
                        containsLineOut = pos.value().line;
                    }
                }
            }

            // Capture the column where the subprogram's start statement
            // (subroutine/function/module-procedure keyword or its prefix)
            // begins.  Uses the wrapping `Statement<StartStmtT>::source`
            // from the parent tuple so we get the keyword position rather
            // than the procedure name's column.
            template <typename SubprogramT, typename StartStmtT>
            int captureStartCol(const SubprogramT &subprogram) {
                const auto &startStmt = std::get<Fortran::parser::Statement<StartStmtT> >(subprogram.t);
                if (const auto pos = locationFromSource(parsing, startStmt.source, false); pos.has_value()) {
                    return pos.value().column;
                }
                return 1;
            }

            // MainProgram special-case: ProgramStmt is optional (Fortran
            // permits an implicit main program with no `program` keyword).
            // Falls back to column 1 for the implicit form.
            int captureMainProgramStartCol(const Fortran::parser::MainProgram &mainProgram) {
                const auto &maybeProgStmt =
                    std::get<std::optional<Fortran::parser::Statement<Fortran::parser::ProgramStmt> > >(
                        mainProgram.t);
                if (maybeProgStmt.has_value()) {
                    if (const auto pos = locationFromSource(parsing, maybeProgStmt.value().source, false);
                        pos.has_value()) {
                        return pos.value().column;
                    }
                }
                return 1;
            }

            bool Pre(const Fortran::parser::MainProgram &mainProgram) {
                isInMainProgram_ = true;
                captureBodyEndLines<Fortran::parser::MainProgram, Fortran::parser::EndProgramStmt>(
                    mainProgram, mainProgramEndLine_, mainProgramEndCol_, mainProgramContainsLine_);
                mainProgramStartCol_ = captureMainProgramStartCol(mainProgram);
                return true;
            }

            bool Pre(const Fortran::parser::SubroutineSubprogram &subprogram) {
                captureBodyEndLines<Fortran::parser::SubroutineSubprogram, Fortran::parser::EndSubroutineStmt>(
                    subprogram, subProgramEndLine_, subProgramEndCol_, subProgramContainsLine_);
                subProgramStartCol_ =
                    captureStartCol<Fortran::parser::SubroutineSubprogram, Fortran::parser::SubroutineStmt>(
                        subprogram);
                return true;
            }

            bool Pre(const Fortran::parser::FunctionSubprogram &subprogram) {
                captureBodyEndLines<Fortran::parser::FunctionSubprogram, Fortran::parser::EndFunctionStmt>(
                    subprogram, subProgramEndLine_, subProgramEndCol_, subProgramContainsLine_);
                subProgramStartCol_ =
                    captureStartCol<Fortran::parser::FunctionSubprogram, Fortran::parser::FunctionStmt>(subprogram);
                return true;
            }

            // F2018 R1538: separate-module-subprogram, used by the
            // `module procedure foo ... end procedure foo` form in
            // submodules.  Structurally identical to SubroutineSubprogram /
            // FunctionSubprogram (MpSubprogramStmt header, SpecificationPart,
            // ExecutionPart, optional InternalSubprogramPart, EndMpSubprogramStmt
            // tail), and reuses the same body-end bookkeeping.
            bool Pre(const Fortran::parser::SeparateModuleSubprogram &subprogram) {
                captureBodyEndLines<
                    Fortran::parser::SeparateModuleSubprogram,
                    Fortran::parser::EndMpSubprogramStmt>(
                    subprogram, subProgramEndLine_, subProgramEndCol_, subProgramContainsLine_);
                subProgramStartCol_ =
                    captureStartCol<Fortran::parser::SeparateModuleSubprogram,
                        Fortran::parser::MpSubprogramStmt>(subprogram);
                return true;
            }

            void Post(const Fortran::parser::MainProgram &) {
                verboseStream() << "Exit main program: " << mainProgramName_ << "\n";
                isInMainProgram_ = false;
                mainProgramEndLine_ = 0;
                mainProgramEndCol_ = 1;
                mainProgramStartCol_ = 1;
                mainProgramContainsLine_ = 0;
            }

            void Post(const Fortran::parser::ProgramStmt &program) {
                mainProgramName_ = program.v.ToString();
                mainProgramLine_ = parsing->allCooked().GetSourcePositionRange(program.v.source)->first.line;
                verboseStream() << "Enter main program: " << mainProgramName_ << "\n";
            }

            // Returns true when the prefix list marks the subprogram as
            // pure or elemental.  Includes IMPURE ELEMENTAL: the per-element
            // dispatch that elemental procedures see when applied to array
            // arguments would otherwise spawn one TAU timer call per element,
            // drowning real signal.  Issue #36 tracks the design for actually
            // instrumenting these.
            //
            // Separate-module-procedure bodies (F2018 R1538 `module
            // procedure foo ... end procedure foo`) carry no prefix list
            // of their own; pure/elemental attributes live on the
            // matching declaration in the parent module's interface.
            // Those bodies are handled in Pre(MpSubprogramStmt) via the
            // resolved Symbol (attrs are inherited from the interface
            // during semantics, including across .mod-file boundaries).
            static bool prefixSkipsInstrumentation(const std::list<Fortran::parser::PrefixSpec> &prefixes) {
                for (const auto &p : prefixes) {
                    if (std::holds_alternative<Fortran::parser::PrefixSpec::Pure>(p.u) ||
                        std::holds_alternative<Fortran::parser::PrefixSpec::Elemental>(p.u)) {
                        return true;
                    }
                }
                return false;
            }

            // Returns true when the resolved Symbol is pure or elemental.
            // Used for separate-module-procedure bodies whose prefix lives
            // on the parent module's interface declaration.  Flang's
            // semantic phase OR's the interface's attrs into the body
            // symbol's attrs (resolve-names.cpp BeginMpSubprogram), so
            // these helpers see PURE/ELEMENTAL even when the interface
            // lives in a separately-compiled .mod file.  Mirrors the
            // IMPURE ELEMENTAL handling of prefixSkipsInstrumentation.
            static bool symbolSkipsInstrumentation(const Fortran::semantics::Symbol *symbol) {
                if (!symbol) {
                    return false;
                }
                return Fortran::semantics::IsPureProcedure(*symbol) ||
                       Fortran::semantics::IsElementalProcedure(*symbol);
            }

            // Emit a non-suppressible note that a pure or elemental
            // subprogram is being skipped.  Standard-conforming TAU
            // instrumentation cannot be injected into a pure procedure
            // because the macros declare `integer, save :: tauProfileTimer(2)`,
            // and SAVE is forbidden inside a pure procedure (F2018 C1599).
            // Elemental procedures are skipped to avoid per-element timer
            // overhead.
            void notePureOrElementalSkip(const std::string &name, const Fortran::parser::CharBlock &source) const {
                const auto pos = locationFromSource(parsing, source, false);
                const std::string filePath = pos ? *pos->path : std::string{"<unknown>"};
                const int line = pos ? pos->line : 0;
                llvm::errs() << "[SALT] " << filePath << ":" << line << ": note: "
                        << "skipping instrumentation of pure or elemental procedure '" << name
                        << "'. Track issue #36.\n";
            }

            bool Pre(const Fortran::parser::SubroutineStmt &subroutineStmt) {
                isInMainProgram_ = false;
                const auto &name = std::get<Fortran::parser::Name>(subroutineStmt.t);
                subprogramName_ = name.ToString();
                subProgramLine_ = parsing->allCooked().GetSourcePositionRange(name.source)->first.line;
                verboseStream() << "Enter Subroutine: " << subprogramName_ << "\n";
                if (prefixSkipsInstrumentation(std::get<std::list<Fortran::parser::PrefixSpec> >(subroutineStmt.t))) {
                    notePureOrElementalSkip(subprogramName_, name.source);
                    skipInstrumentSubprogram_ = true;
                } else if (!shouldInstrumentSubprogram(subprogramName_)) {
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
                subProgramEndLine_ = 0;
                subProgramEndCol_ = 1;
                subProgramStartCol_ = 1;
                subProgramContainsLine_ = 0;
            }

            bool Pre(const Fortran::parser::FunctionStmt &functionStmt) {
                isInMainProgram_ = false;
                const auto &name = std::get<Fortran::parser::Name>(functionStmt.t);
                subprogramName_ = name.ToString();
                subProgramLine_ = parsing->allCooked().GetSourcePositionRange(name.source)->first.line;
                verboseStream() << "Enter Function: " << subprogramName_ << "\n";
                if (prefixSkipsInstrumentation(std::get<std::list<Fortran::parser::PrefixSpec> >(functionStmt.t))) {
                    notePureOrElementalSkip(subprogramName_, name.source);
                    skipInstrumentSubprogram_ = true;
                } else if (!shouldInstrumentSubprogram(subprogramName_)) {
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
                subProgramEndLine_ = 0;
                subProgramEndCol_ = 1;
                subProgramStartCol_ = 1;
                subProgramContainsLine_ = 0;
            }

            // Header statement for a separate module procedure body
            // (`module procedure foo` form, F2018 R1539).  Mirrors the
            // SubroutineStmt / FunctionStmt handlers: capture the name and
            // the line of the procedure declaration, honour selective
            // instrumentation, and skip pure/elemental bodies whose
            // attributes are declared on the parent module's interface.
            bool Pre(const Fortran::parser::MpSubprogramStmt &mpStmt) {
                isInMainProgram_ = false;
                subprogramName_ = mpStmt.v.ToString();
                subProgramLine_ = parsing->allCooked().GetSourcePositionRange(mpStmt.v.source)->first.line;
                verboseStream() << "Enter Module Procedure: " << subprogramName_ << "\n";
                if (symbolSkipsInstrumentation(mpStmt.v.symbol)) {
                    notePureOrElementalSkip(subprogramName_, mpStmt.v.source);
                    skipInstrumentSubprogram_ = true;
                } else if (!shouldInstrumentSubprogram(subprogramName_)) {
                    verboseStream() << "Skipping instrumentation of " << subprogramName_ <<
                            " due to selective instrumentation\n";
                    skipInstrumentSubprogram_ = true;
                }
                return true;
            }

            void Post(const Fortran::parser::SeparateModuleSubprogram &) {
                verboseStream() << "Exit Module Procedure: " << subprogramName_ << "\n";
                skipInstrumentSubprogram_ = false;
                subprogramName_.clear();
                subProgramLine_ = 0;
                subProgramEndLine_ = 0;
                subProgramEndCol_ = 1;
                subProgramStartCol_ = 1;
                subProgramContainsLine_ = 0;
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
                const Fortran::parser::Block &block = executionPart.v;
                if (block.empty()) {
                    verboseStream() << "WARNING: Execution part empty.\n";
                    return;
                }

                const std::optional startLocOpt{getLocation(parsing, block.front(), false)};
                if (!startLocOpt.has_value()) {
                    llvm::errs() << "ERROR: execution part had no start source location!\n";
                    return;
                }
                const auto &startLoc{startLocOpt.value()};

                // Compute the line that ends the procedure's executable
                // section from the wrapping subprogram/main-program tuple --
                // either the line just before its `contains` keyword (if any)
                // or just before its End statement.  Computing it this way
                // (instead of from `block.back()` which can fail to map back
                // through CookedSource for some statements) handles issue #31
                // and llvm-project#196291 by construction.
                const int containsLine = isInMainProgram_ ? mainProgramContainsLine_ : subProgramContainsLine_;
                const int endStmtLine = isInMainProgram_ ? mainProgramEndLine_ : subProgramEndLine_;
                const int boundaryLine = containsLine > 0 ? containsLine : endStmtLine;
                if (boundaryLine <= 0) {
                    llvm::errs() << "ERROR: could not determine procedure body end; "
                            "neither contains nor End statement source resolved.\n";
                    return;
                }
                const int endLine = boundaryLine - 1;

                // Insert the timer start in the Pre phase (when we first visit the node)
                // and the timer stop in the Post phase (when we return after visiting the node's children).
                if (pre) {
                    // Source-range columns for the TAU timer name follow the
                    // PDT convention (head-begin -> body-end, both inclusive).
                    // Start col is the keyword column captured from the
                    // wrapping Statement<StartStmt>; end col is the last char
                    // of the End statement.  When the body is bounded by a
                    // ContainsStmt instead of the End, fall back to col 1
                    // for the end (the boundary line lands on the
                    // ContainsStmt itself, whose extent we don't track).
                    const int startCol = isInMainProgram_ ? mainProgramStartCol_ : subProgramStartCol_;
                    const int endCol = containsLine > 0
                        ? 1
                        : (isInMainProgram_ ? mainProgramEndCol_ : subProgramEndCol_);
                    // Implicit main programs (no program-stmt; F2018 R1101
                    // makes program-stmt optional) have no source line or
                    // name to anchor the timer to; Post(ProgramStmt) never
                    // fires and mainProgramName_/mainProgramLine_ stay
                    // default-initialised.  Synthesize a fallback name
                    // combining the input file's basename with a fixed
                    // `_implicit_main_` marker (won't collide with any
                    // user symbol since `-` is not a valid Fortran ident
                    // character), and use the first-executable-stmt line.
                    std::string procName;
                    int procStartLine;
                    if (isInMainProgram_ && mainProgramName_.empty()) {
                        procName = std::filesystem::path(startLoc.sourceFile->path()).stem().string()
                                + "._implicit_main_";
                        procStartLine = startLoc.line;
                    } else {
                        procName = isInMainProgram_ ? mainProgramName_ : subprogramName_;
                        procStartLine = isInMainProgram_ ? mainProgramLine_ : subProgramLine_;
                    }
                    std::stringstream ss;
                    ss << procName;
                    ss << " [{" << startLoc.sourceFile->path() << "} {";
                    ss << procStartLine;
                    ss << "," << startCol << "}-{";
                    ss << endLine + 1;
                    ss << "," << endCol << "}]";

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

                    if (isInMainProgram_) {
                        verboseStream() << "Program begin \"" << mainProgramName_ << "\" at " << startLoc.line <<
                                ", " << startLoc.column << "\n";
                        addProgramBeginInstrumentation(startLoc.line, splitTimerName);
                    } else {
                        verboseStream() << "Subprogram begin \"" << subprogramName_ << "\" at " << startLoc.line <<
                                ", " << startLoc.column << "\n";
                        addProcedureBeginInstrumentation(startLoc.line, splitTimerName);
                    }
                } else {
                    verboseStream() << "End at line " << endLine << "\n";
                    addProcedureEndInstrumentation(endLine);
                }
            }

            // A ReturnStmt does not have a source, so we instead need to get access to the wrapper Statement that does.
            // Here we get the ReturnStmt through ExecutableConstruct -> Statement<ActionStmt> -> Indirection<ReturnStmt>
            //
            // The same wrapper Statement<ActionStmt> covers an `if (<cond>) return` IfStmt.  Detecting the
            // if-return form here (rather than in Pre(IfStmt&)) gives us access to the wrapper's source
            // CharBlock, which spans the full physical extent of the if-stmt -- including any `&` line
            // continuations inside the condition or between the condition and the action.  The cooked
            // text of that range (and of the embedded ScalarLogicalExpr) is continuation-free.
            //
            // If locationFromSource fails to map either CharBlock we DIE
            // rather than skip: dropping a single instrumentation point
            // would leave a TAU_PROFILE_TIMER_START without a matching
            // TAU_PROFILE_STOP at runtime.  Possible causes of an
            // unmapped CharBlock include non-conforming Fortran (e.g.
            // a deleted construct -- see F2018 Annex B.2.2 / R813 /
            // R823 for the labeled-DO terminator rules), upstream
            // flang source-mapping bugs (cf. llvm-project#196291), or
            // other situations that haven't been characterised yet.
            bool Pre(const Fortran::parser::ExecutableConstruct &execConstruct) {
                if (const auto actionStmt = std::get_if<Fortran::parser::Statement<Fortran::parser::ActionStmt> >(
                    &execConstruct.u)) {
                    if (const auto retInd =
                            std::get_if<Fortran::common::Indirection<Fortran::parser::ReturnStmt> >(
                                &actionStmt->statement.u)) {
                        // Both plain `return` and the obsolescent alternate-return
                        // form `return <scalar-int-expr>` are handled identically:
                        // we inject TAU_PROFILE_STOP on the line before, and the
                        // user's `return [<expr>]` is echoed unchanged.  Issue #32.
                        if (retInd->value().v.has_value()) {
                            noteAlternateReturn(actionStmt->source, /*insideIfStmt=*/false);
                        }
                        const std::optional returnPos{locationFromSource(parsing, actionStmt->source, false)};
                        if (!returnPos.has_value()) {
                            DIE("SALT: cooked-source mapping unavailable for ReturnStmt; "
                                "refusing to emit unbalanced TAU timers");
                        }
                        const int returnLine{returnPos->line};
                        verboseStream() << "Return statement at " << returnLine << "\n";
                        addReturnStmtInstrumentation(returnLine);
                    } else if (const auto ifInd = std::get_if<Fortran::common::Indirection<Fortran::parser::IfStmt> >(
                        &actionStmt->statement.u)) {
                        const Fortran::parser::IfStmt &ifStmt{ifInd->value()};
                        const auto &innerAction{
                            std::get<Fortran::parser::UnlabeledStatement<Fortran::parser::ActionStmt> >(ifStmt.t)
                        };
                        if (const auto innerRet =
                                std::get_if<Fortran::common::Indirection<Fortran::parser::ReturnStmt> >(
                                    &innerAction.statement.u)) {
                            // The wrapping Statement<ActionStmt>'s source covers the entire `if (...) return`
                            // including continuations.  Use it to compute the physical line range to replace.
                            const auto startPosOpt{locationFromSource(parsing, actionStmt->source, false)};
                            const auto endPosOpt{locationFromSource(parsing, actionStmt->source, true)};
                            if (!startPosOpt.has_value() || !endPosOpt.has_value()) {
                                DIE("SALT: cooked-source mapping unavailable for `if (cond) return`; "
                                    "refusing to emit unbalanced TAU timers");
                            }
                            const auto &startPos{*startPosOpt};
                            const auto &endPos{*endPosOpt};
                            // Pull the cooked-source text of the logical condition.  CharBlock::ToString()
                            // yields the post-continuation, comment-stripped representation that flang's
                            // parser already worked with, so the synthesized replacement is syntactically
                            // sound regardless of how the user wrote the condition physically.
                            const std::string conditionText{
                                std::get<Fortran::parser::ScalarLogicalExpr>(ifStmt.t).thing.thing.value().source.
                                ToString()
                            };
                            // Alternate-return form `if (cond) return <expr>`: capture
                            // the int-expression's cooked text so the synthesized
                            // `if-then-endif` replays `return <expr>` faithfully.
                            // Empty text means a plain `return`. Issue #32.
                            std::string returnExprText;
                            if (innerRet->value().v.has_value()) {
                                returnExprText =
                                    innerRet->value().v.value().thing.thing.value().source.ToString();
                                noteAlternateReturn(actionStmt->source, /*insideIfStmt=*/true);
                            }
                            verboseStream() << "If-return spans lines " << startPos.line << "-" << endPos.line
                                    << ", condition: " << conditionText
                                    << (returnExprText.empty() ? "" : ", return expr: " + returnExprText)
                                    << (actionStmt->label.has_value()
                                            ? ", label: " + std::to_string(actionStmt->label.value())
                                            : "")
                                    << "\n";
                            addIfReturnStmtInstrumentation(startPos.line, endPos.line, conditionText,
                                                           returnExprText, actionStmt->label);
                        }
                    }
                }
                return true;
            }

            // Informational note that the user's source uses the obsolescent
            // alternate-return form (`return <scalar-int-expr>`, optionally
            // inside `if (cond)`).  Listed under Fortran 2018 Annex B.3
            // "Obsolescent features"; future revisions of the standard may
            // delete it.  SALT instruments these statements correctly today
            // (issue #32 closed); the note exists so users know to migrate
            // away from the form, not because instrumentation skipped it.
            //
            // Emitted to stderr at always-on severity (not gated on verbose)
            // so it is visible during a normal build.
            void noteAlternateReturn(const Fortran::parser::CharBlock &source, const bool insideIfStmt) const {
                const auto pos = locationFromSource(parsing, source, false);
                const std::string filePath = pos ? *pos->path : std::string{"<unknown>"};
                const int line = pos ? pos->line : 0;
                llvm::errs() << "[SALT] " << filePath << ":" << line << ": note: "
                        << (insideIfStmt
                                ? "instrumenting `if (<cond>) return <expr>`, "
                                : "instrumenting `return <expr>`, ")
                        << "an obsolescent alternate-return form "
                        << "(Fortran 2018 Annex B.3); consider replacing it with structured control flow.\n";
            }

        private:
            // Keeps track of current state of traversal
            bool isInMainProgram_{false};
            std::string mainProgramName_;
            int mainProgramLine_{0};
            // Line numbers of the wrapping End{Program,Subroutine,Function}Stmt
            // and (when present) the ContainsStmt that begins the
            // InternalSubprogramPart.  Used to bound the procedure's
            // executable section from above instead of relying on the last
            // ExecutionPartConstruct's source mapping (issue #31).
            int mainProgramEndLine_{0};
            int mainProgramContainsLine_{0};
            // Columns for the TAU timer name source-range field.  Captured
            // from the wrapping Statement<StartStmt>::source for start
            // (keyword position; defaults to 1 for the implicit-main form
            // and as a generic fallback) and from
            // Statement<EndStmt>::source's last-character column for end
            // (PDT-style inclusive last-char convention).
            int mainProgramStartCol_{1};
            int mainProgramEndCol_{1};
            std::string subprogramName_;
            int subProgramLine_{0};
            int subProgramEndLine_{0};
            int subProgramContainsLine_{0};
            int subProgramStartCol_{1};
            int subProgramEndCol_{1};

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

                // Then, process instrumentation points that REPLACE this line.  An
                // IfReturnStmtInstrumentationPoint can span multiple physical lines
                // when the if-stmt's condition is split via `&` continuation; in that
                // case we must consume input lines through endLine() so the original
                // continuation tail is not echoed after our synthesized replacement.
                while (instIter != instPts.cend() && (*instIter)->line() == lineNum && (*instIter)->location() ==
                       InstrumentationLocation::REPLACE) {
                    outputStream << lineDirective(lineNum, inputFilePath) << "\n";
                    outputStream << (*instIter)->instrumentationString(instMap, lineText) << "\n";
                    int replacedThroughLine{lineNum};
                    // SALT links against LLVM with -fno-rtti, so dynamic_cast is
                    // unavailable.  Use the instrumentation-type tag to recover the
                    // concrete subclass.
                    if ((*instIter)->instrumentationType() == InstrumentationPointType::IF_RETURN) {
                        const auto *ifReturnPt =
                                static_cast<const IfReturnStmtInstrumentationPoint *>(instIter->get());
                        replacedThroughLine = ifReturnPt->endLine();
                    }
                    while (lineNum < replacedThroughLine) {
                        std::string discardedLine;
                        if (!std::getline(inputStream, discardedLine)) {
                            break;
                        }
                        ++lineNum;
                    }
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
            // Order matters: escape regex metacharacters first so any "."
            // characters in the input become "\\.", then substitute the
            // surviving literal "*" with ".*". Doing the "*" -> ".*" rewrite
            // first would let the inserted "." get escaped to "\\.", turning
            // the wildcard into a quantifier on a literal dot.
            static const std::regex metacharacters(R"([\.\^\$\+\(\)\[\]\{\}\|\?])");
            const std::string escapedString{std::regex_replace(globString, metacharacters, R"(\$&)")};
            static const std::regex starRegex{R"(\*)"};
            return std::regex_replace(escapedString, starRegex, ".*");
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
                if (const std::regex excludeRegex{convertGlobToRegexForm(excludeEntry)}; std::regex_match(
                    filePart.string(), excludeRegex)) {
                    return false;
                }
            }

            bool fileInIncludeList{false};
            for (const auto &includeEntry: fileincludelist) {
                if (const std::regex includeRegex{convertGlobToRegexForm(includeEntry)}; std::regex_match(
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
