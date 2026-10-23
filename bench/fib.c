// Call-heavy: naive recursive Fibonacci.
extern int print_int(int x);

int fib(int n) {
  if (n < 2) {
    return n;
  }
  return fib(n - 1) + fib(n - 2);
}

int main() {
  print_int(fib(35));
  return 0;
}
