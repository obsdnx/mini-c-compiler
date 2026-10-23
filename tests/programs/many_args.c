// More than 8 integer and 8 float arguments: exercises AAPCS64 stack-argument passing.
extern int print_int(int x);
extern float print_float(float x);

int sum10(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) {
  return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i + 10 * j;
}

float mixed(int a, float b, int c, float d, int e, float f, int g, float h, int i, float j,
            int k, float l, int m, float n, int o, float p, int q, float r, bool s, bool t, int u) {
  float regs;
  regs = a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p;
  print_float(regs);
  print_int(q);
  print_float(r);
  print_int(s);
  print_int(t);
  print_int(u);
  return regs + q + r + u;
}

int main() {
  print_int(sum10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
  print_int(sum10(1, 2, 3, 4, 5, 6, 7, 8, sum10(1, 1, 1, 1, 1, 1, 1, 1, 1, 1), 10));
  print_float(mixed(1, 0.5, 2, 1.5, 3, 2.5, 4, 3.5, 5, 4.5, 6, 5.5, 7, 6.5, 8, 7.5,
                    100, 0.25, true, false, 1000));
  return 0;
}
