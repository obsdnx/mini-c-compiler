// Short-circuit operators and '!' inside if/while conditions.
extern int print_int(int x);

int calls;

bool t(bool v) {
  calls = calls + 1;
  return v;
}

int main() {
  int i;
  calls = 0;
  if (t(false) || t(true) && !t(false)) { print_int(1); } else { print_int(0); }
  print_int(calls);
  if (!(t(true) || t(true))) { print_int(1); } else { print_int(0); }
  print_int(calls);
  i = 0;
  while (i < 5 && !(i == 3)) {
    i = i + 1;
  }
  print_int(i);
  if (i) { print_int(10); }
  if (!i) { print_int(20); }
  return 0;
}
