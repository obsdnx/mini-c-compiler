// expect: redeclaration of 'x' in the same scope
int main() {
  int x;
  float x;
  return 0;
}
