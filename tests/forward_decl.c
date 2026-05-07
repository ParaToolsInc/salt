/* Regression #53: cparse-llvm must not crash when the translation
 * unit contains FunctionDecls whose getBody() returns nullptr. The
 * forward declaration below is never defined in this TU; its decl
 * must be skipped (hasBody() guard at instrumentor.cpp VisitFunctionDecl).
 * Defined functions and main must still be instrumented.
 */

#include <stdio.h>

void undefined_in_tu(int x);

void caller(int x) {
    if (x < 0) undefined_in_tu(x);
    else printf("%d\n", x);
}

int main(void) {
    caller(7);
    return 0;
}
