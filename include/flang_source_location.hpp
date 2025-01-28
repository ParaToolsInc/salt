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

#ifndef FLANG_SOURCE_LOCATION_H
#define FLANG_SOURCE_LOCATION_H


#include <optional>

#include "flang/Parser/char-block.h"
#include "flang/Parser/parsing.h"
#include "flang/Parser/source.h"

namespace salt::fortran {
    /**
     * From a CharBlock object (generally held in the `source` field of a parse tree node,
     * get the source position (file, line, column).
     * If `end` is set, returns the ending position of the block.
     * If `end` is not set (and by default), returns the starting position of the block.
     */
    [[nodiscard]] std::optional<Fortran::parser::SourcePosition> locationFromSource(
        const Fortran::parser::Parsing *parsing,
        const Fortran::parser::CharBlock &charBlock,
        bool end);

    /**
    * Gets the location (if present) associated with an OpenMPDeclarativeConstruct.
    * If `end` is set, returns the ending position of the block.
    * If `end` is not set (and by default), returns the starting position of the block.
    */
    [[nodiscard]] std::optional<Fortran::parser::SourcePosition> getLocation(
        const Fortran::parser::Parsing *parsing,
        const Fortran::parser::OpenMPDeclarativeConstruct &construct,
        bool end);

    /**
    * Gets the location (if present) associated with an OpenMPConstruct.
    * If `end` is set, returns the ending position of the block.
    * If `end` is not set (and by default), returns the starting position of the block.
    */
    [[nodiscard]] std::optional<Fortran::parser::SourcePosition> getLocation(
        const Fortran::parser::Parsing *parsing,
        const Fortran::parser::OpenMPConstruct &construct,
        bool end);

    /**
    * Gets the location (if present) associated with an OpenACCConstruct.
    * If `end` is set, returns the ending position of the block.
    * If `end` is not set (and by default), returns the starting position of the block.
    */
    [[nodiscard]] std::optional<Fortran::parser::SourcePosition> getLocation(
        const Fortran::parser::Parsing *parsing,
        const Fortran::parser::OpenACCConstruct &construct,
        bool end);

    /**
    * Gets the location (if present) associated with an ExecutableConstruct.
    * If `end` is set, returns the ending position of the block.
    * If `end` is not set (and by default), returns the starting position of the block.
    */
    [[nodiscard]] std::optional<Fortran::parser::SourcePosition> getLocation(
        const Fortran::parser::Parsing *parsing,
        const Fortran::parser::ExecutableConstruct &construct,
        bool end);

    /**
    * Gets the location (if present) associated with an ExecutionPartConstruct.
    * If `end` is set, returns the ending position of the block.
    * If `end` is not set (and by default), returns the starting position of the block.
    */
    [[nodiscard]] std::optional<Fortran::parser::SourcePosition> getLocation(
        const Fortran::parser::Parsing *parsing,
        const Fortran::parser::ExecutionPartConstruct &construct,
        bool end);

}

#endif //FLANG_SOURCE_LOCATION_H
