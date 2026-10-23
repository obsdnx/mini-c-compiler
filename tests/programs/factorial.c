// Iterative and recursive factorial.
extern int print_int(int x);

int fact_iter(int n) {
  int result;
  result = 1;
  while (n > 1) {
    result = result * n;
    n = n - 1;
  }
  return result;
}

int fact_rec(int n) {
  if (n <= 1) {
    return 1;
  }
  return n * fact_rec(n - 1);
}

int main() {
  print_int(fact_iter(10));
  print_int(fact_rec(10));
  return 0;
}
