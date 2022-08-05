#ifndef TOOLING_H
#define TOOLING_H

#include <ryml_all.hpp>
#include "clang/Tooling/Tooling.h"
#include <vector>

/* defines */
#ifdef TAU_WINDOWS
#define TAU_DIR_CHARACTER '\\'
#else
#define TAU_DIR_CHARACTER '/'
#endif /* TAU_WINDOWS */

// make sure begin func comes before returns and such
#define BEGIN_FUNC 0
#define RETURN_FUNC 1
#define MULTILINE_RETURN_FUNC 2
#define EXIT_FUNC 3
#define NUM_LOC_TYPES 4


static llvm::cl::OptionCategory MyToolCategory(
    "TAU instrumentor options",
    "To pass options directly to the compiler, use\n\ttooling [options] <source> -- [compiler options]");

extern llvm::cl::opt<std::string> outputfile;

extern llvm::cl::opt<bool> use_cxx_api;

extern llvm::cl::opt<bool> do_inline;

extern llvm::cl::opt<std::string> selectfile;

typedef struct inst_loc {
    int line = -1;
    int col = -1;
    int kind = -1;
    const char* return_type;
    const char* func_name;
    const char* full_timer_name;
    bool has_args = false;
    bool is_return_ptr = false;
    bool needs_move = false;
    bool skip = false;
} inst_loc;

#endif

// Utility functions required in the frontend too
bool comp_inst_loc(inst_loc *first, inst_loc *second); // only works for inst_locs that are in the same file
bool eq_inst_loc(inst_loc *first, inst_loc *second);
bool check_file_against_list(std::list<std::string> list, std::string fname);

// static std::vector<inst_loc*> inst_locs;
static std::vector<std::string> files_to_go;
static std::vector<std::string> files_skipped;
static bool inst_inline = false;

// class instrumentor {
// public:

//     clang::tooling::ClangTool* Tool = nullptr;
//     char* exec_name;
//     std::set<std::string> file_set;

//     instrumentor();

//     void set_exec_name(const char* name);

//     void run_tool();

//     // Handles a list of instrumentation locations to be included (include=true) or excluded (include=false)
//     void instr_request(std::list<std::string> list, bool include);

//     void instrument_file(std::ifstream &og_file, std::ofstream &inst_file, std::string filename,
//                      std::vector<inst_loc *> inst_locations, bool use_cxx_api, ryml::Tree yaml_tree);

//     void instrument();
// };


class function {
public:
    std::string name; // metadata etc..

    std::string text; // Function text

    std::vector<inst_loc*> inst_locations;

    function(std::string nm, std::string text, std::vector<inst_loc*>);
};

struct fileElement {

    enum{TEXT, FUNC} tag;
    union
    {
        std::string* s;
        function* f;
    };

    fileElement(std::string* txt) {
        tag = TEXT;
        s = txt;
    }

    fileElement(function* fnc) {
        tag = FUNC;
        f = fnc;
    }

};

class file {
public:
    std::string name;
    std::vector <fileElement> elements;

    void emit(std::string fname); // TODO
};

class instrumentor {
public:

    bool use_cxx_api = false;

    ryml::Tree yaml_tree;
    std::list<std::string> excludelist;
    std::list<std::string> includelist;
    std::list<std::string> fileincludelist;
    std::list<std::string> fileexcludelist;

    std::set<std::string> file_set;

    std::vector<file*> files;
    char* exec_name;

    void parse_files(const clang::tooling::CompilationDatabase &Compilations, llvm::ArrayRef< std::string > SourcePaths);

    void processInstrumentationRequests(const char *fname); // Configuration file and selective instrumentation are specific to the instrumentor object

    void apply_selective_instrumentation();

    void set_exec_name(const char* name);
    
    void configure(const char* configuration_file);

    std::string instrument_func(function& f);
    
    void instrument_file(file* f);

    void instrument();

};