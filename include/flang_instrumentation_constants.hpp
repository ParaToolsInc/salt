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

#ifndef FLANG_INSTRUMENTATION_CONSTANTS_HPP
#define FLANG_INSTRUMENTATION_CONSTANTS_HPP

// Verbose flag environment variable
#define SALT_FORTRAN_VERBOSE_VAR "SALT_FORTRAN_VERBOSE"

// Configuration file environment variable
#define SALT_FORTRAN_CONFIG_FILE_VAR "SALT_FORTRAN_CONFIG_FILE"
#define SALT_FORTRAN_CONFIG_DEFAULT_PATH "config_files/tau_config.yaml"

// Selective instrumentation environment variable
#define SALT_FORTRAN_SELECT_FILE_VAR "SALT_FORTRAN_SELECT_FILE"

// Configuration file YAML keys
#define SALT_FORTRAN_KEY "Fortran"
#define SALT_FORTRAN_PROGRAM_BEGIN_KEY "program_insert"
#define SALT_FORTRAN_PROCEDURE_BEGIN_KEY "procedure_begin_insert"
#define SALT_FORTRAN_PROCEDURE_END_KEY "procedure_end_insert"

// Configuration file template replacement strings
#define SALT_FORTRAN_TIMER_NAME_TEMPLATE R"(\$\{full_timer_name\})"

// Fortran line splitting
#define SALT_FORTRAN_STRING_SPLITTER "&\n     &"
#define SALT_F77_LINE_LENGTH 64

#endif //FLANG_INSTRUMENTATION_CONSTANTS_HPP
