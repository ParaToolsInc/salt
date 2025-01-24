#ifdef __linux__
#include <unistd.h>
#elif __APPLE__
#include <mach-o/dyld.h>
#endif
#include "instrumentor.hpp"
#include "frontend.hpp"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTTypeTraits.h"
#include "clang/AST/Type.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Linkage.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <regex>
#include <string>
#include <vector>

#include "selectfile.hpp"

using namespace clang;

llvm::cl::opt<std::string> outputfile("tau_output", llvm::cl::desc("Specify name of output instrumented file"),
                                      llvm::cl::value_desc("filename"), llvm::cl::cat(MyToolCategory));

std::string getExecutablePath() {
    char buffer[1024];
    std::string path;

    #ifdef __linux__
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        path = std::string(buffer);
    }
    #elif __APPLE__
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) == 0) {
        path = std::string(buffer);
    } else {
        char *pathBuffer = new char[size];
        if (_NSGetExecutablePath(pathBuffer, &size) == 0) {
            path = std::string(pathBuffer);
        }
        delete[] pathBuffer;
    }
    #endif

    // Remove the executable name from the path
    size_t pos = path.find_last_of("/\\");
    if (pos != std::string::npos) {
        path = path.substr(0, pos);
    }

    return path;
}

std::string getEnvCfgFile() {
    char *val = getenv("SALT_CONFIG_FILE");
    if (val == NULL) {
        std::string execPath = getExecutablePath();
        return execPath + "/" + SALT_DEFAULT_CONFIG_FILE;
    } else {
        return std::string(val);
    }
}


llvm::cl::opt<std::string> configfile("config_file", llvm::cl::desc("Specify path to SALT configuration YAML file"),
                                      llvm::cl::value_desc("filename"), llvm::cl::init(getEnvCfgFile()),
                                      llvm::cl::cat(MyToolCategory));

llvm::cl::opt<bool> use_cxx_api("tau_use_cxx_api", llvm::cl::desc("Use TAU's C++ instrumentation API"),
                                llvm::cl::init(false), llvm::cl::cat(MyToolCategory));

llvm::cl::opt<bool> do_inline("tau_instrument_inline", llvm::cl::desc("Instrument inlined functions (default: false)"),
                              llvm::cl::init(false), llvm::cl::cat(MyToolCategory));

llvm::cl::opt<std::string> selectfile("tau_select_file",
                                      llvm::cl::desc("Provide a selective instrumentation specification file"),
                                      llvm::cl::value_desc("filename"), llvm::cl::cat(MyToolCategory));

#include "clang_header_includes.h"

char **addHeadersToCommand(int *argc, const char **argv)
{
    // first, check for no arguments.    If none, add --help
    if (*argc == 1)
    {
        *argc = 2;
        char **new_argv = (char **)(calloc(2, sizeof(char *)));
        new_argv[0] = strdup(argv[0]);
        new_argv[1] = strdup("--help");
        return new_argv;
    }
    // We need to add N include paths to the argument list, after the '--'
    int new_argc = *argc + clang_header_includes_length;
    char **new_argv = (char **)(calloc(new_argc, sizeof(char *)));
    int new_i = 0;
    bool found_dash = false; // Marker for finding "--"

    for (int i = 0; i <= *argc; i++)
    {
        if (i == *argc && found_dash) break;
        bool at_dash = false;
        if ((i == *argc) ||                             // found "--"
            (at_dash = strcmp(argv[i], "--") == 0))     // Reached end of argv without finding "--"
        {
            if (at_dash) {
                found_dash = true;
                new_argv[new_i] = strdup(argv[i]);
            }
            else
            {
                new_argv = (char **)realloc(new_argv, (++new_argc)*sizeof(char *));
                new_argv[new_i] = strdup("--");
            }
            new_i++;
            for (int j = 0; j < clang_header_includes_length; j++)
            {
                new_argv[new_i] = strdup(clang_header_includes[j]);
                new_i++;
            }
        }
        else
        {
            // ignore linker flags
            if ((strncmp(argv[i], "-L", 2) != 0) ||        // ignore library paths
                (strncmp(argv[i], "-l", 2) != 0) ||        // ignore libraries
                (strncmp(argv[i], "-D_OPENMP", 9) != 0) || // ignore expanded OpenMP macro
                (strncmp(argv[i], "-Wl", 3) != 0))
            { // ignore linker options
                new_argv[new_i] = strdup(argv[i]);
                new_i++;
            }
            else
            {
                new_argc--;
            }
        }
    }
    *argc = new_argc;
    return new_argv;
}

void findFiles(const std::vector<std::string>& files, instrumentor& CI)
{
    for (auto fname : files)
    {
        std::string temp1;
        auto location = fname.find_last_of("/");
        if (location != std::string::npos)
        {
            temp1 = fname.substr(location + 1);
        }
        else
        {
            temp1 = fname;
        }
        CI.file_set.insert(temp1);
    }

    for (std::string fil : files)
    {
        if (!fileincludelist.empty())
        {
            if (check_file_against_list(fileincludelist, fil) && !check_file_against_list(fileexcludelist, fil))
            {
                if (!fil.empty() && std::find(files_to_go.begin(), files_to_go.end(), fil) == files_to_go.end())
                {
                    files_to_go.push_back(fil);
                }
            }
            else
            {
                files_skipped.push_back(fil);
            }
        }
        else
        {
            if (!check_file_against_list(fileexcludelist, fil))
            {
                if (!fil.empty() && std::find(files_to_go.begin(), files_to_go.end(), fil) == files_to_go.end())
                {
                    files_to_go.push_back(fil);
                }
            }
            else
            {
                files_skipped.push_back(fil);
            }
        }
    }

    if (files.size() < 1)
    {
        fprintf(stderr, "ERROR: no file to instrument, exiting.");
        exit(0);
    }
    // unique requires things to be sorted, even though this doesn't really make sense at this point
    std::sort(inst_locs.begin(), inst_locs.end(), comp_inst_loc);

    // sometimes pre-declarations cause duplicates, yeet them
    auto new_end = std::unique(inst_locs.begin(), inst_locs.end(), eq_inst_loc);
    inst_locs.erase(new_end, inst_locs.end());

    // sort on filename excluding path
    std::sort(files_to_go.begin(), files_to_go.end(), [&](std::string s1, std::string s2) {
        std::string temp1, temp2;
        // handle s1
        if (s1.find("/") != std::string::npos)
        {
            temp1 = s1.substr(s1.find_last_of("/"));
        }
        else
        {
            temp1 = s1;
        }

        // handle s2
        if (s2.find("/") != std::string::npos)
        {
            temp2 = s2.substr(s2.find_last_of("/"));
        }
        else
        {
            temp2 = s2;
        }

        return temp1 < temp2;
    });

    auto new_end2 = std::unique(files_to_go.begin(), files_to_go.end(), [&](std::string s1, std::string s2) {
        std::string temp1, temp2;
        temp1 = s1;
        temp2 = s2;

        if (temp1.substr(0, 2) == "./")
        {
            temp1.erase(0, 2);
        }

        if (temp2.substr(0, 2) == "./")
        {
            temp2.erase(0, 2);
        }

        return (temp1.find(temp2) != std::string::npos || temp2.find(temp1) != std::string::npos);
    });

    files_to_go.erase(new_end2, files_to_go.end());
}

int main(int argc, const char **argv)
{
    int new_argc = argc;
    char **new_argv = addHeadersToCommand(&new_argc, argv);

    //Get source paths
    auto ExpectedParser =
        tooling::CommonOptionsParser::create(new_argc, (const char **)new_argv, MyToolCategory, llvm::cl::OneOrMore,
                                             "Tool for adding TAU instrumentation to source files.\nNote that this "
                                             "will only instrument the first source file given.");

    if (!ExpectedParser)
    {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }

    tooling::CommonOptionsParser &OptionsParser = ExpectedParser.get();
    instrumentor CodeInstrumentor;
    CodeInstrumentor.Tool = new tooling::ClangTool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
    CodeInstrumentor.set_exec_name(argv[0]);

    inst_inline = do_inline;

    if (!selectfile.empty())
    {
        processInstrumentationRequests(selectfile.c_str());
    }

    CodeInstrumentor.run_tool();

    CodeInstrumentor.instr_request(excludelist, false); // Emit selective instrumentation requests

    findFiles(OptionsParser.getSourcePathList(), CodeInstrumentor); //Locate source files and mark for instrumentation/skipping

    CodeInstrumentor.instrument();
    
}
