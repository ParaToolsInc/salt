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

// Internal type for tracking functions, stores instrumentation locations and function body
class function {
public:
    std::string name; // metadata etc..

    std::string text; // Function text

    std::vector<inst_loc*> inst_locations;

    unsigned int baseLineNo;

    function(std::string nm, std::string text, std::vector<inst_loc*>);

    function();

};

// Class used for storing parts of files as either functions or text, using union to express variant types
struct fileElement {

    enum{TEXT, FUNC} tag;

    unsigned start_line;
    unsigned start_col;
    unsigned end_line;
    unsigned end_col;

    union
    {
        std::string* s;
        function* f;
    };

    fileElement(std::string* txt, unsigned s_line, unsigned s_col, unsigned e_line, unsigned e_col)
        : start_line(s_line), start_col(s_col), end_line(e_line), end_col(e_col) {
        tag = TEXT;
        s = txt;
    }

    fileElement(function* fnc, unsigned s_line, unsigned s_col, unsigned e_line, unsigned e_col)
        : start_line(s_line), start_col(s_col), end_line(e_line), end_col(e_col) {
        tag = FUNC;
        f = fnc;
    }

};

// Stores complete structure of files
class file {
public:
    std::string name;
    std::vector <fileElement> elements;

    void emit(std::string fname);
};

// Instrumentor class: Each instance can be passed files, instrumentation requests, and configuration files, then 
class instrumentor {
public:

    bool use_cxx_api = false;


    std::vector<std::string> files_to_go;
    std::vector<std::string> files_skipped;
    bool inst_inline = false;

    ryml::Tree yaml_tree;
    std::list<std::string> excludelist;
    std::list<std::string> includelist;
    std::list<std::string> fileincludelist;
    std::list<std::string> fileexcludelist;

    std::set<std::string> file_set;

    std::map<std::string, file*> fileMap;
    char* exec_name;

    // Parses source files into internal structure
    void parse_files(const clang::tooling::CompilationDatabase &Compilations, llvm::ArrayRef< std::string > SourcePaths);

    // Finds files from source paths that require no instrumentation, but must produce a .inst version
    void findFiles(const std::vector<std::string>& files);

    // Generates instrumentation information from file
    void processInstrumentationRequests(const char *fname); // Configuration file and selective instrumentation are specific to the instrumentor object

    // Applies instrumentation information from `processInstrumentationRequests` to the currently tracked files
    void apply_selective_instrumentation();

    // Sets executable name for timer names
    void set_exec_name(const char* name);
    
    // Adds .yaml configuration file to instrumentation plan
    void configure(const char* configuration_file);

    // Instruments all files that have been parsed, emits all required .inst files
    void instrument();


private:
    std::string instrument_func(function& f, int lineno);
    
    void instrument_file(file* f);

};
