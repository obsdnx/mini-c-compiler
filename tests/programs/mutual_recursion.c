// is_even calls is_odd before it is defined: sema collects all signatures first.
extern int print_int(int x);

bool is_even(int n) {
  if (n == 0) {
    return true;
  }
  return is_odd(n - 1);
}

bool is_odd(int n) {
  if (n == 0) {
    return false;
  }
  return is_even(n - 1);
}

int main() {
  print_int(is_even(10));
  print_int(is_odd(7));
  print_int(is_even(7));
  return 0;
}
