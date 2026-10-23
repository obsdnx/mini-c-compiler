// Float arithmetic and int -> float widening.
extern float print_float(float x);
extern int print_int(int x);

float pi(int terms) {
  float sum;
  float sign;
  int k;
  sum = 0.0;
  sign = 1.0;
  k = 0;
  while (k < terms) {
    sum = sum + sign / (2 * k + 1); // int divisor widened to float
    sign = -sign;
    k = k + 1;
  }
  return 4 * sum;
}

float average(int a, int b, float c) {
  return (a + b + c) / 3;
}

int main() {
  float x;
  print_float(pi(1000));
  print_float(average(1, 2, 3.5));
  x = 7;
  print_float(x / 2);
  print_float(7.5 % 2);
  print_float(-x);
  print_int(1.5 < 2);
  return 0;
}
