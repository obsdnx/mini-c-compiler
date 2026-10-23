// Argument evaluation order and register shuffling at call sites.
extern int print_int(int x);
extern float print_float(float x);

int pick(int a, int b, int c) {
  return a * 100 + b * 10 + c;
}

float fpick(float a, int b, float c) {
  return a * 100 + b * 10 + c;
}

int main() {
  int x;
  int y;
  float f;
  x = 1;
  y = 2;
  f = 3.5;
  print_int(pick(x, y, 3));
  print_int(pick(y, x, x + y));
  print_int(pick(3, x * y, y - x));
  print_int(pick(pick(1, 2, 3) % 10, x, y));
  print_float(fpick(f, x, 7.5 % 2));
  print_float(fpick(1.0, y, f));
  print_float(fpick(f % 2, 3, f));
  return 0;
}
