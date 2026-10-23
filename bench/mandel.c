// Float-heavy: Mandelbrot set membership count on a 600x400 grid.
extern int print_int(int x);

int iterations(float cr, float ci) {
  float zr;
  float zi;
  float t;
  int k;
  zr = 0.0;
  zi = 0.0;
  k = 0;
  while (k < 200 && zr * zr + zi * zi < 4.0) {
    t = zr * zr - zi * zi + cr;
    zi = 2.0 * zr * zi + ci;
    zr = t;
    k = k + 1;
  }
  return k;
}

int main() {
  int x;
  int y;
  int inside;
  inside = 0;
  y = 0;
  while (y < 400) {
    x = 0;
    while (x < 600) {
      if (iterations(-2.0 + 3.0 * x / 600, -1.0 + 2.0 * y / 400) == 200) {
        inside = inside + 1;
      }
      x = x + 1;
    }
    y = y + 1;
  }
  print_int(inside);
  return 0;
}
