#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <clang-c/Index.h>
#include "llvm/Support/CommandLine.h"
using namespace std;

// make sure begin func comes before returns and such
#define BEGIN_FUNC 0
#define RETURN_FUNC 1
#define MULTILINE_RETURN_FUNC 2
#define EXIT_FUNC 3
#define NUM_LOC_TYPES 4

char* loc_typ_strs[NUM_LOC_TYPES] =
  {
    "begin func",
    "return",
    "multiline return",
    "exit"
  };

typedef struct inst_loc {
  int line = -1;
  int col = -1;
  int kind = -1;
  CXType return_type;
  const char* func_name;
  const char* full_tau_name;
  bool has_args = false;
} inst_loc;

std::string current_file;

bool comp_inst_loc (inst_loc* first, inst_loc* second) {
  if (first->line != second->line) {
    return first->line < second->line;
  }
  else if (first->col != second->col) {
    return first->col < second->col;
  }
  else { // SOME PEOPLE have functions that are just {} so we need to make sure begin comes before return
    return first->kind < second->kind;
  }
}

std::vector<inst_loc*> inst_locations;

void dump_inst_loc(inst_loc* loc) {
  printf("\tLine:     %d\n", loc->line);
  printf("\tCol:      %d\n", loc->col);
  printf("\tKind:     %s\n", loc_typ_strs[loc->kind]);
  printf("\tRet type: %s\n", clang_getCString(clang_getTypeSpelling(loc->return_type)));
  printf("\tName:     %s\n", loc->func_name);
  printf("\tTau:      %s\n", loc->full_tau_name);
  printf("\tHas args: %s\n", loc->has_args ? "Yes" : "No");
}

void dump_inst_loc(inst_loc* loc, int n) {
  printf("location %d\n", n);
  dump_inst_loc(loc);
}

void dump_all_locs() {
  for (int i = 0; i < inst_locations.size(); i++) {
    dump_inst_loc(inst_locations[i], i);
  }
}

// dummy test function for parsing things
int hello() {
	{
	printf("hello\n");
	}
	{{
  return 42;
	}}
}

ostream& operator<<(ostream& stream, const CXString& str)
{
  stream << clang_getCString(str);
  clang_disposeString(str);
  return stream;
}

// assumes funcdecl is CXCursor_FunctionDecl
void makeFuncAndTauNames(CXCursor funcdecl, std::string& func_name, std::string& tau_name) {
  CXSourceRange extent = clang_getCursorExtent(funcdecl);
  CXSourceLocation start_loc = clang_getRangeStart(extent);
  CXSourceLocation end_loc = clang_getRangeEnd(extent);

  unsigned int start_line;
  unsigned int start_col;
  unsigned int end_line;
  unsigned int end_col;

  // for compount stmts this *is* the L/R brace location, yay
  clang_getSpellingLocation(start_loc, nullptr, &start_line, &start_col, nullptr );
  clang_getSpellingLocation(end_loc, nullptr, &end_line, &end_col, nullptr );

  std::string sig = clang_getCString(clang_getTypeSpelling(clang_getCursorType(funcdecl)));
  func_name = clang_getCString(clang_getCursorSpelling(funcdecl));
  sig.insert(sig.find_first_of("("), func_name);

  CXLanguageKind lang = clang_getCursorLanguage(funcdecl);
  std::string lang_string;
  if (lang == CXLanguage_C) {
    lang_string = "C";
  }
  else if (lang == CXLanguage_CPlusPlus) {
    lang_string = "C++";
  }
  else {
    lang_string = "invalid";
  }

  tau_name = sig + " " + lang_string + " [{" + current_file + "} {"
    + to_string(start_line) + "," + to_string(start_col) + "}-{"
    + to_string(end_line) + "," + to_string(end_col-1) + "}]";
}

void handleFuncStartEnd(CXCursor c, CXCursor parent) {
  // do things with functions: https://bastian.rieck.me/blog/posts/2016/baby_steps_libclang_function_extents/
  CXSourceRange extent = clang_getCursorExtent(c);
  CXSourceLocation start_loc = clang_getRangeStart(extent);
  CXSourceLocation end_loc = clang_getRangeEnd(extent);

  unsigned int start_line;
  unsigned int start_col;
  unsigned int end_line;
  unsigned int end_col;

  // for compount stmts this *is* the L/R brace location, yay
  clang_getSpellingLocation(start_loc, nullptr, &start_line, &start_col, nullptr );
  clang_getSpellingLocation(end_loc, nullptr, &end_line, &end_col, nullptr );

  std::string func_name;
  std::string tau_name;
  makeFuncAndTauNames(parent, func_name, tau_name);

  char* func_name_c = new char[func_name.length()+1];
  std::strcpy(func_name_c, func_name.c_str());

  char* tau_name_c = new char[tau_name.length()+1];
  std::strcpy(tau_name_c, tau_name.c_str());

  // the cols are 1-indexed, but the strings of each line are 0-indexed
  // the given start_col is the index of the open brace, so to go 1 after the char at the start
  // inst location, start_col is fine
  // but end_col is *one after* the close brace, so to go one before the char at the end inst
  // char, we need to do end_col - 2

  inst_loc* start = new inst_loc;
  start->line = start_line;
  start->col = start_col;
  start->kind = BEGIN_FUNC;
  start->return_type = clang_getResultType(clang_getCursorType(parent));
  start->func_name = func_name_c;
  start->full_tau_name = tau_name_c;
  start->has_args = clang_Cursor_getNumArguments(parent) > 0;

  inst_locations.push_back(start);

  inst_loc* end = new inst_loc;
  end->line = end_line;
  end->col = end_col - 2;
  end->kind = RETURN_FUNC;
  end->return_type = clang_getResultType(clang_getCursorType(parent));
  end->func_name = func_name_c;
  end->full_tau_name = tau_name_c;
  end->has_args = clang_Cursor_getNumArguments(parent) > 0;

  inst_locations.push_back(end);

  // cout << "Cursor '" << clang_getCursorSpelling(c)
  //   << "' of kind '"
  //   << clang_getCursorKindSpelling(clang_getCursorKind(c)) << "'\n";
  // cout << "\tStart " << start_line << "," << start_col
  //   << "; End " << end_line << "," << end_col << "\n";
}

void handleReturn(CXCursor c, CXCursor parent, CXCursor encl_function) {
  CXSourceRange extent = clang_getCursorExtent(c);
  CXSourceLocation start_loc = clang_getRangeStart(extent);
  CXSourceLocation end_loc = clang_getRangeEnd(extent);

  unsigned int start_line;
  unsigned int start_col;
  unsigned int end_line;
  unsigned int end_col;

  // for compount stmts this *is* the L/R brace location, yay
  clang_getSpellingLocation(start_loc, nullptr, &start_line, &start_col, nullptr );
  clang_getSpellingLocation(end_loc, nullptr, &end_line, &end_col, nullptr );

  printf("return start %d:%d\n", start_line, start_col);
  printf("return end   %d:%d\n", end_line, end_col);

  CXSourceLocation return_loc = clang_getCursorLocation(c);

  unsigned int ret_line = 0;
  unsigned int ret_col = 0;

  clang_getSpellingLocation(return_loc, nullptr, &ret_line, &ret_col, nullptr );

  std::string func_name;
  std::string tau_name;
  makeFuncAndTauNames(encl_function, func_name, tau_name);

  char* func_name_c = new char[func_name.length()+1];
  std::strcpy(func_name_c, func_name.c_str());

  char* tau_name_c = new char[tau_name.length()+1];
  std::strcpy(tau_name_c, tau_name.c_str());

  printf("func type %s\n", clang_getCString(clang_getTypeSpelling(clang_getCursorType(encl_function))));
  printf("returns a %s\n", clang_getCString(clang_getTypeSpelling(clang_getResultType(clang_getCursorType(encl_function)))));

  // need to do col - 1 because cols are 1-indexed and line string is 0-indexed
  inst_loc* ret = new inst_loc;
  ret->line = ret_line;
  ret->col = ret_col - 1;
  ret->kind = start_line == end_line ? RETURN_FUNC : MULTILINE_RETURN_FUNC;
  ret->func_name = func_name_c;
  ret->return_type = clang_getResultType(clang_getCursorType(encl_function));
  ret->full_tau_name = tau_name_c;
  ret->has_args = clang_Cursor_getNumArguments(encl_function) > 0;

  inst_locations.push_back(ret);

  std::cout << "Parent: '" << clang_getCursorSpelling(encl_function)
    << "' of kind " << clang_getCursorKindSpelling(clang_getCursorKind(encl_function)) <<"\n";
}

CXChildVisitResult traverse(CXCursor c, CXCursor parent, CXClientData client_data)
{
	CXSourceLocation location = clang_getCursorLocation(c);
	// ignore anything that isn't in this file
  if(clang_Location_isFromMainFile(location) == 0) {
    return CXChildVisit_Continue;
	}

  // cout << "Cursor '" << clang_getCursorSpelling(c)
  //   << "' of kind '"
  //   << clang_getCursorKindSpelling(clang_getCursorKind(c)) << "'\n";

  CXCursorKind kind = clang_getCursorKind(c);

  // if (kind == CXCursorKind::CXCursor_FunctionDecl ||         // what it sounds like
  //    kind == CXCursorKind::CXCursor_CXXMethod ||            // also what it sounds like
  //    kind == CXCursorKind::CXCursor_FunctionTemplate ||     // CXX templates
  //    kind == CXCursorKind::CXCursor_Constructor ||          // constructors and destructors
  //    kind == CXCursorKind::CXCursor_Destructor ||
  //    kind == CXCursorKind::CXCursor_ConversionFunction) {   // operator overloads, strangely (node name CXXConversion)
  //     printf("setting client data\n");
  //     client_data = &c;
  // }
  //
  // CXCursorKind parent_kind = clang_getCursorKind(parent);
  // if (parent_kind == CXCursorKind::CXCursor_FunctionDecl ||
  //    parent_kind == CXCursorKind::CXCursor_CXXMethod ||
  //    parent_kind == CXCursorKind::CXCursor_FunctionTemplate ||
  //    parent_kind == CXCursorKind::CXCursor_Constructor ||
  //    parent_kind == CXCursorKind::CXCursor_Destructor ||
  //    parent_kind == CXCursorKind::CXCursor_ConversionFunction) {
  //   printf("parent kind is function\n");
  //   if (kind == CXCursorKind::CXCursor_CompoundStmt) {
  //     printf("handling function\n");
  //     handleFuncStartEnd(c, parent);
  //   }
  // }
  //
  // if (kind == CXCursorKind::CXCursor_ReturnStmt) {
  //   printf("handling return\n");
  //   CXCursor encl_function = *(reinterpret_cast<CXCursor*>(client_data));
  //   handleReturn(c, parent, encl_function);
  // }
  //
  // // ignore anything inside a lambda
  // //TODO: setting to profile lambdas too
  // if (kind == CXCursorKind::CXCursor_LambdaExpr) {
  //   printf("ignoring lambda\n");
  //   return CXChildVisit_Continue;
  // }

  unsigned int curLevel  = *( reinterpret_cast<unsigned int*>( client_data ) );
  unsigned int nextLevel = curLevel + 1;

  std::cout << std::string( curLevel, '-' ) << " " << clang_getCString(clang_getCursorKindSpelling(clang_getCursorKind(c)))
    << " (" << clang_getCString(clang_getCursorSpelling(c)) << ")\n";

  clang_visitChildren(c, traverse, &nextLevel);

	return CXChildVisit_Continue;
}

void make_begin_func_code(inst_loc* loc, std::string& code) {
  code += "\tTAU_PROFILE_TIMER(tautimer, \"";
  code += loc->full_tau_name;
  code += "\", \" \", ";
  code += strcmp(loc->func_name, "main") == 0 ? "TAU_DEFAULT" : "TAU_USER";
  code += ");\n";
  if (strcmp(loc->func_name, "main") == 0 && loc->has_args) {
    code += "\tTAU_INIT(&argc, &argv);\n";
  }

	// not needed
  //code += "#ifndef TAU_MPI\n";
  //code += "#ifndef TAU_SHMEM\n";
  //code += "  TAU_PROFILE_SET_NODE(0);\n";
  //code += "#endif /* TAU_SHMEM */\n";
  //code += "#endif /* TAU_MPI */\n"; // set node 0
  code += "\tTAU_PROFILE_START(tautimer);\n";

  //code += "{\n"; //TODO is this needed? if yes, matching close bracket before end func
}

void make_begin_func_code_cxx(inst_loc* loc, std::string& code) {
  code += "\tTAU_PROFILE(\"";
  code += loc->full_tau_name;
  code += "\", \" \", ";
  code += strcmp(loc->func_name, "main") == 0 ? "TAU_DEFAULT" : "TAU_USER";
  code += ");\n";
  if (strcmp(loc->func_name, "main") == 0 && loc->has_args) {
    code += "\tTAU_INIT(&argc, &argv);\n";
  }
}

bool make_end_func_code(inst_loc* loc, std::string& code, std::string& line) {
  // void is easy, just drop in the stop
  // also do this if the line does not actually contain "return"; eg, int main() with no explicit return
  if (loc->return_type.kind == CXType_Void || line.find("return") == std::string::npos) {
    code += "\tTAU_PROFILE_STOP(tautimer);\n";
    return false;
  }
  // types are harder, need to pull the arg to return before the stop in case it does things
  else {
    code += "\t{ ";
    code += clang_getCString(clang_getTypeSpelling(loc->return_type));
    code += " tau_ret_val = ";
    int first_pos = line.find("return") + 6;
    int last_pos = line.find(";", first_pos);
    code += line.substr(first_pos, last_pos);
    code += " TAU_PROFILE_STOP(tautimer); return tau_ret_val; }\n";
    return true;
  }

}

void instrument_file(ifstream& og_file, ofstream& inst_file, bool use_cxx_api) {
  std::string line;
  int lineno = 0;
  // if (inst_locations.size() == 0) {
  //   printf("No instrumentation locations for this file!");
  //   return;
  // }
  auto inst_loc_iter = inst_locations.begin();

  inst_file << "#include <Profile/Profiler.h>\n";
  inst_file << "#line 1 \"" << current_file << "\"\n";

  while (getline(og_file, line)) {
    lineno++;
    // short circuit if we run out of inst locations to avoid segfaults :)
    // need the if and the while because sometimes 2+ inst locations are on the same line
    if (inst_loc_iter != inst_locations.end()) {
      inst_loc* curr_inst_loc = *inst_loc_iter;
      if (lineno == curr_inst_loc->line) {
        curr_inst_loc = *inst_loc_iter;
        int num_inst_locs_this_line = 0;
        bool should_skip_printing_next_line = false;
        std::string start = line.substr(0,curr_inst_loc->col);
        std::string end;
        while (inst_loc_iter != inst_locations.end() && lineno == curr_inst_loc->line) {
          if (curr_inst_loc->col < line.size()) {
            end = line.substr(curr_inst_loc->col);
          }
          else {
            end = "";
          }
          if (num_inst_locs_this_line == 0) {
            inst_file << start;
          }
          std::string inst_code;
          switch (curr_inst_loc->kind) {
            case BEGIN_FUNC:
              inst_file << "\n#line " << lineno << "\n";
              if (use_cxx_api) {
                make_begin_func_code_cxx(curr_inst_loc, inst_code);
              }
              else {
                make_begin_func_code(curr_inst_loc, inst_code);
              }
              inst_file << inst_code;
              inst_file << "#line " << lineno << "\n";
              break;
            case RETURN_FUNC:
              if (!use_cxx_api) {
                inst_file << "\n#line " << lineno << "\n";
                should_skip_printing_next_line = make_end_func_code(curr_inst_loc, inst_code, line);
                inst_file << inst_code;
                inst_file << "#line " << lineno << "\n";
              }
              break;
            case MULTILINE_RETURN_FUNC:
              if (!use_cxx_api) {
                inst_file << "\n#line " << lineno << "\n";
                while (line.find(";") == string::npos) {
                  std::string templine;
                  getline(og_file, templine);
                  line += templine;
                  lineno++;
                }
                should_skip_printing_next_line = make_end_func_code(curr_inst_loc, inst_code, line);
                inst_file << inst_code;
                inst_file << "#line " << lineno << "\n";
              }
            default:
              break;
          }

          inst_loc_iter++;
          curr_inst_loc = *inst_loc_iter;
          num_inst_locs_this_line++;
        }
        if (!should_skip_printing_next_line) {
          inst_file << end << "\n";
        }
      }
      else {
        inst_file << line << "\n";
      }
    }
    else {
      inst_file << line << "\n";
    }
  }
}

llvm::cl::opt<string> outputfile("tau_output", llvm::cl::desc("Specify name of output instrumented file"), llvm::cl::value_desc("filename"));
llvm::cl::opt<string> inputfile(llvm::cl::Positional, llvm::cl::desc("<input_file>"), llvm::cl::Required, llvm::cl::init("-"));
llvm::cl::opt<string> compileargs("compile_flags", llvm::cl::desc("Compilation flags (DO NOT include input file name)"), llvm::cl::value_desc("\"-arg1 -arg2 ...\""));
llvm::cl::opt<bool> use_cxx_api("use_cxx_api", llvm::cl::desc("Use TAU's C++ instrumentation API"), llvm::cl::init(false));

int main(int argc, char* argv[])
{
  llvm::cl::ParseCommandLineOptions(argc, argv);
  current_file = inputfile;
  CXIndex index = clang_createIndex(1, 1);
  size_t pos = 0;
  std::vector<string> compile_flags_vec;
  while ((pos = compileargs.find(" ")) != std::string::npos) {
      compile_flags_vec.push_back(compileargs.substr(0, pos));
      compileargs.erase(0, pos + 1);
  }
  compile_flags_vec.push_back(compileargs);
  const char* compile_flags[compile_flags_vec.size()];
  for (int i = 0; i < compile_flags_vec.size(); i++) {
    compile_flags[i] = compile_flags_vec[i].c_str();
    cout << compile_flags[i] << "\n";
  }
  // const char* args[] = {"-lclang", "-g", "-I/home/alisterj/.local/include"};
  CXTranslationUnit unit = clang_parseTranslationUnit(
    index,
    inputfile.c_str(), compile_flags, compile_flags_vec.size(),
    nullptr, 0,
    CXTranslationUnit_KeepGoing
    | CXTranslationUnit_RetainExcludedConditionalBlocks
    | CXTranslationUnit_IncludeAttributedTypes
    | CXTranslationUnit_VisitImplicitAttributes
    | CXTranslationUnit_IgnoreNonErrorsFromIncludedFiles );

  if (unit == nullptr)
  {
    cerr << "Unable to parse translation unit. Quitting." << endl;
    exit(-1);
  }

  CXCursor cursor = clang_getTranslationUnitCursor(unit);

  unsigned int level = 0;
  clang_visitChildren(cursor, traverse, &level);

  //sort by line numbers then cols so looping goes well
  std::sort(inst_locations.begin(), inst_locations.end(), comp_inst_loc);

  dump_all_locs();

  std::ifstream og_file;
  std::ofstream inst_file;
  std::string newname = inputfile;
  if (!outputfile.empty()) {
    newname = outputfile;
  }
  else {
    newname.insert(newname.find_last_of("."), ".inst");
  }
  printf("new filename: %s\n", newname.c_str());

  inst_file.open(newname);
  og_file.open(inputfile);

  instrument_file(og_file, inst_file, use_cxx_api);
  og_file.close();
  inst_file.close();

  clang_disposeTranslationUnit(unit);
  clang_disposeIndex(index);
}

// TODO
  // test on DCA
  // selective instrumentation



// for grid???
// #!/bin/bash
//
// module load cuda/10.2
// module load gcc/8.1
// module list
//
// make distclean
// ./configure CXX="nvcc -ccbin=g++"
// export NVCC_APPEND_FLAGS="-Xcompiler -finstrument-functions -Xcompiler -finstrument-functions-exclude-file-list=.h,.hpp,include"
