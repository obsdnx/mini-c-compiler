// Runtime support linked with every Mini-C program (Mini-C has no I/O of its own).
#include <stdio.h>

int print_int(int x) {
  printf("%d\n", x);
  return 0;
}

float print_float(float x) {
  printf("%f\n", x);
  return 0.0f;
}
