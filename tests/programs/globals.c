// Globals of every type are zero-initialised.
extern int print_int(int x);
extern float print_float(float x);

int gi;
float gf;
bool gb;

void set() {
  gi = 42;
  gf = 2.5;
  gb = true;
}

int main() {
  print_int(gi);
  print_float(gf);
  print_int(gb);
  set();
  print_int(gi);
  print_float(gf);
  print_int(gb);
  return 0;
}
