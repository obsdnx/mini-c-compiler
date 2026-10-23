extern int print_int(int x);

int counter;

void bump(int by) {
  counter = counter + by;
  return;
}

void report() {
  print_int(counter);
}

int first_divisor(int n) {
  int d;
  d = 2;
  while (d * d <= n) {
    if (n % d == 0) {
      return d;
    }
    d = d + 1;
  }
  return n;
}

int main() {
  int i;
  int j;
  int primes;
  counter = 0;
  bump(5);
  bump(10);
  report();
  ;
  ;
  i = 2;
  primes = 0;
  while (i < 1000) {
    if (first_divisor(i) == i) {
      primes = primes + 1;
    }
    i = i + 1;
  }
  print_int(primes);
  i = 0;
  while (i < 10) {
    j = 0;
    while (j < i) j = j + 1;
    i = i + 1;
    counter = counter + j;
  }
  report();
  return 0;
}
