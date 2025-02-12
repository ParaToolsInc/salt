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

#ifndef FLANG_INSTRUMENTATION_POINT_HPP
#define FLANG_INSTRUMENTATION_POINT_HPP

#include <string>
#include <map>
#include <utility>

namespace salt::fortran {
    enum class InstrumentationPointType {
        PROGRAM_BEGIN, // Declare profiler, initialize TAU, set node, start timer
        PROCEDURE_BEGIN, // Declare profiler, start timer
        PROCEDURE_END, // Stop timer on the line after
        RETURN_STMT, // Stop timer on the line before
        IF_RETURN // Transform if to if-then-endif, stop timer before return
    };

    enum class InstrumentationLocation {
        BEFORE,
        AFTER,
        REPLACE
    };

    using InstrumentationMap = std::map<InstrumentationPointType, const std::string>;

    class InstrumentationPoint {
    public:
        InstrumentationPoint(const InstrumentationPointType type, const int line,
                             const InstrumentationLocation location) : instrumentationType_(type), line_(line),
                                                                       location_(location) {
        }

        virtual ~InstrumentationPoint() = default;

        [[nodiscard]] InstrumentationPointType instrumentationType() const {
            return instrumentationType_;
        }

        [[nodiscard]] int line() const {
            return line_;
        }

        [[nodiscard]] InstrumentationLocation location() const {
            return location_;
        }

        [[nodiscard]] bool instrumentBefore() const {
            return location() == InstrumentationLocation::BEFORE;
        }

        bool operator<(const InstrumentationPoint &other) const {
            if (line() == other.line()) {
                if (instrumentBefore() && !other.instrumentBefore()) {
                    return true;
                }
                return false;
            }
            return line() < other.line();
        }

        [[nodiscard]] std::string typeString() const;

        [[nodiscard]] std::string locationString() const;

        [[nodiscard]] virtual std::string toString() const;

        [[nodiscard]] virtual std::string instrumentationString(const InstrumentationMap &instMap,
                                                                const std::string &lineText) const;

    private:
        const InstrumentationPointType instrumentationType_;
        const int line_;
        const InstrumentationLocation location_;
    };

    class ProgramBeginInstrumentationPoint final : public InstrumentationPoint {
    public:
        ProgramBeginInstrumentationPoint(const int line, std::string timerName) : InstrumentationPoint(
                InstrumentationPointType::PROGRAM_BEGIN, line, InstrumentationLocation::BEFORE),
            timerName_(std::move(timerName)) {
        }

        [[nodiscard]] std::string timerName() const {
            return timerName_;
        }

        [[nodiscard]] std::string toString() const override;

        [[nodiscard]] std::string instrumentationString(const InstrumentationMap &instMap,
                                                        const std::string &lineText) const override;

    private:
        const std::string timerName_;
    };

    class ProcedureBeginInstrumentationPoint final : public InstrumentationPoint {
    public:
        ProcedureBeginInstrumentationPoint(const int line, std::string timerName) : InstrumentationPoint(
                InstrumentationPointType::PROCEDURE_BEGIN,
                line,
                InstrumentationLocation::BEFORE),
            timerName_(std::move(timerName)) {
        }

        [[nodiscard]] std::string timerName() const {
            return timerName_;
        }

        [[nodiscard]] std::string toString() const override;

        [[nodiscard]] std::string instrumentationString(const InstrumentationMap &instMap,
                                                        const std::string &lineText) const override;

    private:
        const std::string timerName_;
    };
class ProcedureEndInstrumentationPoint final : public InstrumentationPoint {
public:
    ProcedureEndInstrumentationPoint(const int line, std::string timerName) : InstrumentationPoint(
        InstrumentationPointType::PROCEDURE_END, line, InstrumentationLocation::AFTER),
        timerName_(std::move(timerName)) {
    }

    [[nodiscard]] std::string timerName() const {
        return timerName_;
    }

    [[nodiscard]] std::string toString() const override;

    [[nodiscard]] std::string instrumentationString(const InstrumentationMap &instMap,
                                                    const std::string &lineText) const override;

private:
    const std::string timerName_;
};
    class ReturnStmtInstrumentationPoint final : public InstrumentationPoint {
    public:
        explicit ReturnStmtInstrumentationPoint(const int line) : InstrumentationPoint(
            InstrumentationPointType::RETURN_STMT, line, InstrumentationLocation::BEFORE) {
        }
    };

    class IfReturnStmtInstrumentationPoint final : public InstrumentationPoint {
    public:
        explicit IfReturnStmtInstrumentationPoint(const int line, const int conditionalColumn) : InstrumentationPoint(
                InstrumentationPointType::IF_RETURN, line, InstrumentationLocation::REPLACE),
            conditionalColumn_(conditionalColumn) {
        }

        [[nodiscard]] int conditionalColumn() const {
            return conditionalColumn_;
        }

        [[nodiscard]] std::string toString() const override;

        [[nodiscard]] std::string instrumentationString(const InstrumentationMap &instMap,
                                                        const std::string &lineText) const override;

    private:
        const int conditionalColumn_;
    };
}

#endif //FLANG_INSTRUMENTATION_POINT_HPP
