#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>

#include "selectfile.hpp"
#include "tooling.hpp"
#include "dprint.hpp"

std::list<std::string> excludelist;
std::list<std::string> includelist;
std::list<std::string> fileincludelist;
std::list<std::string> fileexcludelist;

void dump_list(std::list<std::string> l) {
  for (std::string s : l) {
    DPRINT("%s\n", s.c_str());
  }
}

char *trimwhitespace(char *str)
{
  char *end;

  // Trim leading space
  while (isspace(*str))
    str++;

  if (*str == 0)    // All spaces?
    return str;

  // Trim trailing space
  end = str + strlen(str) - 1;
  while (end > str && isspace(*end))
    end--;

  // Write new null terminator
  *(end + 1) = 0;

  return str;
}

void parseError(const char *message, char *line, int lineno, int column)
{
  fprintf(stderr,
    "ERROR: %s: parse error at selective instrumentation file line %d col %d\n",
    message, lineno, column);
  fprintf(stderr, "line=%s\n", line);
  exit(0);
}

///////////////////////////////////////////////////////////////////////////
// parseInstrumentationCommand
// input: line -  character string containing a line of text from the selective
// instrumentation file
// input: lineno - integer line no. (for reporting parse errors if any)
//
///////////////////////////////////////////////////////////////////////////
// void parseInstrumentationCommand(char *line, int lineno)
// {
//   char *original;
//   int i, ret, value;
//   bool filespecified = false;
//   bool phasespecified = false;
//   bool timerspecified = false;
//   itemQualifier_t qualifier = STATIC;
//   bool staticspecified = false;
//   bool dynamicspecified = false;
//   char pname[INBUF_SIZE]; /* parsed name */
//   char pfile[INBUF_SIZE]; /* parsed filename */
//   char plineno[INBUF_SIZE]; /* parsed lineno */
//   char pcode[INBUF_SIZE]; /* parsed code */
//   char plang[INBUF_SIZE]; /* parsed language */
//   char plevel[INBUF_SIZE]; /* parsed loop level */
//   int language = tauInstrument::LA_ANY;
//   int startlineno, stoplineno;
//   int level = 1; // Default loop instrumentation level
//
//   startlineno = stoplineno = 0;
//   instrumentKind_t kind = TAU_NOT_SPECIFIED;
//
//   DEBUG_MSG("Inside parseInstrumentationCommand: line %s lineno: %d\n", line, lineno);
//
//   original = line;
//   line = trimwhitespace(line);
//
//   /* parse: file = "foo.cc" line = 245 code = "TAU_NODE(0);" lang = "c++" */
//   if (strncmp(line, "file", 4) == 0) {
//     DEBUG_MSG("Found FILE!\n");
//
//     line += 4;
//     WSPACE(line);
//     TOKEN('=');
//     WSPACE(line);
//     TOKEN('"');
//     RETRIEVESTRING(pfile, line);
//     filespecified = true;
//     DEBUG_MSG("GOT name = %s\n", pfile);
//     WSPACE(line);
//     if (strncmp(line, "line", 4) == 0) { /* got line token, get line no. */
//       line += 4;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       RETRIEVENUMBER(plineno, line);
//       ret = sscanf(plineno, "%d", &value);
//       DEBUG_MSG("GOT line no = %d, line = %s\n", value, line);
//     } else {
//       parseError("<line> token not found", line, lineno, line - original);
//     }
//
//     WSPACE(line);
//     if (strncmp(line, "code", 4) == 0) {
//       line += 4;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVECODE(pcode, line);
//       DEBUG_MSG("GOT code = %s\n", pcode);
//     } else {
//       parseError("<code> token not found", line, lineno, line - original);
//     }
//
//     WSPACE(line);
//     if (strncmp(line, "lang", 4) == 0) {
//       line += 4;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(plang, line);
//       DEBUG_MSG("GOT lang = %s\n", plang);
//       language = parseLanguageString(plang);
//       if (language < 0) parseError("<lang> token invalid", line, lineno, line - original);
//     } DEBUG_MSG("file = %s, code = %s, line no = %d, language = %d\n", pfile, pcode, value, language);
//     instrumentList.push_back(new tauInstrument(string(pfile), value, string(pcode), TAU_LINE, language));
//
//     /* parse: entry routine = "foo()" code = "TAU_SET_NODE(0)" lang = "c" */
//   } else if (strncmp(line, "entry", 5) == 0) {
//     DEBUG_MSG("Found ENTRY!\n");
//
//     line += 5;
//     WSPACE(line);
//     if (strncmp(line, "file", 4) == 0) {
//       line += 4;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(pfile, line);
//       WSPACE(line);
//       filespecified = true;
//       DEBUG_MSG("GOT file = %s\n", pfile);
//     }
//     if (strncmp(line, "routine", 7) == 0) {
//       line += 7;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(pname, line);
//       WSPACE(line);
//       DEBUG_MSG("GOT routine = %s\n", pname);
//     } else {
//       strcpy(pname, "#");
//     }
//     if (strncmp(line, "code", 4) == 0) {
//       line += 4; /* move 4 spaces */
//       /* check for = <WSPACE> " */
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVECODE(pcode, line);
//       WSPACE(line);
//       DEBUG_MSG("GOT code = %s\n", pcode);
//     } else {
//       parseError("<code> token not found", line, lineno, line - original);
//     }
//     if (strncmp(line, "lang", 4) == 0) {
//       line += 4;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(plang, line);
//       WSPACE(line);
//       DEBUG_MSG("GOT lang = %s\n", plang);
//       language = parseLanguageString(plang);
//       if (language < 0) parseError("<lang> token invalid", line, lineno, line - original);
//     }
//
//     DEBUG_MSG("entry routine = %s, code = %s, lang = %d\n", pname, pcode, language);
//
//     if (filespecified) {
//       instrumentList.push_back(
//           new tauInstrument(string(pfile), string(pname), string(pcode), TAU_ROUTINE_ENTRY, language));
//     } else {
//       instrumentList.push_back(new tauInstrument(string(pname), string(pcode), true, TAU_ROUTINE_ENTRY, language));
//     } /* file and routine are both specified for entry */
//
//     /* parse: exit routine = "foo()" code = "bar()" lang = "c" */
//   } else if (strncmp(line, "exit", 4) == 0) {
//
//     line += 4;
//     WSPACE(line);
//     if (strncmp(line, "file", 4) == 0) {
//       line += 4;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(pfile, line);
//       WSPACE(line);
//       filespecified = true;
//       DEBUG_MSG("GOT file = %s\n", pfile);
//     }
//     if (strncmp(line, "routine", 7) == 0) {
//       line += 7;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(pname, line);
//       WSPACE(line);
//       DEBUG_MSG("GOT routine = %s\n", pname);
//     } else {
//       strcpy(pname, "#");
//     }
//     if (strncmp(line, "code", 4) == 0) {
//       line += 4;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVECODE(pcode, line);
//       WSPACE(line);
//       DEBUG_MSG("GOT code = %s\n", pcode);
//     } else {
//       parseError("<code> token not found", line, lineno, line - original);
//     }
//     if (strncmp(line, "lang", 4) == 0) {
//       line += 4;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(plang, line);
//       WSPACE(line);
//       DEBUG_MSG("GOT lang = %s\n", plang);
//       language = parseLanguageString(plang);
//       if (language < 0) parseError("<lang> token invalid", line, lineno, line - original);
//     }
//
//     DEBUG_MSG("exit routine = %s, code = %s, lang = %d\n", pname, pcode, language);
//
//     if (filespecified) {
//       instrumentList.push_back(
//           new tauInstrument(string(pfile), string(pname), string(pcode), TAU_ROUTINE_EXIT, language));
//     } else {
//       instrumentList.push_back(new tauInstrument(string(pname), string(pcode), true, TAU_ROUTINE_EXIT, language));
//     } /* file and routine are both specified for exit */
//
//     /* parse: abort routine = "foo()" code = "bar()" lang = "c" */
//   } else if (strncmp(line, "abort", 5) == 0) {
//     DEBUG_MSG("Found ABORT!\n");
//     line += 5;
//
//     WSPACE(line);
//     if (strncmp(line, "file", 4) == 0) {
//       line += 4;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(pfile, line);
//       WSPACE(line);
//       filespecified = true;
//       DEBUG_MSG("GOT file = %s\n", pfile);
//     }
//     if (strncmp(line, "routine", 7) == 0) {
//       line += 7;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(pname, line);
//       WSPACE(line);
//       DEBUG_MSG("GOT routine = %s\n", pname);
//     } else {
//       strcpy(pname, "#");
//     }
//     if (strncmp(line, "code", 4) == 0) {
//       line += 4;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVECODE(pcode, line);
//       WSPACE(line);
//       DEBUG_MSG("GOT code = %s\n", pcode);
//     } else {
//       parseError("<code> token not found", line, lineno, line - original);
//     }
//     if (strncmp(line, "lang", 4) == 0) {
//       line += 4;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(plang, line);
//       WSPACE(line);
//       DEBUG_MSG("GOT lang = %s\n", plang);
//       language = parseLanguageString(plang);
//       if (language < 0) parseError("<lang> token invalid", line, lineno, line - original);
//     }
//
//     DEBUG_MSG("entry routine = %s, code = %s, lang = %d\n", pname, pcode, language);
//
//     if (filespecified) {
//       instrumentList.push_back(new tauInstrument(string(pfile), string(pname), string(pcode), TAU_ABORT, language));
//     } else {
//       instrumentList.push_back(new tauInstrument(string(pname), string(pcode), true, TAU_ABORT, language));
//     } /* file and routine are both specified for abort */
//
//     /* parse: init code = "init();" lang = "c" */
//   } else if (strncmp(line, "init", 4) == 0) {
//     DEBUG_MSG("Found INIT!\n");
//     line += 4;
//
//     WSPACE(line);
//     if (strncmp(line, "code", 4) == 0) {
//       line += 4;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVECODE(pcode, line);
//       WSPACE(line);
//       DEBUG_MSG("GOT code = %s\n", pcode);
//     } else {
//       parseError("<code> token not found", line, lineno, line - original);
//     }
//     if (strncmp(line, "lang", 4) == 0) {
//       line += 4; /* move 4 spaces */
//       /* check for = <WSPACE> " */
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(plang, line);
//       WSPACE(line);
//       DEBUG_MSG("GOT lang = %s\n", plang);
//       language = parseLanguageString(plang);
//       if (language < 0) parseError("<lang> token invalid", line, lineno, line - original);
//     }
//
//     DEBUG_MSG("code = %s\n", pcode);
//
//     instrumentList.push_back(new tauInstrument(string(pcode), true, TAU_INIT, language));
//
//   } else if (strncmp(line, "decl", 4) == 0) {
//     DEBUG_MSG("Found DECL!\n");
//     line += 4;
//
//     WSPACE(line);
//     if (strncmp(line, "file", 4) == 0) {
//       line += 4;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(pfile, line);
//       WSPACE(line);
//       filespecified = true;
//
//       DEBUG_MSG("GOT file = %s\n", pfile);
//
//     }
//     if (strncmp(line, "routine", 7) == 0) {
//       line += 7;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(pname, line);
//       WSPACE(line);
//
//       DEBUG_MSG("GOT routine = %s\n", pname);
//
//     } else {
//       strcpy(pname, "#");
//     }
//     if (strncmp(line, "code", 4) == 0) {
//       line += 4;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVECODE(pcode, line);
//       WSPACE(line);
//       DEBUG_MSG("GOT code = %s\n", pcode);
//     } else {
//       parseError("<code> token not found", line, lineno, line - original);
//     }
//     if (strncmp(line, "lang", 4) == 0) {
//       line += 4;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(plang, line);
//       WSPACE(line);
//       DEBUG_MSG("GOT lang = %s\n", plang);
//       language = parseLanguageString(plang);
//       if (language < 0) parseError("<lang> token invalid", line, lineno, line - original);
//     }
//
//     DEBUG_MSG("decl routine = %s, code = %s, lang = %d\n", pname, pcode, language);
//
//     if (filespecified) {
//       instrumentList.push_back(
//           new tauInstrument(string(pfile), string(pname), string(pcode), TAU_ROUTINE_DECL, language));
//     } else {
//       instrumentList.push_back(
//           new tauInstrument(string(pname), string(pcode), true, TAU_ROUTINE_DECL, language));
//     } /* file and routine are both specified for entry */
//
//   } else if (strncmp(line, "loops", 5) == 0) {
//     kind = TAU_LOOPS;
//     line += 5;
//     DEBUG_MSG("GOT loops lineno = %d\n", lineno);
//   } else if (strncmp(line, "io", 2) == 0) {
//     kind = TAU_IO;
//     line += 2;
//     DEBUG_MSG("GOT io lineno = %d\n", lineno);
//   } else if (strncmp(line, "memory", 6) == 0) {
//     kind = TAU_MEMORY;
//     line += 6;
//     DEBUG_MSG("GOT memory lineno = %d\n", lineno);
//   } else if (strncmp(line, "forall", 6) == 0) {
//     kind = TAU_FORALL;
//     line += 6;
//     DEBUG_MSG("GOT forall lineno = %d\n", lineno);
//   } else if (strncmp(line, "barrier", 7) == 0) {
//     kind = TAU_BARRIER;
//     line += 7;
//     DEBUG_MSG("GOT barrier lineno = %d\n", lineno);
//   } else if (strncmp(line, "fence", 5) == 0) {
//     kind = TAU_FENCE;
//     line += 5;
//     DEBUG_MSG("GOT fence lineno = %d\n", lineno);
//   } else if (strncmp(line, "notify", 6) == 0) {
//     kind = TAU_NOTIFY;
//     line += 6;
//     DEBUG_MSG("GOT notify lineno = %d\n", lineno);
//   } else if(strncmp(line, "phase", 5) == 0) {
//     phasespecified = true;
//     kind = TAU_PHASE;
//     line += 5;
//     DEBUG_MSG("GOT phase lineno = %d\n", lineno);
//   } else if(strncmp(line, "timer", 5) == 0) {
//     timerspecified = true;
//     kind = TAU_TIMER;
//     line += 5;
//     DEBUG_MSG("GOT timer lineno = %d\n", lineno);
//   } else if(strncmp(line, "static", 6) == 0) {
//     staticspecified = true;
//     qualifier = STATIC;
//     line += 6;
//     DEBUG_MSG("GOT static lineno = %d\n", lineno);
//   } else if(strncmp(line, "dynamic", 7) == 0) {
//     dynamicspecified = true;
//     qualifier = DYNAMIC;
//     line += 7;
//     DEBUG_MSG("GOT dynamic lineno = %d\n", lineno);
//   } else {
//     parseError("unrecognized token", line, lineno, line - original);
//   }
//
//   /* we have either static/dynamic phase/timer ... */
//   if (staticspecified || dynamicspecified) {
//     /* proceed to the next keyword */
//     WSPACE(line);
//     if (strncmp(line, "phase", 5) == 0) {
//       phasespecified = true;
//       kind = TAU_PHASE;
//       line += 5;
//       DEBUG_MSG("GOT phase command lineno = %d\n", lineno);
//     } else if (strncmp(line, "timer", 5) == 0) {
//       timerspecified = true;
//       kind = TAU_TIMER;
//       line += 5;
//       DEBUG_MSG("GOT timer command lineno = %d\n", lineno);
//     } else {
//       parseError("<phase/timer> token not found", line, lineno, line - original);
//     } /* at this stage we have static/dynamic phase/timer definition */
//   } /* static || dynamic specified */
//
//   switch (kind) {
//   case TAU_NOT_SPECIFIED:
//     break;  // Some instrumentation directives don't have a specific kind
//   case TAU_LOOPS: {
//     WSPACE(line);
//     if (strncmp(line, "file", 4) == 0) {
//       line += 4;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(pfile, line);
//       WSPACE(line);
//       filespecified = true;
//       DEBUG_MSG("GOT file = %s\n", pfile);
//     }
//     if (strncmp(line, "routine", 7) == 0) {
//       line += 7;
//       /* found routine */
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(pname, line);
//       WSPACE(line);
//       DEBUG_MSG("got routine = %s\n", pname);
//     } else {
//       parseError("<routine> token not found", line, lineno, line - original);
//     }
//     if (strncmp(line, "level", 5) == 0) {
//       line += 5;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       RETRIEVENUMBERATEOL(plevel, line);
//       ret = sscanf(plevel, "%d", &level);
//       WSPACE(line);
//       DEBUG_MSG("GOT loop level = %d, line = %s\n", level, line);
//       if (level > 0) {
//         setLoopInstrumentationLevel(level);
//       } else {
//         parseError("Invalid loop level: must be greater than 0\n", line, lineno, line - original);
//       }
//     }
//     if (filespecified) {
//       instrumentList.push_back(new tauInstrument(string(pfile), string(pname), kind));
//     } else {
//       instrumentList.push_back(new tauInstrument(string(pname), kind));
//     }
//   }
//   break; // END case TAU_LOOPS
//
//   case TAU_IO:      // Fall through
//   case TAU_MEMORY:  // Fall through
//   case TAU_FORALL:  // Fall through
//   case TAU_BARRIER: // Fall through
//   case TAU_FENCE:   // Fall through
//   case TAU_NOTIFY: {
//     WSPACE(line);
//     if (strncmp(line, "file", 4) == 0) {
//       line += 4;
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(pfile, line);
//       WSPACE(line);
//       filespecified = true;
//       DEBUG_MSG("GOT file = %s\n", pfile);
//     }
//     if (strncmp(line, "routine", 7) == 0) {
//       line += 7;
//       /* found routine */
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(pname, line);
//       DEBUG_MSG("got routine = %s\n", pname);
//       if (filespecified) {
//         instrumentList.push_back(new tauInstrument(string(pfile), string(pname), kind));
//       } else {
//         instrumentList.push_back(new tauInstrument(string(pname), kind));
//       }
//     } else {
//       parseError("<routine> token not found", line, lineno, line - original);
//     }
//   }
//   break; // END case TAU_NOTIFY
//
//   case TAU_PHASE:    // Fall through
//   case TAU_TIMER: {
//     WSPACE(line);
//
//     /* static/dynamic phase/timer routine = "..." */
//     if (strncmp(line, "routine", 7) == 0) {
//       line += 7;
//       /* found routine */
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(pname, line);
//       DEBUG_MSG("s/d p/t got routine = %s\n", pname);
//
//       /* static/dynamic phase/timer name = "..." file=<name> line = <no> to line = <no> */
//     } else if (strncmp(line, "name", 4) == 0) {
//       line += 4;
//       /* found name */
//       WSPACE(line);
//       TOKEN('=');
//       WSPACE(line);
//       TOKEN('"');
//       RETRIEVESTRING(pname, line);
//       WSPACE(line);
//       DEBUG_MSG("s/d p/t got name = %s\n", pname);
//
//       /* name was parsed. Look for line = <no> to line = <no> next */
//       if (strncmp(line, "file", 4) == 0) { /* got line token, get line no. */
//         line += 4;
//         WSPACE(line);
//         TOKEN('=');
//         WSPACE(line);
//         TOKEN('"');
//         RETRIEVESTRING(pfile, line);
//         filespecified = true;
//         WSPACE(line);
//         DEBUG_MSG("GOT file = %s\n", pfile);
//       } else {
//         parseError("<file> token not found", line, lineno, line - original);
//       }
//       if (strncmp(line, "line", 4) == 0) { /* got line token, get line no. */
//         line += 4;
//         WSPACE(line);
//         TOKEN('=');
//         WSPACE(line);
//         RETRIEVENUMBER(plineno, line);
//         ret = sscanf(plineno, "%d", &startlineno);
//         DEBUG_MSG("GOT start line no = %d, line = %s\n", startlineno, line);
//         WSPACE(line);
//         if (strncmp(line, "to", 2) == 0) {
//           line += 2;
//           WSPACE(line);
//           /* look for line=<no> next */
//           if (strncmp(line, "line", 4) == 0) {
//             line += 4;
//             WSPACE(line);
//             TOKEN('=');
//             WSPACE(line);
//             RETRIEVENUMBERATEOL(pcode, line);
//             ret = sscanf(pcode, "%d", &stoplineno);
//             DEBUG_MSG("GOT stop line no = %d\n", stoplineno);
//           } else { /* we got line=<no> to , but there was no line */
//             parseError("<line> token not found in the stop declaration", line, lineno, line - original);
//           } /* parsed to clause */
//         } else { /* hey, line = <no> is there, but there is no "to" */
//           parseError("<to> token not found", line, lineno, line - original);
//         } /* we have parsed all the tokens now. Let us see what was specified phase/timer routine = <name> or phase/timer name =<name> line = <no> to line = <no> */
//       } else {
//         parseError("<line> token not found in the start declaration", line, lineno, line - original);
//       } /* line specified */
//     } else { /* name or routine not specified */
//       parseError("<routine/name> token not found", line, lineno, line - original);
//     } /* end of routine/name processing */
//
//     /* create instrumentation requests */
//     if (filespecified) { /* [static/dynamic] <phase/timer> name = "<name>" file="<name>" line=a to line=b */
//       instrumentList.push_back(new tauInstrument(qualifier, kind, pname, pfile, startlineno, stoplineno));
//     } else { /* [static/dynamic] <phase/timer> routine = "<name>" */
//       instrumentList.push_back(new tauInstrument(qualifier, kind, pname));
//     }
//   }
//   break; // END case TAU_TIMER
//
//   default: {
//     parseError("Internal error: unknown instrumentKind_t", line, lineno, line - original);
//   }
//   break;
//   } // END switch (kind)
//
// } // END void parseInstrumentationCommand(char *line, int lineno)

void processInstrumentationRequests(const char *fname)
{

  std::ifstream input(fname);
  char line[INBUF_SIZE];
  char* inbuf;
  int lineno = 0;


  if (!input) {
    std::cerr << "ERROR: Cannot open file: " << fname << std::endl;
  }


  DPRINT0("Inside processInstrumentationRequests\n");
  while (input.getline(line, INBUF_SIZE) || input.gcount()) {
    lineno++;
    /* Skip whitespaces at the beginning of line */
    inbuf = line;
    while (isspace(*inbuf)) {
      ++inbuf;
    }
    if ((inbuf[0] == '#') || (inbuf[0] == '\0')) {
      continue;
    }
    if (strcmp(inbuf,BEGIN_EXCLUDE_TOKEN) == 0) {
      while(input.getline(line,INBUF_SIZE) || input.gcount()) {
        lineno++;
        /* Skip whitespaces at the beginning of line */
        inbuf = line;
        while (isspace(*inbuf)) {
          ++inbuf;
        }

      	if (strcmp(inbuf, END_EXCLUDE_TOKEN) == 0) {
          break; /* Found the end of exclude list. */
        }

        if ((inbuf[0] == '#') || (inbuf[0] == '\0')) {
          continue;
        }
        if (inbuf[0] == '"') {
          /* What if the string begins with "? In that case remove quotes from
             the string. "#foo" becomes #foo and is passed on to the
             exclude list. */
          char *exclude = strdup(&inbuf[1]);
          int i;
          for (i = 0; i < strlen(exclude); i++) {
            if (exclude[i] == '"') {
              exclude[i]='\0';
              break; /* out of the loop */
            }
          }
          DPRINT("Passing %s as exclude string\n", exclude);
          excludelist.push_back(std::string(exclude));
          free(exclude);
        }
        else {
          excludelist.push_back(std::string(inbuf));
        }
      }
    }

    if (strcmp(inbuf, BEGIN_INCLUDE_TOKEN) == 0) {
      while(input.getline(line,INBUF_SIZE) || input.gcount()) {
        lineno++;
        /* Skip whitespaces at the beginning of line */
        inbuf = line;
        while (isspace(*inbuf)) {
          ++inbuf;
        }
      	if (strcmp(inbuf, END_INCLUDE_TOKEN) == 0) {
          break; /* Found the end of exclude list. */
        }
        if ((inbuf[0] == '#') || (inbuf[0] == '\0')) {
          continue;
        }
        if (inbuf[0] == '"') {
          /* What if the string begins with "? In that case remove quotes from
             the string. "#foo" becomes #foo and is passed on to the
             exclude list. */
          char *exclude = strdup(&inbuf[1]);
          int i;
          for (i = 0; i < strlen(exclude); i++) {
            if (exclude[i] == '"') {
              exclude[i]='\0';
              break; /* out of the loop */
            }
          }
          DPRINT("Passing %s as include string\n", exclude);
          includelist.push_back(std::string(exclude));
          free(exclude);
        }
        else {
          includelist.push_back(std::string(inbuf));
        }
      }
    }

    if (strcmp(inbuf,BEGIN_FILE_INCLUDE_TOKEN) == 0) {
      while(input.getline(line,INBUF_SIZE) || input.gcount()) {
        lineno++;
        /* Skip whitespaces at the beginning of line */
        inbuf = line;
        while (isspace(*inbuf)) {
          ++inbuf;
        }
      	if (strcmp(inbuf, END_FILE_INCLUDE_TOKEN) == 0) {
          break; /* Found the end of file include list. */
        }
        if ((inbuf[0] == '#') || (inbuf[0] == '\0')) {
          continue;
        }
        // strip quotes
        if (inbuf[0] == '"') {
          char *include = strdup(&inbuf[1]);
          for (int i = 0; i < strlen(include); i++) {
            if (include[i] == '"') {
              include[i] = '\0';
              break;
            }
          }
          fileincludelist.push_back(std::string(include));
          free(include);
        }
        else {
          fileincludelist.push_back(std::string(inbuf));
        }
      	DPRINT("Parsing inst. file: adding %s to file include list\n", inbuf);
      }
    }

    if (strcmp(inbuf,BEGIN_FILE_EXCLUDE_TOKEN) == 0) {
      while(input.getline(line,INBUF_SIZE) || input.gcount()) {
        lineno++;
        /* Skip whitespaces at the beginning of line */
        inbuf = line;
        while (isspace(*inbuf)) {
          ++inbuf;
        }
      	if (strcmp(inbuf, END_FILE_EXCLUDE_TOKEN) == 0) {
          break; /* Found the end of file exclude list. */
        }
        if ((inbuf[0] == '#') || (inbuf[0] == '\0')) {
          continue;
        }
        // strip quotes
        if (inbuf[0] == '"') {
          char *exclude = strdup(&inbuf[1]);
          for (int i = 0; i < strlen(exclude); i++) {
            if (exclude[i] == '"') {
              exclude[i] = '\0';
              break;
            }
          }
          fileexcludelist.push_back(std::string(exclude));
          free(exclude);
        }
        else {
          fileexcludelist.push_back(std::string(inbuf));
        }
      }
    }

    if (strcmp(inbuf,BEGIN_INSTRUMENT_SECTION) == 0) {
      printf("WARNING: Instrument section is not supported yet.");
      while(input.getline(line,INBUF_SIZE) || input.gcount()) {
        lineno++;
        /* Skip whitespaces at the beginning of line */
        inbuf = line;
        while (isspace(*inbuf)) {
          ++inbuf;
        }

    	if (strcmp(inbuf, END_INSTRUMENT_SECTION) == 0) {
        break; /* Found the end of file exclude list. */
      }
      if ((inbuf[0] == '#') || (inbuf[0] == '\0')) {
        continue;
      }
	    // parseInstrumentationCommand(inbuf, lineno);
      }
    }
    /* next token */
  } // while (input.getline(line, INBUF_SIZE) || input.gcount())
  input.close();

  DPRINT0("includelist\n");
  dump_list(includelist);

  DPRINT0("excludelist\n");
  dump_list(excludelist);

  DPRINT0("fileincludelist\n");
  dump_list(fileincludelist);

  DPRINT0("fileexcludelist\n");
  dump_list(fileexcludelist);
}
