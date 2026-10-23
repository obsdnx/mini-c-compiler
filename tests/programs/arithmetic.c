// Precedence, associativity and integer semantics.
extern int print_int(int x);

int main() {
  print_int(2 + 3 * 4);             // 14
  print_int((2 + 3) * 4);           // 20
  print_int(10 - 3 - 2);            // 5: left associative despite the right-recursive grammar
  print_int(100 / 10 / 5);          // 2
  print_int(17 % 5);                // 2
  print_int(-7 / 2);                // -3: truncation toward zero
  print_int(-7 % 2);                // -1
  print_int(- -5);                  // 5
  print_int(2 * -3 + 1);            // -5
  print_int(1 + 2 * 3 - 4 / 2 % 3); // 5
  return 0;
}
