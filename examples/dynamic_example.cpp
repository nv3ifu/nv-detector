#include <cstdio>

extern "C" {
// 简单的加法函数，用于测试 PLT Hook
// NOLINTNEXTLINE(readability-identifier-length)
auto SimpleAdd(int a, int b) -> int {
  printf("SimpleAdd called: %d + %d = %d\n", a, b, a + b);
  return a + b;
}
}
