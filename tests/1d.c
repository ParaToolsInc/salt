#include <stdio.h>

int main() {
  int N = 100;

  int a[N], b[N], c[N];

  for (int i = 0; i < N; i++) {
    a[i] = 1;
    b[i] = 2;
    c[i] = 0;
  }

  for (int i = 0; i < N; i++) {
    c[i] = a[i] + b[i];
  }

  for (int i = 0; i < N; i++) {
    if (c[i] != 3) {
      printf("ERROR at i = %d\n", i);
      return -1;
    }
  }

  printf("All good\n");
  return 0;
}
