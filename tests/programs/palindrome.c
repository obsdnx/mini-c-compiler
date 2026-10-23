extern int print_int(int x);

bool palindrome(int number) {
  int t;
  int rev;
  int rmndr;
  rev = 0;
  t = number;
  while (number > 0) {
    rmndr = number % 10;
    rev = rev * 10 + rmndr;
    number = number / 10;
  }
  return t == rev;
}

int main() {
  print_int(palindrome(12321));
  print_int(palindrome(12345));
  print_int(palindrome(9));
  return 0;
}
