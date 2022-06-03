#ifndef TOOLING_H
#define TOOLING_H

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

extern const char* loc_typ_strs[NUM_LOC_TYPES];

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


extern std::vector<inst_loc*> all_inst_locations;

extern bool inst_inline;

#endif
