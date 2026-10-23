// Loop- and division-heavy: count primes below 2,000,000 by trial division.
extern int print_int(int x);

bool is_prime(int n) {
  int d;
  if (n < 2) {
    return false;
  }
  d = 2;
  while (d * d <= n) {
    if (n % d == 0) {
      return false;
    }
    d = d + 1;
  }
  return true;
}

int main() {
  int i;
  int count;
  i = 0;
  count = 0;
  while (i < 2000000) {
    if (is_prime(i)) {
      count = count + 1;
    }
    i = i + 1;
  }
  print_int(count);
  return 0;
}
