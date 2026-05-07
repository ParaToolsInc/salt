/* Regression #53: cparse-llvm must not crash when the translation
 * unit contains FunctionDecls whose getBody() returns nullptr. The
 * pure-virtual member and the externally-declared free function below
 * have no body in this TU; both must be skipped (hasBody() guard).
 * The concrete override is implicitly inline (in-class definition)
 * and is gated separately. main must be instrumented.
 */

#include <iostream>

extern void undefined_free_fn(int);

class Abstract {
public:
    virtual ~Abstract() = default;
    virtual void unimplemented() = 0;
};

class Concrete : public Abstract {
public:
    void unimplemented() override {
        std::cout << "concrete\n";
    }
};

int main() {
    Concrete c;
    c.unimplemented();
    return 0;
}
