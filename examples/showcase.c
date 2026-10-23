// A tour of Mini-C: every stage of the compiler has something to show here.
extern int print_int(int x);
extern float print_float(float x);

int calls; // globals are zero-initialised

// Recursion: at -O2, LLVM turns this into a loop (tail-recursion elimination).
int factorial(int n) {
  if (n <= 1) {
    return 1;
  }
  return n * factorial(n - 1);
}

// int -> float widening is implicit; float -> int would be a compile error.
float mean(int a, int b, float c) {
  return (a + b + c) / 3;
}

bool noisy(bool v) {
  calls = calls + 1;
  return v;
}

int main() {
  int i;
  float total;
  i = 0;
  total = 0.0;
  while (i < 5) {
    total = total + mean(i, i + 1, 0.5);
    i = i + 1;
  }
  print_int(factorial(10));
  print_float(total);

  // && is lazy: noisy() runs once, not twice.
  if (noisy(false) && noisy(true)) {
    print_int(-1);
  }
  print_int(calls);
  return 0;
}
