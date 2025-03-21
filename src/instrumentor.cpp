#define RYML_SINGLE_HDR_DEFINE_NOW
#define RYML_SHARED

#include <ryml_all.hpp>
#include "instrumentor.hpp"
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
#include <regex>
#include <string>
#include <vector>
#include <sys/stat.h>

#include "dprint.hpp"
#include "selectfile.hpp"

using namespace clang;

#include "clang_header_includes.h"

extern const char* loc_typ_strs[NUM_LOC_TYPES];

const std::string file_get_contents(FILE *fp)
{
    char *buffer = 0;
    long length;
    fseek (fp, 0, SEEK_END);
    length = ftell(fp);
    fseek (fp, 0, SEEK_SET);
    buffer = (char *) malloc(length);
    if (buffer)
    {
        fread (buffer, 1, length, fp);
    }
    return std::string(buffer);
}

const char *loc_typ_strs[NUM_LOC_TYPES] = {"begin func", "return", "multiline return", "exit"};

void makeFuncAndTimerNames(FunctionDecl *func, ASTContext *context, SourceManager &src_mgr, std::string &func_name,
                         std::string &timer_name);

// only works for inst_locs that are in the same file
bool comp_inst_loc(inst_loc *first, inst_loc *second)
{
    if (first->line != second->line)
    {
        return first->line < second->line;
    }
    else if (first->col != second->col)
    {
        return first->col < second->col;
    }
    else
    { // SOME PEOPLE have functions that are just {} so we need to make sure begin comes before return
        return first->kind < second->kind;
    }
}

bool eq_inst_loc(inst_loc *first, inst_loc *second)
{
    if (first->line == second->line && first->col == second->col && first->kind == second->kind &&
        strcmp(first->func_name, second->func_name) == 0)
    {
        return true;
    }
    return false;
}

void dump_inst_loc(inst_loc *loc)
{
    DPRINT("\tLine:                 %d\n", loc->line);
    DPRINT("\tCol:                    %d\n", loc->col);
    DPRINT("\tKind:                 %s\n", loc_typ_strs[loc->kind]);
    DPRINT("\tRet type:         %s\n", loc->return_type);
    DPRINT("\tName:                 \"%s\"\n", loc->func_name);
    DPRINT("\tTimer:                    %s\n", loc->full_timer_name);
    DPRINT("\tHas args:         %s\n", loc->has_args ? "Yes" : "No");
    DPRINT("\tIs ret ptr:     %s\n", loc->is_return_ptr ? "Yes" : "No");
    DPRINT("\tNeeds move:     %s\n", loc->needs_move ? "Yes" : "No");
    DPRINT("\tSkip:                 %s\n", loc->skip ? "Yes" : "No");
}

void dump_inst_loc(inst_loc *loc, int n)
{
    DPRINT("location %d\n", n);
    dump_inst_loc(loc);
}

void dump_inst_loc(inst_loc *loc, int n, bool (*filter)(inst_loc *))
{
    if (filter(loc))
    {
        DPRINT("location %d\n", n);
        dump_inst_loc(loc);
    }
}

void dump_all_locs()
{
    for (int i = 0; i < inst_locs.size(); i++)
    {
        dump_inst_loc(inst_locs[i], i);
    }
}

void dump_all_locs(std::vector<inst_loc *> locs)
{
    for (int i = 0; i < locs.size(); i++)
    {
        dump_inst_loc(locs[i], i);
    }
}

void dump_all_locs(bool (*filter)(inst_loc *))
{
    for (int i = 0; i < inst_locs.size(); i++)
    {
        dump_inst_loc(inst_locs[i], i, filter);
    }
}

std::string ReplacePhrase(std::string str, std::string phrase, std::string to_replace)
{
    while (str.find(phrase) != std::string::npos) 
    {
        str.replace(str.find(phrase), phrase.length(), to_replace);
    }
    return str;
}

void make_begin_func_code(inst_loc *loc, std::string &code, ryml::Tree yaml_tree, const bool use_cxx_api=false)
{
    /* dump the location */
    /* dump_inst_loc(loc); */
    if (!loc->skip)
    {
        if (strcmp(loc->func_name, "main") == 0 )
        {
            // Insert on main function
            for (ryml::NodeRef const& child : yaml_tree[use_cxx_api ? "main_insert_scope" : "main_insert"].children())
            {
                std::stringstream ss;
                ss << child.val();
                std::string updated_str;
                updated_str  = ReplacePhrase(ss.str(), "${full_timer_name}", loc->full_timer_name);
                /* handle the case where main does NOT have arguments */
                if (!loc->has_args)
                {
                    updated_str = ReplacePhrase(updated_str, "    TAU_INIT(&argc, &argv);", "/* TAU_INIT() skipped, no arguments */");
                }
                code += updated_str + "\n";
            }
        }
        else
        {
            // Insert on function begin insert
            for (ryml::NodeRef const& child : yaml_tree[use_cxx_api ? "function_begin_insert_scope" : "function_begin_insert"].children())
            {
                std::stringstream ss;
                ss << child.val();
                std::string updated_str;
                updated_str  = ReplacePhrase(ss.str(), "${full_timer_name}", loc->full_timer_name);
                code += updated_str + "\n";
            }
        }
    }
}

// returns true if we can/should skip putting the line after this into the inst file
void make_end_func_code(inst_loc *loc, std::string &code, std::string &line, ryml::Tree yaml_tree)
{
    if (line.find("return", loc->col) == std::string::npos)
    {
        // don't put anything in for non-void functions missing explicit return
        // if we have int main() with no explicit return, use -optDisable hack
        // -glares at cmake-
        if (std::string(loc->return_type).find("void") == std::string::npos)
        {
            return;
        }
        else
        {
            // if it is void, put in stop just in case
            if (!loc->skip)
            {
                // Insert on function begin insert
                for (ryml::NodeRef const& child : yaml_tree["function_end_insert"].children()) 
                {
                    std::stringstream ss;
                    ss << child.val();
                    std::string updated_str;
                    updated_str  = ReplacePhrase(ss.str(), "${full_timer_name}", loc->full_timer_name);
                    code += "\t" + updated_str + "\n";
                }
            }
            else
            {
                code = "";
            }
            return;
        }
    }
    // types are harder, need to pull the arg to return before the stop in case it does things
    else
    {
        if (!loc->skip)
        {
            std::string temp = line;
            temp.erase(std::remove_if(temp.begin(), temp.end(), isspace), temp.end());
            if (temp.find("return;") != std::string::npos)
            {
                // also throw in brackets in case SOMEONE didn't put brackets around their if
                // Insert on function begin insert
                for (ryml::NodeRef const& child : yaml_tree["function_end_insert"].children()) 
                {
                    std::stringstream ss;
                    ss << child.val();
                    std::string updated_str;
                    updated_str  = ReplacePhrase(ss.str(), "${full_timer_name}", loc->full_timer_name);
                    code += "\t{" + updated_str + " ";
                }
                code += "return;}\n";
                return;
            }
            else
            {
                // special case void*
                if (std::string(loc->return_type).find("void") != std::string::npos && !loc->is_return_ptr)
                {
                    code += "\t{ ";
                    int first_pos = line.find("return") + 6;
                    int last_pos = line.find(";", first_pos);
                    code += line.substr(first_pos, last_pos - first_pos + 1);

                    // Insert on function begin insert
                    for (ryml::NodeRef const& child : yaml_tree["function_end_insert"].children()) 
                    {
                        std::stringstream ss;
                        ss << child.val();
                        std::string updated_str;
                        updated_str  = ReplacePhrase(ss.str(), "${full_timer_name}", loc->full_timer_name);
                        code += " " + updated_str + " ";
                    }
                    code += "return; }\n";
                }
                // special case if we need to throw in a std::move because of copy assign shenanigans
                else if (loc->needs_move)
                {
                    code += "\t{ ";
                    code += loc->return_type;
                    code += " inst_ret_val = std::move(";
                    int first_pos = line.find("return") + 6;
                    int last_pos = line.find(";", first_pos);
                    // no +1, don't want semicolon
                    code += line.substr(first_pos, last_pos - first_pos);
                    code += "); ";

                     // Insert on function begin insert
                    for (ryml::NodeRef const& child : yaml_tree["function_end_insert"].children()) 
                    {
                        std::stringstream ss;
                        ss << child.val();
                        std::string updated_str;
                        updated_str  = ReplacePhrase(ss.str(), "${full_timer_name}", loc->full_timer_name);
                        code += " " + updated_str + " ";
                    }
                    code += "return inst_ret_val; }\n";
                }
                // general case for typed returns
                else
                {
                    code += "\t{ ";
                    code += loc->return_type;
                    code += " inst_ret_val = ";
                    int first_pos = line.find("return") + 6;
                    int last_pos = line.find(";", first_pos);
                    // +1 to catch the semicolon too
                    code += line.substr(first_pos, last_pos - first_pos + 1);

                     // Insert on function begin insert
                    for (ryml::NodeRef const& child : yaml_tree["function_end_insert"].children()) 
                    {
                        std::stringstream ss;
                        ss << child.val();
                        std::string updated_str;
                        updated_str  = ReplacePhrase(ss.str(), "${full_timer_name}", loc->full_timer_name);
                        code += " " + updated_str + " ";
                    }
                    code += "return inst_ret_val; }\n";
                }
                return;
            }
        }
        else
        {
            code = line;
            code += "\n";
        }
    }
}

// https://stackoverflow.com/questions/2896600/how-to-replace-all-occurrences-of-a-character-in-string
std::string ReplaceAll(std::string str, const std::string &from, const std::string &to)
{
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos)
    {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
    }
    // printf("new string: %s\n", str.c_str());
    return str;
}

// https://stackoverflow.com/questions/1798112/removing-leading-and-trailing-spaces-from-a-string
// trim from left
inline std::string &ltrim(std::string &s, const char *t = " \t\n\r\f\v")
{
    s.erase(0, s.find_first_not_of(t));
    return s;
}

// trim from right
inline std::string &rtrim(std::string &s, const char *t = " \t\n\r\f\v")
{
    s.erase(s.find_last_not_of(t) + 1);
    return s;
}

// trim from left & right
inline std::string &trim(std::string &s, const char *t = " \t\n\r\f\v")
{
    return ltrim(rtrim(s, t), t);
}

// first arg is from list (might have wildcard), second string is ours
bool matchName(std::string &regex, std::string &match)
{
    /* Use '#' as the wildcard kleene star operator character */
    regex = ReplaceAll(regex, std::string("*"), std::string("\\*"));
    regex = ReplaceAll(regex, std::string("#"), std::string(".*"));

    return std::regex_match(trim(match), std::regex(trim(regex), std::regex_constants::grep));
}

// returns true if loc matches something in list
bool check_loc_against_list(std::list<std::string> list, inst_loc *loc)
{
    // printf("checking loc against list\n");
    std::string timer_name = std::string(loc->full_timer_name).substr(0, std::string(loc->full_timer_name).find("["));
    for (std::string item : list)
    {
        if (matchName(item, timer_name))
        {
            DPRINT("found match for: %s\n", loc->full_timer_name);
            DPRINT("%s\n", item.c_str());
            return true;
        }
    }
    return false;
}

// returns true if function matches something in list
bool check_func_against_list(std::list<std::string> list, FunctionDecl *func, ASTContext *context,
                             SourceManager &src_mgr)
{
    std::string func_name;
    std::string timer_name;
    // printf("checking function against list\n");
    makeFuncAndTimerNames(func, context, src_mgr, func_name, timer_name);
    timer_name = timer_name.substr(0, timer_name.find("["));
    // printf("timer_name %s\n", timer_name.c_str());
    for (std::string item : list)
    {
        // printf("item %s\n", item.c_str());
        if (matchName(item, timer_name))
        {
            DPRINT("found match for: %s\n", timer_name.c_str());
            DPRINT("%s\n", item.c_str());
            return true;
        }
    }
    return false;
}

bool matchFileName(std::string &regex, std::string &match)
{
    // TODO: if no "/", just match on filename (slice match)
    regex = ReplaceAll(regex, std::string("*"), std::string(".*"));
    if (regex.find("/") == std::string::npos)
    {
        // if no path is specified, match any path OR the original filename
        regex = ReplaceAll(std::string("(.*/x|x)"), std::string("x"), regex);
    }
    // printf("string    %s\n", match.c_str());
    // printf("regex     %s\n", regex.c_str());
    // printf("match?    %s\n", std::regex_match(trim(match), std::regex(trim(regex), std::regex_constants::egrep)) ?
    // "Yes": "No");

    return std::regex_match(trim(match), std::regex(trim(regex), std::regex_constants::egrep));
}


bool check_file_against_list(std::list<std::string> list, std::string fname)
{
    for (std::string item : list)
    {
        if (matchFileName(item, fname))
        {
            DPRINT("found match for: %s\n", fname.c_str());
            return true;
        }
    }
    return false;
}   


void makeFuncAndTimerNames(FunctionDecl *func, ASTContext *context, SourceManager &src_mgr, std::string &func_name,
                         std::string &timer_name)
{
    // TODO: what if it doesn't have a body? -- might have fixed
    Stmt *func_body = func->getBody();
    SourceRange range = func_body->getSourceRange();

    FullSourceLoc start_loc = context->getFullLoc(range.getBegin());
    FullSourceLoc end_loc = context->getFullLoc(range.getEnd());

    unsigned int start_line = start_loc.getSpellingLineNumber();
    unsigned int start_col = start_loc.getSpellingColumnNumber();
    unsigned int end_line = end_loc.getSpellingLineNumber();
    unsigned int end_col = end_loc.getSpellingColumnNumber();

    func_name = func->getQualifiedNameAsString();
    QualType type = func->getType();
    std::string sig = type.getAsString();
    while (sig.find("class ") != std::string::npos)
    {
        sig.erase(sig.find("class "), 6);
    }
    while (sig.find("enum ") != std::string::npos)
    {
        sig.erase(sig.find("enum "), 5);
    }
    // not sure we need this yet
    // while (sig.find("struct ") != std::string::npos) {
    //     sig.erase(sig.find("struct "), 7);
    // }
    while (sig.find("_Bool") != std::string::npos)
    {
        // printf("%s\n", sig.c_str());
        sig.replace(sig.find("_Bool"), 5, "bool");
    }
    sig.insert(sig.find_first_of("("), func_name);

    // unused variable
    // LanguageLinkage lang = func->getLanguageLinkage();
    std::string lang_string = "";
    // Kevin said this is unnecessary -- but selectfiles have it at the end so ???
    // if (lang == CLanguageLinkage) {
    //     lang_string = "C";
    // }
    // else if (lang == CXXLanguageLinkage) {
    //     lang_string = "C++";
    // }
    // else {
    //     lang_string = "unknown";
    // }

    std::string current_file = src_mgr.getFilename(start_loc).str();

    timer_name = sig + " " + lang_string + " [{" + current_file + "} {" + std::to_string(start_line) + "," +
               std::to_string(start_col) + "}-{" + std::to_string(end_line) + "," + std::to_string(end_col) + "}]";
}

class FindReturnVisitor : public RecursiveASTVisitor<FindReturnVisitor>
{
    ASTContext *context;
    SourceManager &src_mgr;
    FunctionDecl *encl_function;
    std::vector<SourceRange> lambda_locs;

  public:
    explicit FindReturnVisitor(ASTContext *context, SourceManager &SM) : context(context), src_mgr(SM)
    {
    }

    bool VisitReturnStmt(ReturnStmt *ret)
    {
        for (SourceRange lambda : lambda_locs)
        {
            if (lambda.fullyContains(ret->getSourceRange()))
            {
                // ignore lambdas
                return true;
            }
        }
        makeRetInstLoc(ret);
        return true;
    }

    bool VisitLambdaExpr(LambdaExpr *lambda)
    {
        lambda_locs.push_back(lambda->getSourceRange());
        return true;
    }

  private:
    void makeRetInstLoc(ReturnStmt *retstmt)
    {
        SourceRange range = retstmt->getSourceRange();

        FullSourceLoc start_loc = context->getFullLoc(range.getBegin());
        FullSourceLoc end_loc = context->getFullLoc(range.getEnd());

        unsigned int start_line = start_loc.getSpellingLineNumber();
        unsigned int start_col = start_loc.getSpellingColumnNumber();
        unsigned int end_line = end_loc.getSpellingLineNumber();
        // unused
        // unsigned int end_col = end_loc.getSpellingColumnNumber();

        std::string func_name;
        std::string timer_name;
        makeFuncAndTimerNames(encl_function, context, src_mgr, func_name, timer_name);

        char *func_name_c = new char[func_name.length() + 1];
        std::strcpy(func_name_c, func_name.c_str());

        char *timer_name_c = new char[timer_name.length() + 1];
        std::strcpy(timer_name_c, timer_name.c_str());

        std::string ret_name = encl_function->getReturnType().getAsString();
        if (encl_function->getReturnType().getTypePtr()->isBooleanType() && ret_name.find("_Bool") != std::string::npos)
        {
            ret_name.replace(ret_name.find("_Bool"), 5, "bool");
        }
        if (ret_name.substr(0, 5).find("class") != std::string::npos)
        {
            ret_name = ret_name.substr(6); // if it starts with "class", chop that off
        }
        if (ret_name.substr(0, 11).find("const class") != std::string::npos)
        {
            ret_name = ret_name.erase(6, 6); // if it starts with "class", chop that off
        }

        bool needs_move = false;
        if (encl_function->getReturnType()->isClassType())
        {
            CXXRecordDecl *decl = encl_function->getReturnType()->getAsCXXRecordDecl();
            if (!(decl->hasSimpleCopyAssignment() || decl->hasTrivialCopyAssignment()))
            {
                needs_move = true;
            }
        }

        char *ret_name_c = new char[ret_name.length() + 1];
        std::strcpy(ret_name_c, ret_name.c_str());

        inst_loc *ret = new inst_loc;
        ret->line = start_line;
        ret->col = start_col - 1;
        ret->kind = start_line == end_line ? RETURN_FUNC : MULTILINE_RETURN_FUNC;
        ret->func_name = func_name_c;
        ret->return_type = ret_name_c;
        ret->full_timer_name = timer_name_c;
        ret->has_args = encl_function->getNumParams() > 0;
        ret->is_return_ptr = encl_function->getReturnType()->isPointerType();
        ret->needs_move = needs_move;

        inst_locs.push_back(ret);

        // llvm::outs() << "\tFound return at " << start_line << ":" << start_col << "\n";
    }

    friend class FindFunctionVisitor;
};

class FindFunctionVisitor : public RecursiveASTVisitor<FindFunctionVisitor>
{
    ASTContext *context;
    SourceManager &src_mgr;
    FindReturnVisitor return_visitor;

  public:
    explicit FindFunctionVisitor(ASTContext *context, SourceManager &SM)
        : context(context), src_mgr(SM), return_visitor(context, SM)
    {
    }

    bool VisitFunctionDecl(FunctionDecl *func)
    {
        // if (func->isInlined()) {
        //     printf("Function %s is inline\n", func->getQualifiedNameAsString().c_str());
        // }
        // short circuit on hasBody() first to protect check_func_against_list (and makeFuncInstLoc) from segfaults
        if (func->hasBody() &&
            (!func->isInlined() || inst_inline || check_func_against_list(includelist, func, context, src_mgr)))
        { //
            makeFuncInstLoc(func);
            return_visitor.encl_function = func;
            return_visitor.TraverseDecl(func);
        }
        return true;
    }

  private:
    void makeFuncInstLoc(FunctionDecl *func)
    {
        Stmt *func_body = func->getBody();
        SourceRange range = func_body->getSourceRange();

        FullSourceLoc start_loc = context->getFullLoc(range.getBegin());
        FullSourceLoc end_loc = context->getFullLoc(range.getEnd());

        unsigned int start_line = start_loc.getSpellingLineNumber();
        unsigned int start_col = start_loc.getSpellingColumnNumber();
        unsigned int end_line = end_loc.getSpellingLineNumber();
        unsigned int end_col = end_loc.getSpellingColumnNumber();

        std::string func_name;
        std::string timer_name;

        makeFuncAndTimerNames(func, context, src_mgr, func_name, timer_name);

        char *func_name_c = new char[func_name.length() + 1];
        std::strcpy(func_name_c, func_name.c_str());

        char *timer_name_c = new char[timer_name.length() + 1];
        std::strcpy(timer_name_c, timer_name.c_str());

        std::string ret_name = func->getReturnType().getAsString();
        // why
        if (func->getReturnType().getTypePtr()->isBooleanType() && ret_name.find("_Bool") != std::string::npos)
        {
            ret_name.replace(ret_name.find("_Bool"), 5, "bool ");
        }
        if (ret_name.substr(0, 5).find("class") != std::string::npos)
        {
            ret_name = ret_name.substr(6); // if it starts with "class", chop that off
        }
        if (ret_name.substr(0, 11).find("const class") != std::string::npos)
        {
            ret_name = ret_name.erase(6, 6); // if it starts with "class", chop that off
        }
        char *ret_name_c = new char[ret_name.length() + 1];
        std::strcpy(ret_name_c, ret_name.c_str());

        bool needs_move = false;
        if (func->getReturnType()->isClassType())
        {
            CXXRecordDecl *decl = func->getReturnType()->getAsCXXRecordDecl();
            if (!(decl->hasSimpleCopyAssignment() || decl->hasTrivialCopyAssignment()))
            {
                needs_move = true;
            }
        }

        inst_loc *start = new inst_loc;
        start->line = start_line;
        start->col = start_col;
        start->kind = BEGIN_FUNC;
        start->return_type = ret_name_c;
        start->func_name = func_name_c;
        start->full_timer_name = timer_name_c;
        start->has_args = func->getNumParams() > 0;
        start->is_return_ptr = func->getReturnType()->isPointerType();
        start->needs_move = needs_move;

        inst_locs.push_back(start);

        inst_loc *end = new inst_loc;
        end->line = end_line;
        end->col = end_col - 1;
        end->kind = RETURN_FUNC;
        end->return_type = ret_name_c;
        end->func_name = func_name_c;
        end->full_timer_name = timer_name_c;
        end->has_args = func->getNumParams() > 0;
        end->is_return_ptr = func->getReturnType()->isPointerType();
        end->needs_move = needs_move;

        inst_locs.push_back(end);

        // llvm::outs() << "Found function " << timer_name << "\n";
    }
};

class FindFunctionConsumer : public clang::ASTConsumer
{
    FindFunctionVisitor func_visitor;
    SourceManager &src_mgr;

  public:
    FindFunctionConsumer(ASTContext *context, SourceManager &SM) : func_visitor(context, SM), src_mgr(SM)
    {
    }

    virtual void HandleTranslationUnit(ASTContext &context)
    {
        auto decls = context.getTranslationUnitDecl()->decls();
        for (auto &decl : decls)
        {
            SourceLocation srcloc = decl->getLocation();
            // always exclude system headers
            if (src_mgr.isInSystemHeader(srcloc) || 
                src_mgr.isInExternCSystemHeader(srcloc) ||
                src_mgr.isInSystemMacro(srcloc))
            {
                continue;
            }
            // if file include list exists: *only include those*
            // except for things in the exclude list
            if (!fileincludelist.empty())
            {
                if (check_file_against_list(fileincludelist, src_mgr.getFilename(srcloc).str()) &&
                    !check_file_against_list(fileexcludelist, src_mgr.getFilename(srcloc).str()))
                {
                    // printf("adding1 %s\n", src_mgr.getFilename(srcloc).str().c_str());
                    if (!src_mgr.getFilename(srcloc).str().empty() &&
                        std::find(files_to_go.begin(), files_to_go.end(), src_mgr.getFilename(srcloc).str()) ==
                            files_to_go.end())
                    {
                        files_to_go.push_back(src_mgr.getFilename(srcloc).str());
                    }
                    func_visitor.TraverseDecl(decl);
                }
            }
            // if file include list does not exist, just check against exclude list
            else
            {
                if (!check_file_against_list(fileexcludelist, src_mgr.getFilename(srcloc).str()))
                {
                    // decl->dump();
                    // printf("adding2 %s\n", src_mgr.getFilename(srcloc).str().c_str());
                    // printf("empty? %s\n", src_mgr.getFilename(srcloc).str().empty() ? "Yes" : "No");
                    if (!src_mgr.getFilename(srcloc).str().empty() &&
                        std::find(files_to_go.begin(), files_to_go.end(), src_mgr.getFilename(srcloc).str()) ==
                            files_to_go.end())
                    {
                        files_to_go.push_back(src_mgr.getFilename(srcloc).str());
                    }
                    func_visitor.TraverseDecl(decl);
                }
            }
        }
    }
};

class FindFunctionAction : public ASTFrontendAction
{
  public:
    virtual std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &Compiler, llvm::StringRef InFile)
    {
        return std::make_unique<FindFunctionConsumer>(&Compiler.getASTContext(), Compiler.getSourceManager());
    }
};

instrumentor::instrumentor() 
{
    // Constructor currently does not need to do anything
}

void instrumentor::set_exec_name(const char* name)
{
    exec_name = strdup(name);
}

void instrumentor::run_tool()
{
    Tool->run(tooling::newFrontendActionFactory<FindFunctionAction>().get());
}

void instrumentor::instr_request(std::list<std::string> list, bool include) 
{
    for (inst_loc *loc : inst_locs)
    {
        if (check_loc_against_list(list, loc))
        {
            loc->skip = !include;
        }
    }
}

void instrumentor::instrument_file(std::ifstream &og_file, std::ofstream &inst_file, std::string filename,
                     std::vector<inst_loc *> inst_locations, bool use_cxx_api, ryml::Tree yaml_tree)
{
    std::string line;
    int lineno = 0;

    auto inst_loc_iter = inst_locations.begin();

    for (ryml::NodeRef const& child : yaml_tree["include"].children()) {
        inst_file << "#include " << child.val() << "\n";
    }

    inst_file << "#line 1 \"" << filename << "\"\n";

    while (getline(og_file, line))
    {
        lineno++;
        // short circuit if we run out of inst locations to avoid segfaults :)
        // need the if and the while because sometimes 2+ inst locations are on the same line
        if (inst_loc_iter != inst_locations.end())
        {
            inst_loc *curr_inst_loc = *inst_loc_iter;
            if (lineno == curr_inst_loc->line)
            {
                curr_inst_loc = *inst_loc_iter;
                int num_inst_locs_this_line = 0;
                std::string start = line.substr(0, curr_inst_loc->col);
                std::string end;
                while (inst_loc_iter != inst_locations.end() && lineno == curr_inst_loc->line)
                {
                    if (curr_inst_loc->col < line.size())
                    {
                        end = line.substr(curr_inst_loc->col);
                    }
                    else
                    {
                        end = "";
                    }
                    if (num_inst_locs_this_line == 0)
                    {
                        inst_file << start;
                    }
                    std::string inst_code = "";
                    switch (curr_inst_loc->kind)
                    {
                    case BEGIN_FUNC:
                        inst_file << "\n#line " << lineno << "\n";
                        make_begin_func_code(curr_inst_loc, inst_code, yaml_tree, use_cxx_api);
                        inst_file << inst_code;
                        inst_file << "#line " << lineno << "\n";
                        break;
                    case RETURN_FUNC:
                        if (!use_cxx_api)
                        {
                            inst_file << "\n#line " << lineno << "\n";
                            make_end_func_code(curr_inst_loc, inst_code, line, yaml_tree);
                            inst_file << inst_code;
                            inst_file << "#line " << lineno << "\n";
                            if (line.find("return", curr_inst_loc->col) != std::string::npos &&
                                line.find(";", line.find("return", curr_inst_loc->col)) != std::string::npos)
                            {
                                end = line.substr(line.find(";", line.find("return", curr_inst_loc->col)) + 1);
                            }
                        }
                        break;
                    case MULTILINE_RETURN_FUNC:
                        if (!use_cxx_api)
                        {
                            inst_file << "\n#line " << lineno << "\n";
                            // join all the lines together into one so make_end_func_code can actually know what's
                            // happening
                            while (line.find(";") == std::string::npos)
                            {
                                std::string templine;
                                getline(og_file, templine);
                                line += templine;
                                lineno++;
                            }
                            make_end_func_code(curr_inst_loc, inst_code, line, yaml_tree);
                            inst_file << inst_code;
                            inst_file << "#line " << lineno << "\n";
                            if (line.find("return", curr_inst_loc->col) != std::string::npos &&
                                line.find(";", line.find("return", curr_inst_loc->col)) != std::string::npos)
                            {
                                end = line.substr(line.find(";", line.find("return", curr_inst_loc->col)) + 1);
                            }
                        }
                    default:
                        break;
                    }

                    inst_loc_iter++;
                    curr_inst_loc = *inst_loc_iter;
                    num_inst_locs_this_line++;
                }

                inst_file << end << "\n";
            }
            else
            {
                inst_file << line << "\n";
            }
        }
        else
        {
            inst_file << line << "\n";
        }
    }
}

void instrumentor::instrument()
{
    // printf("size %zu\n", files_to_go.size());
    // for (std::string fname : files_to_go) {
    //     printf("Instrumenting %s\n", fname.c_str());
    // }
    
    for (std::string fname : files_to_go)
    {
        // if this isn't the file we passed in, continue
        // basically, skip all headers.
        std::string short_name;
        auto location = fname.find_last_of("/\\");
        if (location != std::string::npos)
        {
            short_name = fname.substr(location + 1);
        }
        else
        {
            short_name = fname;
        }
        if (file_set.count(short_name) == 0)
        {
            continue;
        }
        DPRINT("Instrumenting %s\n", fname.c_str());
        std::vector<inst_loc *> inst_locations;

        DPRINT0("filtering locations for file\n");
        // filter on fname
        for (inst_loc *loc : inst_locs)
        {
            std::string timer_name = std::string(loc->full_timer_name);
            // grab contents of first set of curly braces, which is filename
            std::string loc_fname = timer_name.substr(timer_name.find_first_of("{") + 1,
                                                    timer_name.find_first_of("}") - timer_name.find_first_of("{") - 1);
            // printf("loc file %s\n", loc_fname.c_str());
            // printf("cur file %s\n", fname.c_str());
            if (loc_fname.find(fname) != std::string::npos || fname.find(loc_fname) != std::string::npos)
            {
                inst_locations.push_back(loc);
            }
        }

        // inst_locs.begin(), inst_locs.end(), inst_locations.begin(),
        // [&](inst_loc* loc) {
        //     std::string timer_name = std::string(loc->full_timer_name);
        //     // grab contents of first set of curly braces, which is filename
        //     std::string loc_fname = timer_name.substr(timer_name.find_first_of("{")+1, timer_name.find_first_of("}") -
        //     timer_name.find_first_of("{")-1); printf("loc file %s\n", loc_fname.c_str()); printf("cur file %s\n",
        //     fname.c_str()); return trim(loc_fname) == trim(fname);
        // });
        // sort by line numbers then cols (then loc type) so looping goes well
        std::sort(inst_locations.begin(), inst_locations.end(), comp_inst_loc);
        // unique again just in case
        auto new_end3 = std::unique(inst_locations.begin(), inst_locations.end(), eq_inst_loc);
        inst_locations.erase(new_end3, inst_locations.end());

        // if (fname.find("WaveFunction.cpp") != std::string::npos) {
        // dump_all_locs(inst_locations);
        // }

        std::ifstream og_file;
        std::ofstream inst_file;
        std::string newname = fname;
        if (!outputfile.empty())
        {
            newname = outputfile;
        }
        else
        {
            auto location = fname.find_last_of("/\\");
            if (location != std::string::npos)
            {
                newname = fname.substr(location + 1);
            }
            else
            {
                newname = fname;
            }
            location = newname.find_last_of(".");
            if (location != std::string::npos)
            {
                newname.insert(newname.find_last_of("."), ".inst");
            }
            else
            {
                newname.append(".inst");
            }
        }
        DPRINT("new filename (inst): %s\n", newname.c_str());

        inst_file.open(newname);
        og_file.open(fname);

        // check for cxxparse executable name. If so, force cxx api usage
        if (strstr(exec_name, "cxxparse") != nullptr)
        {
            use_cxx_api = true;
            DPRINT("%s: Forcing TAU CXX API\n", exec_name);
            fflush(stdout);
        }

        // Read config.yaml
        ryml::Tree yaml_tree;
        if (FILE *config_file = fopen(configfile.c_str(), "r"))
        {
            std::ifstream inputStream{configfile.c_str()};
            if (!inputStream) {
                llvm::errs() << "ERROR: Could not open configuration file " << configfile.c_str() << "\n";
                std::exit(-3);
            }
            std::stringstream configStream;
            configStream << inputStream.rdbuf();
            // TODO handle errors if config yaml doesn't parse
            yaml_tree = ryml::parse_in_arena(ryml::to_csubstr(configStream.str()));

            fclose(config_file);
        }
        else
        {
            llvm::outs() << "No config file found\n";
            remove(newname.c_str());
            exit(1);
        }

        // If using C++ API, check that config file contains code for scoped instrumentation
        if (use_cxx_api) {
            if (ryml::ConstNodeRef mainInsertScope = yaml_tree["main_insert_scope"]; mainInsertScope.invalid()) {
               llvm::errs() << "Using C++ Instrumentation API requires `main_insert_scope` in config file.\n";
                exit(2);
            }
            if (ryml::ConstNodeRef functionBeginScope = yaml_tree["function_begin_insert_scope"]; functionBeginScope.
                invalid()) {
                llvm::errs() << "Using C++ Instrumentation API requires `function_begin_insert_scope` in config file.\n";
                exit(2);
            }
        }

        llvm::outs() << "Instrumentation: " << yaml_tree["instrumentation"].val() << "\n"; 
        instrument_file(og_file, inst_file, fname, inst_locations, use_cxx_api, yaml_tree);
        og_file.close();
        inst_file.close();
    }

    for (std::string fname : files_skipped)
    {
        std::ifstream og_file;
        std::ofstream inst_file;
        std::string newname = fname;
        if (!outputfile.empty())
        {
            newname = outputfile;
        }
        else
        {
            newname.insert(newname.find_last_of("."), ".inst");
        }
        DPRINT("new filename (skip): %s\n", newname.c_str());

        inst_file.open(newname);
        og_file.open(fname);

        inst_file << og_file.rdbuf();

        og_file.close();
        inst_file.close();
    }
}
