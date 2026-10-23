// bool semantics: promotion to int, truthiness in conditions, precedence of comparisons.
extern int print_int(int x);

int main() {
  bool a;
  bool b;
  int n;
  float f;
  a = true;
  b = !a;
  print_int(a);
  print_int(b);
  print_int(a + a);
  print_int(a == b);
  print_int(a != b);
  n = 5;
  print_int(!n);
  print_int(!!n);
  f = 0.0;
  if (f) {
    print_int(111);
  } else {
    print_int(222);
  }
  print_int(-a);
  print_int(1 < 2 == true);
  print_int(3 > 2 > 1);
  return 0;
}
