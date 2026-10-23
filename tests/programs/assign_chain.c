// Assignment is a right-associative expression.
extern int print_int(int x);
extern float print_float(float x);

int main() {
  int a;
  int b;
  int c;
  float f;
  a = b = c = 7;
  print_int(a + b + c);
  f = a = 3;
  print_float(f);
  print_int(a);
  return 0;
}
