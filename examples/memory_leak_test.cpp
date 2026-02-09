#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "detector.h"
auto TestMallocLeak() -> void {
  printf("\n=== Test 1: malloc leak ===\n");
  void* ptr = malloc(100);
  if (ptr != nullptr) {
    printf("Allocated 100 bytes with malloc at %p\n", ptr);
  }
}
auto TestNewLeak() -> void {
  printf("\n=== Test 2: new leak ===\n");
  int* num = new int(42);
  printf("Allocated int with new at %p, value = %d\n", static_cast<void*>(num),
         *num);
}
auto TestNewArrayLeak() -> void {
  printf("\n=== Test 3: new[] array leak ===\n");
  int* arr = new int[50];
  printf("Allocated int[50] with new[] at %p\n", static_cast<void*>(arr));
  for (int i = 0; i < 50; ++i) {
    arr[i] = i;
  }
}
auto TestCallocLeak() -> void {
  printf("\n=== Test 4: calloc leak ===\n");
  auto* data = static_cast<double*>(calloc(20, sizeof(double)));
  if (data != nullptr) {
    printf("Allocated 20 doubles with calloc at %p\n",
           static_cast<void*>(data));
  }
}
auto TestPartialFreeLeak() -> void {
  printf("\n=== Test 5: partial free leak ===\n");
  void* ptr1 = malloc(64);
  void* ptr2 = malloc(128);
  void* ptr3 = malloc(256);
  printf("Allocated 3 blocks: %p, %p, %p\n", ptr1, ptr2, ptr3);
  void* freed_ptr = ptr2;
  free(ptr2);
  printf("Freed middle block %p\n", freed_ptr);
}
auto TestNoLeak() -> void {
  printf("\n=== Test 6: no leak (correct usage) ===\n");
  void* ptr = malloc(512);
  printf("Allocated 512 bytes at %p\n", ptr);
  void* freed_ptr = ptr;
  free(ptr);
  printf("Freed %p - no leak!\n", freed_ptr);
}
auto TestStrdupLeak() -> void {
  printf("\n=== Test 7: strdup leak ===\n");
  const char* original = "This is a test string for memory leak detection";
  char* copy = strdup(original);
  if (copy != nullptr) {
    printf("Duplicated string at %p: \"%s\"\n", static_cast<void*>(copy), copy);
  }
}
auto TestReallocLeak() -> void {
  printf("\n=== Test 8: realloc leak ===\n");
  void* ptr = malloc(64);
  printf("Initial allocation: %p (64 bytes)\n", ptr);
  void* new_ptr = realloc(ptr, 1024 * 1024);  
  printf("After realloc to 1MB: %p\n", new_ptr);
  if (new_ptr != nullptr) {
    memset(new_ptr, 0, 1024 * 1024);
    printf("Memory initialized at %p\n", new_ptr);
  }
}
auto TestReallocInPlace() -> void {
  printf("\n=== Test 9: realloc in-place (no leak) ===\n");
  void* ptr = malloc(1024);
  printf("Initial allocation: %p (1024 bytes)\n", ptr);
  void* new_ptr = realloc(ptr, 2048);
  printf("After realloc to 2KB: %p (same address: %s)\n", new_ptr,
         (new_ptr == ptr) ? "yes" : "no");
  if (new_ptr != nullptr) {
    free(new_ptr);
    printf("Freed %p - no leak!\n", new_ptr);
  }
}
auto main() -> int {
  printf("========================================\n");
  printf("Memory Leak Detection Test\n");
  printf("========================================\n");
  printf("\n>>> Initializing detector...\n");
  DetectorInit("./logs",                   
               kDetectorOptionMemory,      
               kOutputOptionConsoleFile);  
  printf(">>> Registering main program...\n");
  DetectorRegisterMain();
  printf(">>> Starting detector...\n");
  DetectorStart();
  printf("\n========================================\n");
  printf("Running test cases...\n");
  printf("========================================\n");
  TestMallocLeak();       
  TestNewLeak();          
  TestNewArrayLeak();     
  TestCallocLeak();       
  TestPartialFreeLeak();  
  TestNoLeak();           
  TestStrdupLeak();       
  TestReallocLeak();      
  TestReallocInPlace();   
  printf("\n========================================\n");
  printf("All test cases completed\n");
  printf("========================================\n");
  printf("\n>>> Detecting memory leaks...\n");
  DetectorDetect();
  printf("\n========================================\n");
  printf("Test finished\n");
  printf("========================================\n");
  return 0;
}
