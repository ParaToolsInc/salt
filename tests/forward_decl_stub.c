/* Stub TU paired with tests/forward_decl.c.  The fixture's regression
 * coverage for issue #53 is "cparse-llvm must skip FunctionDecls whose
 * getBody() returns nullptr without crashing".  forward_decl.c provides
 * the bodyless prototype + caller + main that exercises that AST shape;
 * cparse-llvm only ever sees forward_decl.c.  This second TU supplies
 * the missing definition so the link step succeeds and the resulting
 * binary can be run to verify the instrumentor's output is valid
 * end-to-end.
 *
 * Keep this file minimal so it cannot accidentally introduce new
 * regression coverage of its own.
 */

void undefined_in_tu(int x) {
    (void)x;
}
