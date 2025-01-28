#ifndef SELECTFILE_H
#define SELECTFILE_H

#include <list>

#define BEGIN_EXCLUDE_TOKEN      "BEGIN_EXCLUDE_LIST"
#define END_EXCLUDE_TOKEN        "END_EXCLUDE_LIST"
#define BEGIN_INCLUDE_TOKEN      "BEGIN_INCLUDE_LIST"
#define END_INCLUDE_TOKEN        "END_INCLUDE_LIST"
#define BEGIN_FILE_INCLUDE_TOKEN "BEGIN_FILE_INCLUDE_LIST"
#define END_FILE_INCLUDE_TOKEN   "END_FILE_INCLUDE_LIST"
#define BEGIN_FILE_EXCLUDE_TOKEN "BEGIN_FILE_EXCLUDE_LIST"
#define END_FILE_EXCLUDE_TOKEN   "END_FILE_EXCLUDE_LIST"
#define BEGIN_INSTRUMENT_SECTION "BEGIN_INSTRUMENT_SECTION"
#define END_INSTRUMENT_SECTION	 "END_INSTRUMENT_SECTION"

#define INBUF_SIZE 65536

#define WSPACE(line) while ( line[0] == ' ' || line[0] == '\t')  \
    { \
      if (line[0] == '\0') parseError("EOL found", line, lineno, line - original);  \
      line++;  \
    }

#define TOKEN(k) if (line[0] != k || line[0] == '\0') parseError("token not found", line, lineno, (int ) (line - original)); \
		 else line++;

#define RETRIEVESTRING(pname, line) i = 0; \
       while (line[0] != '"') { \
       if (line [0] == '\0') parseError("EOL", line, lineno, line - original); \
         pname[i++] = line[0]; line++; \
       } \
       pname[i] = '\0';  \
       line++; /* found closing " */

#define RETRIEVENUMBER(pname, line) i = 0; \
  while (line[0] != ' ' && line[0] != '\t' ) { \
  if (line [0] == '\0') parseError("EOL", line, lineno, line - original); \
    pname[i++] = line[0]; line++; \
  } \
  pname[i] = '\0';  \
  line++; /* found closing " */

#define RETRIEVECODE(pname, line) i = 0; \
  while (line[0] != '"') { \
    if (line [0] == '\0') parseError("EOL", line, lineno, line - original); \
    if (line[0] == '\\') { \
      switch(line[1]) { \
        case '\\': \
        case '"': \
          break; \
        case 'n': \
          line[1] = '\n'; \
          break; \
        case 't': \
          line[1] = '\t'; \
          break; \
        default: \
          parseError("Unknown escape sequence", line, lineno, line - original); \
          break; \
      } \
      line++; \
    } \
    pname[i++] = line[0]; line++; \
  } \
  pname[i] = '\0';  \
  line++; /* found closing " */

extern std::list<std::string> excludelist;
extern std::list<std::string> includelist;
extern std::list<std::string> fileincludelist;
extern std::list<std::string> fileexcludelist;

// void parseInstrumentationCommand(char *line, int lineno);
bool processInstrumentationRequests(const char *fname);

#endif
