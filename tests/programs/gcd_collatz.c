extern int print_int(int x);

int gcd(int a, int b) {
  if (b == 0) {
    return a;
  }
  return gcd(b, a % b);
}

int collatz(int n) {
  int steps;
  steps = 0;
  while (n != 1) {
    if (n % 2 == 0) {
      n = n / 2;
    } else {
      n = 3 * n + 1;
    }
    steps = steps + 1;
  }
  return steps;
}

int main() {
  print_int(gcd(1071, 462));
  print_int(gcd(17, 5));
  print_int(collatz(27));
  print_int(collatz(97));
  return 0;
}
