// IEEE-754 NaN: every ordered comparison is false and != is true, both as a
// value and as a branch condition (checks the inverted condition codes).
extern int print_int(int x);

int main() {
  float n;
  n = 0.0 / 0.0;
  print_int(n < 1.0);
  print_int(n <= 1.0);
  print_int(n > 1.0);
  print_int(n >= 1.0);
  print_int(n == n);
  print_int(n != n);
  if (n < 1.0) { print_int(1); } else { print_int(0); }
  if (n > 1.0) { print_int(1); } else { print_int(0); }
  if (n != n) { print_int(1); } else { print_int(0); }
  if (!(n >= 1.0)) { print_int(1); } else { print_int(0); }
  if (n) { print_int(1); } else { print_int(0); }
  return 0;
}
