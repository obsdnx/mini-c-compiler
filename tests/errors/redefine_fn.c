// expect: redefinition of function 'f'
int f() {
  return 1;
}
int f() {
  return 2;
}
