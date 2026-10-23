extern int print_int(int x);

int fib_rec(int n) {
  if (n < 2) {
    return n;
  }
  return fib_rec(n - 1) + fib_rec(n - 2);
}

int fib_iter(int n) {
  int a;
  int b;
  int t;
  int i;
  a = 0;
  b = 1;
  i = 0;
  while (i < n) {
    t = a + b;
    a = b;
    b = t;
    i = i + 1;
  }
  return a;
}

int main() {
  int i;
  i = 0;
  while (i <= 10) {
    print_int(fib_iter(i));
    i = i + 1;
  }
  print_int(fib_rec(20));
  print_int(fib_iter(40));
  return 0;
}
