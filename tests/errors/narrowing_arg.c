// expect: narrowing conversion from float to int in argument 1
int f(int a) {
  return a;
}
int main() {
  return f(2.5);
}
