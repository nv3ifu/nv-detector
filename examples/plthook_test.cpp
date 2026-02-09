#include "plthook.h"
#include <dlfcn.h>
#include <cstdarg>
#include <cstdio>
#include <iostream>
using PrintfFunc = int (*)(const char *format, ...);
static PrintfFunc g_original_printf = nullptr;
auto HookedPrintf(const char *format, ...) -> int {
  if (g_original_printf != nullptr) {
    g_original_printf("[HOOKED] ");
  }
  va_list args;
  va_start(args, format);
  int result = 0;
  if (g_original_printf != nullptr) {
    result = vprintf(format, args);  
  }
  va_end(args);
  return result;
}
auto main() -> int {
  try {
    void *lib_handle = dlopen("./libdynamic_example.so", RTLD_LAZY);
    if (lib_handle == nullptr) {
      std::cerr << "Failed to load library: " << dlerror() << std::endl;
      return 1;
    }
    using SimpleAddFunc = int (*)(int, int);
    auto simple_add =
        reinterpret_cast<SimpleAddFunc>(dlsym(lib_handle, "SimpleAdd"));
    if (simple_add == nullptr) {
      std::cerr << "Failed to get SimpleAdd: " << dlerror() << std::endl;
      dlclose(lib_handle);
      return 1;
    }
    std::cout << "Before hook, calling SimpleAdd(1, 2):" << std::endl;
    simple_add(1, 2);  
    auto hook = PltHook::Create("./libdynamic_example.so");
    void *original_printf = nullptr;
    if (hook->ReplaceFunction("printf", reinterpret_cast<void *>(HookedPrintf),
                              &original_printf) !=
        PltHook::ErrorCode::kSuccess) {
      std::cerr << "Failed to hook printf: " << PltHook::GetLastError()
                << std::endl;
      dlclose(lib_handle);
      return 1;
    }
    g_original_printf = reinterpret_cast<PrintfFunc>(original_printf);
    std::cout << "\nSuccessfully hooked printf\n" << std::endl;
    std::cout << "After hook, calling SimpleAdd(1, 2):" << std::endl;
    simple_add(1, 2);  
    simple_add(1, 2);  
    dlclose(lib_handle);
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
