/**
 * @file memory_leak_test.cpp
 * @brief 内存泄漏检测示例程序
 *
 * 该程序演示如何使用 NvDetector SDK 检测各种类型的内存泄漏
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "detector.h"

/**
 * @brief 演示 malloc 内存泄漏
 */
auto TestMallocLeak() -> void {
  printf("\n=== Test 1: malloc leak ===\n");

  // 分配 100 字节但不释放
  void* ptr = malloc(100);
  if (ptr != nullptr) {
    printf("Allocated 100 bytes with malloc at %p\n", ptr);
    // 故意不调用 free(ptr) - 这会造成内存泄漏
  }
}

/**
 * @brief 演示 new 内存泄漏
 */
auto TestNewLeak() -> void {
  printf("\n=== Test 2: new leak ===\n");

  // 分配单个 int 但不释放
  int* num = new int(42);
  printf("Allocated int with new at %p, value = %d\n", static_cast<void*>(num),
         *num);
  // 故意不调用 delete num - 这会造成内存泄漏
}

/**
 * @brief 演示 new[] 数组内存泄漏
 */
auto TestNewArrayLeak() -> void {
  printf("\n=== Test 3: new[] array leak ===\n");

  // 分配 50 个 int 的数组但不释放
  int* arr = new int[50];
  printf("Allocated int[50] with new[] at %p\n", static_cast<void*>(arr));

  // 初始化数组
  for (int i = 0; i < 50; ++i) {
    arr[i] = i;
  }

  // 故意不调用 delete[] arr - 这会造成内存泄漏
}

/**
 * @brief 演示 calloc 内存泄漏
 */
auto TestCallocLeak() -> void {
  printf("\n=== Test 4: calloc leak ===\n");

  // 分配 20 个 double 但不释放
  double* data = static_cast<double*>(calloc(20, sizeof(double)));
  if (data != nullptr) {
    printf("Allocated 20 doubles with calloc at %p\n",
           static_cast<void*>(data));
    // 故意不调用 free(data) - 这会造成内存泄漏
  }
}

/**
 * @brief 演示部分释放导致的内存泄漏
 */
auto TestPartialFreeLeak() -> void {
  printf("\n=== Test 5: partial free leak ===\n");

  // 分配多个内存块
  void* ptr1 = malloc(64);
  void* ptr2 = malloc(128);
  void* ptr3 = malloc(256);

  printf("Allocated 3 blocks: %p, %p, %p\n", ptr1, ptr2, ptr3);

  // 只释放第二个
  void* freed_ptr = ptr2;
  free(ptr2);
  printf("Freed middle block %p\n", freed_ptr);

  // ptr1 和 ptr3 没有释放 - 这会造成内存泄漏
}

/**
 * @brief 演示正确的内存管理（无泄漏）
 */
auto TestNoLeak() -> void {
  printf("\n=== Test 6: no leak (correct usage) ===\n");

  // 分配并正确释放
  void* ptr = malloc(512);
  printf("Allocated 512 bytes at %p\n", ptr);

  void* freed_ptr = ptr;
  free(ptr);
  printf("Freed %p - no leak!\n", freed_ptr);
}

/**
 * @brief 演示 strdup 内存泄漏
 */
auto TestStrdupLeak() -> void {
  printf("\n=== Test 7: strdup leak ===\n");

  const char* original = "This is a test string for memory leak detection";
  char* copy = strdup(original);

  if (copy != nullptr) {
    printf("Duplicated string at %p: \"%s\"\n", static_cast<void*>(copy), copy);
    // 故意不调用 free(copy) - 这会造成内存泄漏
  }
}

/**
 * @brief 主函数
 */
auto main() -> int {
  printf("========================================\n");
  printf("Memory Leak Detection Test\n");
  printf("========================================\n");

  // ========== 初始化检测器 ==========
  printf("\n>>> Initializing detector...\n");
  DetectorInit("./logs",                   // 日志目录
               kDetectorOptionMemory,      // 检测内存泄漏
               kOutputOptionConsoleFile);  // 输出到控制台和文件

  // ========== 注册主程序 ==========
  printf(">>> Registering main program...\n");
  DetectorRegisterMain();

  // ========== 启动检测 ==========
  printf(">>> Starting detector...\n");
  DetectorStart();

  printf("\n========================================\n");
  printf("Running test cases...\n");
  printf("========================================\n");

  // ========== 运行测试用例 ==========
  TestMallocLeak();       // malloc 泄漏
  TestNewLeak();          // new 泄漏
  TestNewArrayLeak();     // new[] 泄漏
  TestCallocLeak();       // calloc 泄漏
  TestPartialFreeLeak();  // 部分释放泄漏
  TestNoLeak();           // 无泄漏（正确示例）
  TestStrdupLeak();       // strdup 泄漏

  printf("\n========================================\n");
  printf("All test cases completed\n");
  printf("========================================\n");

  // ========== 检测泄漏 ==========
  printf("\n>>> Detecting memory leaks...\n");
  DetectorDetect();

  printf("\n========================================\n");
  printf("Test finished\n");
  printf("========================================\n");

  return 0;
}
