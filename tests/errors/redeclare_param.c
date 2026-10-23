// expect: redeclaration of 'a' in the same scope
int f(int a) {
  int a;
  return a;
}
