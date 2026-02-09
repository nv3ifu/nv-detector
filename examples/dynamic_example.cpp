#include <cstdio>
extern "C" {
auto SimpleAdd(int a, int b) -> int {
  printf("SimpleAdd called: %d + %d = %d\n", a, b, a + b);
  return a + b;
}
}
