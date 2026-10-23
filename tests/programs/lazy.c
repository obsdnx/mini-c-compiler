// && and || must short-circuit: the right operand runs only when needed.
extern int print_int(int x);

int calls;

bool touch(bool v) {
  calls = calls + 1;
  return v;
}

int main() {
  bool r;
  calls = 0;
  r = false && touch(true);
  print_int(calls);
  r = true || touch(true);
  print_int(calls);
  r = true && touch(false);
  print_int(calls);
  print_int(r);
  r = false || touch(true);
  print_int(calls);
  print_int(r);
  if (calls > 100 && touch(true)) {
    print_int(999);
  }
  print_int(calls);
  return 0;
}
