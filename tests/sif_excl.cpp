#include <iostream>

void keep_routine() {
    std::cout << "kept\n";
}

void excluded_routine() {
    std::cout << "excluded\n";
}

int main() {
    keep_routine();
    excluded_routine();
    return 0;
}
