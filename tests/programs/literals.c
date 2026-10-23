// Immediates that need movz/movk/movn sequences on AArch64.
extern int print_int(int x);
extern float print_float(float x);

int main() {
  print_int(2147483647);
  print_int(-2147483647 - 1);
  print_int(65535);
  print_int(65536);
  print_int(-65536);
  print_int(70000 * 3);
  print_float(.5);
  print_float(123456.75);
  return 0;
}
