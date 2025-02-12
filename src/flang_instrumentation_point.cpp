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


#include <string>
#include <sstream>
#include <regex>

#include "flang/Common/idioms.h"

#include "dprint.hpp"
#include "flang_instrumentation_point.hpp"
#include "flang_instrumentation_constants.hpp"


using namespace std::string_literals;

std::string salt::fortran::InstrumentationPoint::typeString() const {
    switch (instrumentationType()) {
        case InstrumentationPointType::PROGRAM_BEGIN:
            return "PROGRAM_BEGIN"s;
        case InstrumentationPointType::PROCEDURE_BEGIN:
            return "PROCEDURE_BEGIN"s;
        case InstrumentationPointType::PROCEDURE_END:
            return "PROCEDURE_END"s;
        case InstrumentationPointType::RETURN_STMT:
            return "RETURN_STMT"s;
        case InstrumentationPointType::IF_RETURN:
            return "IF_RETURN"s;
        default:
            CRASH_NO_CASE;
    }
}

std::string salt::fortran::InstrumentationPoint::locationString() const {
    switch (location()) {
        case InstrumentationLocation::BEFORE:
            return "BEFORE"s;
        case InstrumentationLocation::AFTER:
            return "AFTER"s;
        case InstrumentationLocation::REPLACE:
            return "REPLACE"s;
        default:
            CRASH_NO_CASE;
    }
}

std::string salt::fortran::InstrumentationPoint::toString() const {
    std::stringstream ss;
    ss << line() << "\t";
    ss << locationString() << "\t";
    ss << typeString() << "\t";
    return ss.str();
}

std::string salt::fortran::InstrumentationPoint::instrumentationString(const InstrumentationMap &instMap,
                                                                       [[maybe_unused]] const std::string &lineText)
const {
    return instMap.at(instrumentationType());
}

std::string salt::fortran::ProgramBeginInstrumentationPoint::toString() const {
    std::stringstream ss;
    ss << InstrumentationPoint::toString();
    ss << "\"" << timerName() << "\"\t";
    return ss.str();
}

std::string salt::fortran::ProgramBeginInstrumentationPoint::instrumentationString(
    const InstrumentationMap &instMap, [[maybe_unused]] const std::string &lineText) const {
    static std::regex timerNameRegex{SALT_FORTRAN_TIMER_NAME_TEMPLATE};
    const std::string instTemplate{InstrumentationPoint::instrumentationString(instMap, lineText)};
    return std::regex_replace(instTemplate, timerNameRegex, timerName());
}

std::string salt::fortran::ProcedureBeginInstrumentationPoint::toString() const {
    std::stringstream ss;
    ss << InstrumentationPoint::toString();
    ss << "\"" << timerName() << "\"\t";
    return ss.str();
}

std::string salt::fortran::ProcedureBeginInstrumentationPoint::instrumentationString(
    const InstrumentationMap &instMap, [[maybe_unused]] const std::string &lineText) const {
    static std::regex timerNameRegex{SALT_FORTRAN_TIMER_NAME_TEMPLATE};
    const std::string instTemplate{InstrumentationPoint::instrumentationString(instMap, lineText)};
    return std::regex_replace(instTemplate, timerNameRegex, timerName());
}

std::string salt::fortran::ProcedureEndInstrumentationPoint::toString() const {
    std::stringstream ss;
    ss << InstrumentationPoint::toString();
    ss << "\"" << timerName() << "\"\t";
    return ss.str();
}

std::string salt::fortran::ProcedureEndInstrumentationPoint::instrumentationString(
    const InstrumentationMap &instMap, [[maybe_unused]] const std::string &lineText) const {
    static std::regex timerNameRegex{SALT_FORTRAN_TIMER_NAME_TEMPLATE};
    const std::string instTemplate{InstrumentationPoint::instrumentationString(instMap, lineText)};
    return std::regex_replace(instTemplate, timerNameRegex, timerName());
}

std::string salt::fortran::IfReturnStmtInstrumentationPoint::toString() const {
    std::stringstream ss;
    ss << InstrumentationPoint::toString();
    ss << conditionalColumn() << "\t";
    return ss.str();
}

std::string salt::fortran::IfReturnStmtInstrumentationPoint::instrumentationString(
    const InstrumentationMap &instMap, const std::string &lineText) const {
    std::stringstream ss;
    ss << lineText.substr(0, conditionalColumn()) << " then\n";
    ss << InstrumentationPoint::instrumentationString(instMap, lineText) << "\n";
    ss << "      return\n";
    ss << "      endif\n";
    return ss.str();
}
