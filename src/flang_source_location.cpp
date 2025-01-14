/* Copyright (C) 2025, ParaTools, Inc.

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

#include "flang/Parser/char-block.h"
#include "flang/Parser/parsing.h"
#include "flang/Parser/source.h"

#include "flang_source_location.hpp"

[[nodiscard]] std::optional<Fortran::parser::SourcePosition> salt::fortran::locationFromSource(
    const Fortran::parser::Parsing *parsing, const Fortran::parser::CharBlock &charBlock, const bool end) {
    if (const auto &sourceRange{parsing->allCooked().GetSourcePositionRange(charBlock)}; sourceRange.
        has_value()) {
        if (end) {
            return sourceRange->second;
        }
        return sourceRange->first;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Fortran::parser::SourcePosition> salt::fortran::getLocation(
    const Fortran::parser::Parsing *parsing,
    const Fortran::parser::OpenMPDeclarativeConstruct &construct,
    const bool end) {
    // This function is based on the equivalent function in
    // flang/examples/FlangOmpReport/FlangOmpReportVisitor.cpp
    return std::visit(
        [&](const auto &o) -> std::optional<Fortran::parser::SourcePosition> {
            return locationFromSource(parsing, o.source, end);
        },
        construct.u);
}

[[nodiscard]] std::optional<Fortran::parser::SourcePosition> salt::fortran::getLocation(
    const Fortran::parser::Parsing *parsing,
    const Fortran::parser::OpenMPConstruct &construct,
    const bool end) {
    // This function is based on the equivalent function in
    // flang/examples/FlangOmpReport/FlangOmpReportVisitor.cpp
    return std::visit(
        Fortran::common::visitors{
            [&](const Fortran::parser::OpenMPStandaloneConstruct &c) -> std::optional<
        Fortran::parser::SourcePosition> {
                return locationFromSource(parsing, c.source, end);
            },
            // OpenMPSectionsConstruct, OpenMPLoopConstruct,
            // OpenMPBlockConstruct, OpenMPCriticalConstruct Get the source from
            // the directive field.
            [&](const auto &c) -> std::optional<Fortran::parser::SourcePosition> {
                const Fortran::parser::CharBlock &source{std::get<0>(c.t).source};
                return locationFromSource(parsing, source, end);
            },
            [&](const Fortran::parser::OpenMPAtomicConstruct &c) -> std::optional<
        Fortran::parser::SourcePosition> {
                return std::visit(
                    [&](const auto &o) -> std::optional<Fortran::parser::SourcePosition> {
                        const Fortran::parser::CharBlock &source{
                            std::get<Fortran::parser::Verbatim>(o.t).source
                        };
                        return locationFromSource(parsing, source, end);
                    },
                    c.u);
            },
            [&](const Fortran::parser::OpenMPSectionConstruct &c) -> std::optional<
        Fortran::parser::SourcePosition> {
                const Fortran::parser::CharBlock &source{c.source};
                return locationFromSource(parsing, source, end);
            },
        },
        construct.u);
}

[[nodiscard]] std::optional<Fortran::parser::SourcePosition> salt::fortran::getLocation(
    const Fortran::parser::Parsing *parsing,
    const Fortran::parser::OpenACCConstruct &construct, const bool end) {
    // This function is based on the equivalent function in
    // flang/examples/FlangOmpReport/FlangOmpReportVisitor.cpp
    return std::visit(
        Fortran::common::visitors{
            [&](const auto &c) -> std::optional<Fortran::parser::SourcePosition> {
                return locationFromSource(parsing, c.source, end);
            },
            [&](const Fortran::parser::OpenACCBlockConstruct &c) -> std::optional<
        Fortran::parser::SourcePosition> {
                if (end) {
                    return locationFromSource(parsing, std::get<Fortran::parser::AccEndBlockDirective>(c.t).source,
                                              end);
                }
                return locationFromSource(parsing, std::get<Fortran::parser::AccBeginBlockDirective>(c.t).source, end);
            },
            [&](const Fortran::parser::OpenACCLoopConstruct &c) -> std::optional<
        Fortran::parser::SourcePosition> {
                if (end) {
                    if (const auto &maybeDo = std::get<std::optional<Fortran::parser::DoConstruct> >(c.t);
                        maybeDo.has_value()) {
                        return locationFromSource(parsing,
                            std::get<Fortran::parser::Statement<Fortran::parser::EndDoStmt> >(maybeDo.value().t)
                            .
                            source, end);
                    }
                }
                return locationFromSource(parsing, std::get<Fortran::parser::AccBeginLoopDirective>(c.t).source, end);
            },
        }, construct.u);
}

[[nodiscard]] std::optional<Fortran::parser::SourcePosition> salt::fortran::getLocation(
    const Fortran::parser::Parsing *parsing,
    const Fortran::parser::ExecutableConstruct &construct,
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
            [&](const auto &c) -> std::optional<Fortran::parser::SourcePosition> {
                return locationFromSource(parsing, c.source, end);
            },
            [&](const Fortran::common::Indirection<Fortran::parser::CUFKernelDoConstruct> &c) ->
        std::optional<Fortran::parser::SourcePosition> {
                if (end) {
                    const auto &optionalConstruct = std::get<std::optional<Fortran::parser::DoConstruct> >(
                        c.value().t);
                    if (optionalConstruct.has_value()) {
                        return locationFromSource(parsing,
                            std::get<Fortran::parser::Statement<Fortran::parser::EndDoStmt> >(
                                optionalConstruct.value().t).source, end);
                    }
                }
                return locationFromSource(parsing,
                    std::get<Fortran::parser::CUFKernelDoConstruct::Directive>(c.value().t).source, end);
            },
            [&](const Fortran::common::Indirection<Fortran::parser::OmpEndLoopDirective> &c) ->
        std::optional<Fortran::parser::SourcePosition> {
                return locationFromSource(parsing, c.value().source, end);
            },
            [&](const Fortran::common::Indirection<Fortran::parser::OpenMPConstruct> &c) ->
        std::optional<Fortran::parser::SourcePosition> {
                return getLocation(parsing, c.value(), end);
            },
            [&](const Fortran::common::Indirection<Fortran::parser::AccEndCombinedDirective> &c) ->
        std::optional<Fortran::parser::SourcePosition> {
                return locationFromSource(parsing, c.value().source, end);
            },
            [&](const Fortran::common::Indirection<Fortran::parser::OpenACCConstruct> &c) ->
        std::optional<Fortran::parser::SourcePosition> {
                return getLocation(parsing, c.value(), end);
            },
            [&](const Fortran::common::Indirection<Fortran::parser::CompilerDirective> &c)->
        std::optional<Fortran::parser::SourcePosition> {
                return locationFromSource(parsing, c.value().source, end);
            },
            [&](const Fortran::common::Indirection<Fortran::parser::ForallConstruct> &c) ->
        std::optional<Fortran::parser::SourcePosition> {
                if (end) {
                    return locationFromSource(parsing,
                        std::get<Fortran::parser::Statement<Fortran::parser::EndForallStmt> >(c.value().t).
                        source, end);
                }
                return locationFromSource(parsing,
                    std::get<Fortran::parser::Statement<Fortran::parser::ForallConstructStmt> >(c.value().t).
                    source, end);
            },
            [&](const Fortran::common::Indirection<Fortran::parser::WhereConstruct> &c) ->
        std::optional<Fortran::parser::SourcePosition> {
                if (end) {
                    return locationFromSource(parsing,
                        std::get<Fortran::parser::Statement<Fortran::parser::EndWhereStmt> >(c.value().t).
                        source, end);
                }
                return locationFromSource(parsing,
                    std::get<Fortran::parser::Statement<Fortran::parser::WhereConstructStmt> >(c.value().t).
                    source, end);
            },
            [&](const Fortran::common::Indirection<Fortran::parser::SelectTypeConstruct> &c) ->
        std::optional<Fortran::parser::SourcePosition> {
                if (end) {
                    return locationFromSource(parsing,
                        std::get<Fortran::parser::Statement<Fortran::parser::EndSelectStmt> >(c.value().t).
                        source, end);
                }
                return locationFromSource(parsing,
                    std::get<Fortran::parser::Statement<Fortran::parser::SelectTypeStmt> >(c.value().t).source,
                    end);
            },
            [&](const Fortran::common::Indirection<Fortran::parser::SelectRankConstruct> &c) ->
        std::optional<Fortran::parser::SourcePosition> {
                if (end) {
                    return locationFromSource(parsing,
                        std::get<Fortran::parser::Statement<Fortran::parser::EndSelectStmt> >(c.value().t).
                        source, end);
                }
                return locationFromSource(parsing,
                    std::get<Fortran::parser::Statement<Fortran::parser::SelectRankStmt> >(c.value().t).
                    source, end);
            },
            [&](const Fortran::common::Indirection<Fortran::parser::IfConstruct> &c) ->
        std::optional<Fortran::parser::SourcePosition> {
                if (end) {
                    return locationFromSource(parsing,
                        std::get<Fortran::parser::Statement<Fortran::parser::EndIfStmt> >(c.value().t).source,
                        end);
                }
                return locationFromSource(parsing,
                    std::get<Fortran::parser::Statement<Fortran::parser::IfThenStmt> >(c.value().t).source,
                    end);
            },
            [&](const Fortran::common::Indirection<Fortran::parser::DoConstruct> &c) ->
        std::optional<Fortran::parser::SourcePosition> {
                if (end) {
                    return locationFromSource(parsing,
                        std::get<Fortran::parser::Statement<Fortran::parser::EndDoStmt> >(c.value().t).source,
                        end);
                }
                return locationFromSource(parsing,
                    std::get<Fortran::parser::Statement<Fortran::parser::NonLabelDoStmt> >(c.value().t).source,
                    end);
            },
            [&](const Fortran::common::Indirection<Fortran::parser::CriticalConstruct> &c) ->
        std::optional<Fortran::parser::SourcePosition> {
                if (end) {
                    return locationFromSource(parsing,
                        std::get<Fortran::parser::Statement<Fortran::parser::EndCriticalStmt> >(c.value().t).
                        source,
                        end);
                }
                return locationFromSource(parsing,
                    std::get<Fortran::parser::Statement<Fortran::parser::CriticalStmt> >(c.value().t).source,
                    end);
            },
            [&](const Fortran::common::Indirection<Fortran::parser::ChangeTeamConstruct> &c) ->
        std::optional<Fortran::parser::SourcePosition> {
                if (end) {
                    return locationFromSource(parsing,
                        std::get<Fortran::parser::Statement<Fortran::parser::EndChangeTeamStmt> >(c.value().t).
                        source,
                        end);
                }
                return locationFromSource(parsing,
                    std::get<Fortran::parser::Statement<Fortran::parser::ChangeTeamStmt> >(c.value().t).source,
                    end);
            },
            [&](const Fortran::common::Indirection<Fortran::parser::CaseConstruct> &c) ->
        std::optional<Fortran::parser::SourcePosition> {
                if (end) {
                    return locationFromSource(parsing,
                        std::get<Fortran::parser::Statement<Fortran::parser::EndSelectStmt> >(c.value().t).
                        source, end);
                }
                return locationFromSource(parsing,
                    std::get<Fortran::parser::Statement<Fortran::parser::SelectCaseStmt> >(c.value().t).source,
                    end);
            },
            [&](const Fortran::common::Indirection<Fortran::parser::BlockConstruct> &c) ->
        std::optional<Fortran::parser::SourcePosition> {
                if (end) {
                    return locationFromSource(parsing,
                        std::get<Fortran::parser::Statement<Fortran::parser::EndBlockStmt> >(c.value().t).
                        source,
                        end);
                }
                return locationFromSource(parsing,
                    std::get<Fortran::parser::Statement<Fortran::parser::BlockStmt> >(c.value().t).source, end);
            },
            [&](const Fortran::common::Indirection<Fortran::parser::AssociateConstruct> &c) ->
        std::optional<Fortran::parser::SourcePosition> {
                if (end) {
                    return locationFromSource(parsing,
                        std::get<Fortran::parser::Statement<Fortran::parser::EndAssociateStmt> >(c.value().t).
                        source, end);
                }
                return locationFromSource(parsing,
                    std::get<Fortran::parser::Statement<Fortran::parser::AssociateStmt> >(c.value().t).
                    source, end);
            }
        }, construct.u);
}

[[nodiscard]] std::optional<Fortran::parser::SourcePosition> salt::fortran::getLocation(
    const Fortran::parser::Parsing *parsing,
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
            [&](const Fortran::parser::ExecutableConstruct &c) -> std::optional<
        Fortran::parser::SourcePosition> {
                return getLocation(parsing, c, end);
            },
            [&](const auto &c) -> std::optional<Fortran::parser::SourcePosition> {
                return locationFromSource(parsing, c.source, end);
            },
            [&](const Fortran::parser::ErrorRecovery &) -> std::optional<Fortran::parser::SourcePosition> {
                DIE("Should not encounter ErrorRecovery in parse tree");
            }
        }, construct.u);
}
