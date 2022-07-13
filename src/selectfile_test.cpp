#include <string>
#include "instrumentor.hpp"
#include "selectfile.hpp"

std::vector<inst_loc*> inst_locations;

const char* loc_typ_strs[NUM_LOC_TYPES] =
  {
    "begin func",
    "return",
    "multiline return",
    "exit"
  };

void dump_inst_loc(inst_loc* loc) {
  printf("\tLine:         %d\n", loc->line);
  printf("\tCol:          %d\n", loc->col);
  printf("\tKind:         %s\n", loc_typ_strs[loc->kind]);
  printf("\tRet type:     %s\n", loc->return_type);
  printf("\tName:         %s\n", loc->func_name);
  printf("\tTau:          %s\n", loc->full_tau_name);
  printf("\tHas args:     %s\n", loc->has_args ? "Yes" : "No");
  printf("\tIs ret ptr:   %s\n", loc->is_return_ptr ? "Yes" : "No");
  printf("\tNeeds move:   %s\n", loc->needs_move ? "Yes" : "No");
}

void dump_inst_loc(inst_loc* loc, int n) {
  printf("location %d\n", n);
  dump_inst_loc(loc);
}

void dump_inst_loc(inst_loc* loc, int n, bool (*filter)(inst_loc*)) {
  if (filter(loc)) {
    printf("location %d\n", n);
    dump_inst_loc(loc);
  }
}

void dump_all_locs() {
  for (int i = 0; i < inst_locations.size(); i++) {
    dump_inst_loc(inst_locations[i], i);
  }
}

void dump_all_locs(bool (*filter)(inst_loc*)) {
  for (int i = 0; i < inst_locations.size(); i++) {
    dump_inst_loc(inst_locations[i], i, filter);
  }
}

int main(int argc, const char** argv) {

  std::string selectfile;
  if (argc > 1){
    selectfile = std::string(argv[1]);
  }
  else {
    printf("no args\n");
    return 0;
  }

  if (!selectfile.empty()) {
    processInstrumentationRequests(selectfile.c_str());
  }

  dump_all_locs();
}
