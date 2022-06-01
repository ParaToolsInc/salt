#include <stdio.h>
#include <stdlib.h>

void foo() {
	printf("42\n");
	int x = 2;
	if (x == 2) {
		exit(0);
	}
	else {
		return;
	}
}

int main(int argc, char* argv[]) {
	printf("hello world\n");
	foo();
	return 0;
}
