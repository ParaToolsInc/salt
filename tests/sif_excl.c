#include <stdio.h>

void keep_routine(void) {
    printf("kept\n");
}

void excluded_routine(void) {
    printf("excluded\n");
}

int main(void) {
    keep_routine();
    excluded_routine();
    return 0;
}
