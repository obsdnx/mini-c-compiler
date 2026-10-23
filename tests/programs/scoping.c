// Block scoping and shadowing.
extern int print_int(int x);

int x;

int shadow(int x) {
  {
    int x;
    x = 100;
    print_int(x);
  }
  return x;
}

int main() {
  int y;
  x = 1;
  y = shadow(42);
  print_int(y);
  print_int(x);
  {
    float x;
    x = 2.5;
    {
      int y;
      y = 7;
      print_int(y);
    }
  }
  print_int(y);
  print_int(x);
  return 0;
}
