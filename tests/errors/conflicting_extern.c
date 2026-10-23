// expect: conflicting declaration
extern int f(int x);
float f(int x) {
  return 1.0;
}
