#include "plthook.h"

#include <dlfcn.h>

#include <cstdarg>
#include <cstdio>
#include <iostream>

using PrintfFunc = int (*)(const char *format, ...);

// 全局变量保存原始 printf 函数指针
static PrintfFunc g_original_printf = nullptr;

auto HookedPrintf(const char *format, ...) -> int {
  // 先打印 hook 信息
  if (g_original_printf != nullptr) {
    g_original_printf("[HOOKED] ");
  }

  // 调用原始 printf，传递所有参数
  va_list args;
  va_start(args, format);
  int result = 0;
  if (g_original_printf != nullptr) {
    result = vprintf(format, args);  // 使用 vprintf 传递 va_list
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

    // 获取SimpleAdd函数来测试
    using SimpleAddFunc = int (*)(int, int);
    auto simple_add =
        reinterpret_cast<SimpleAddFunc>(dlsym(lib_handle, "SimpleAdd"));
    if (simple_add == nullptr) {
      std::cerr << "Failed to get SimpleAdd: " << dlerror() << std::endl;
      dlclose(lib_handle);
      return 1;
    }

    std::cout << "Before hook, calling SimpleAdd(1, 2):" << std::endl;
    simple_add(1, 2);  // 这会调用原始的printf

    // 创建PltHook实例
    auto hook = PltHook::Create("./libdynamic_example.so");

    // 保存原始printf函数指针
    void *original_printf = nullptr;

    // 替换printf函数
    if (hook->ReplaceFunction("printf", reinterpret_cast<void *>(HookedPrintf),
                              &original_printf) !=
        PltHook::ErrorCode::kSuccess) {
      std::cerr << "Failed to hook printf: " << PltHook::GetLastError()
                << std::endl;
      dlclose(lib_handle);
      return 1;
    }

    // 保存原始函数指针到全局变量
    g_original_printf = reinterpret_cast<PrintfFunc>(original_printf);

    std::cout << "\nSuccessfully hooked printf\n" << std::endl;

    // 测试被hook的函数
    std::cout << "After hook, calling SimpleAdd(1, 2):" << std::endl;
    simple_add(1, 2);  // 这会调用被hook的printf

    simple_add(1, 2);  // 这会调用被hook的printf
    // 清理
    dlclose(lib_handle);
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
