/**
 * @file memory_detect.cc
 * @brief 内存检测模块实现文件
 *
 * 该文件实现了内存泄漏检测的核心功能，通过hook内存分配和释放函数来跟踪内存使用情况。
 * 主要功能包括：
 * 1. 记录所有内存分配和释放操作
 * 2. 捕获内存分配时的调用栈
 * 3. 检测并报告内存泄漏
 * 4. 提供详细的泄漏信息，包括泄漏地址、大小和调用栈
 */

#include "memory_detect.h"

#include <dlfcn.h>     // 用于动态链接库操作，如dladdr函数
#include <execinfo.h>  // 用于获取调用栈，如backtrace函数
#include <unistd.h>    // 系统调用，如getpagesize函数

#include <array>          // std::array，用于固定大小数组
#include <cstdio>         // 标准输入输出
#include <cstdlib>        // 标准库函数
#include <cstring>        // 字符串操作函数
#include <mutex>          // 互斥锁，保证线程安全
#include <unordered_map>  // 哈希表，用于存储内存分配信息
#include <vector>         // 动态数组

#include "output_control.h"  // 输出控制模块
#include "plthook.h"         // PLT钩子模块，用于函数替换

// 调试输出宏，用于输出内存分配和释放的详细信息
// 在非DEBUG模式下被禁用，避免产生过多日志
#define TRACKER_DEBUG(...) ((void)0)

namespace tracker {

/**
 * @struct AllocationInfo
 * @brief 内存分配信息结构体
 *
 * 记录每次内存分配的详细信息，包括大小和调用栈
 */
constexpr size_t kCallStackNum = 16;
struct AllocationInfo {
  size_t size;  // 分配的内存大小（字节数）
  std::array<void*, kCallStackNum>
      callstack;          // 调用栈地址数组，最多记录16层调用
  size_t callstack_size;  // 实际记录的调用栈深度
};

/**
 * @class MemoryTracker
 * @brief 内存跟踪器类
 *
 * 负责记录和管理内存分配信息，实现单例模式
 * 提供内存分配、释放的记录功能和内存泄漏检测功能
 */
class MemoryTracker {
 public:
  /**
   * @brief 获取单例实例
   * @return MemoryTracker& 单例实例的引用
   *
   * 使用静态局部变量确保线程安全的单例初始化
   */
  static auto GetInstance() -> MemoryTracker& {
    static MemoryTracker instance;
    return instance;
  }

  /**
   * @brief 记录内存分配
   * @param ptr 分配的内存地址
   * @param size 分配的内存大小
   *
   * 记录内存分配信息，包括地址、大小和调用栈
   */
  auto RecordAllocation(void* ptr, size_t size) -> void;

  /**
   * @brief 记录内存释放
   * @param ptr 释放的内存地址
   *
   * 从记录中移除已释放的内存分配信息
   */
  auto RecordDeallocation(void* ptr) -> void;

  /**
   * @brief 打印内存状态
   *
   * 输出内存使用统计信息和潜在的内存泄漏详情
   * 包括泄漏地址、大小和调用栈的符号解析
   */
  auto PrintStatus() const -> void;

  auto HasLeaks() -> bool;

  /**
   * @brief 获取总分配内存大小
   * @return size_t 总分配内存大小（字节数）
   */
  auto GetTotalAllocated() const -> size_t;

  /**
   * @brief 获取活跃的内存分配数量
   * @return size_t 当前未释放的内存分配数量
   */
  auto GetActiveAllocations() const -> size_t;
  MemoryTracker(const MemoryTracker&) = delete;
  auto operator=(const MemoryTracker&) -> MemoryTracker& = delete;

 private:
  /**
   * @brief 私有构造函数
   *
   * 初始化计数器，实现单例模式
   */
  MemoryTracker() = default;

  // 禁用拷贝构造和赋值操作，确保单例性质

  mutable std::mutex mutex_;  // 互斥锁，保护内部数据结构的线程安全

  // 记录所有活跃的内存分配，键为内存地址，值为分配信息
  std::unordered_map<void*, AllocationInfo> allocations_;

  size_t total_allocated_ = 0;     // 总共分配的内存大小（字节数）
  size_t total_freed_ = 0;         // 总共释放的内存大小（字节数）
  size_t active_allocations_ = 0;  // 活跃的内存分配数量（未释放的分配次数）
};

/**
 * @brief 记录内存分配
 * @param ptr 分配的内存地址
 * @param size 分配的内存大小
 *
 * 记录内存分配信息，包括地址、大小和调用栈
 * 线程安全，使用互斥锁保护数据结构
 */
auto MemoryTracker::RecordAllocation(void* ptr, size_t size) -> void {
  if (ptr == nullptr) {
    return;  // 忽略空指针，避免无效记录
  }

  std::lock_guard<std::mutex> lock(mutex_);  // 加锁保护数据结构

  AllocationInfo info;
  info.size = size;

  // 获取调用栈，最多记录16层
  info.callstack_size =
      static_cast<size_t>(backtrace(info.callstack.data(), kCallStackNum));

  TRACKER_DEBUG("RecordAllocation: %p, size: %zu\n", ptr, size);

  allocations_[ptr] = info;  // 记录分配信息到哈希表
  total_allocated_ += size;  // 更新总分配大小
  active_allocations_++;     // 更新活跃分配数量
}

/**
 * @brief 记录内存释放
 * @param ptr 释放的内存地址
 *
 * 从记录中移除已释放的内存分配信息
 * 线程安全，使用互斥锁保护数据结构
 */
auto MemoryTracker::RecordDeallocation(void* ptr) -> void {
  if (ptr == nullptr) {
    return;  // 忽略空指针，避免无效操作
  }

  std::lock_guard<std::mutex> lock(mutex_);  // 加锁保护数据结构

  auto it = allocations_.find(ptr);
  if (it != allocations_.end()) {
    total_freed_ += it->second.size;  // 更新总释放大小
    active_allocations_--;            // 更新活跃分配数量
    allocations_.erase(it);           // 从记录中移除该分配
  }
}

/**
 * @brief 打印内存状态
 *
 * 输出内存使用统计信息和潜在的内存泄漏详情
 * 包括泄漏地址、大小和调用栈的符号解析
 * 线程安全，使用互斥锁保护数据结构
 */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto MemoryTracker::PrintStatus() const -> void {
  std::lock_guard<std::mutex> lock(mutex_);  // 加锁保护数据结构

  // 打印内存使用统计信息（前面添加换行分隔）
  TRACKER_PRINT("\n\n=== Memory Tracker Status ===\n");
  TRACKER_PRINT("Total allocated: %zu bytes\n", total_allocated_);
  TRACKER_PRINT("Total freed: %zu bytes\n", total_freed_);
  TRACKER_PRINT("Active allocations: %zu\n", active_allocations_);
  TRACKER_PRINT(
      "Potential leaks: %s%zu%s\n",
      allocations_.empty() ? tracker::Color::kGreen : tracker::Color::kBoldRed,
      allocations_.size(), tracker::Color::kReset);

  // 如果有泄漏，打印详细信息
  if (!allocations_.empty()) {
    TRACKER_PRINT("\n%sDetailed leak information:%s\n",
                  tracker::Color::kBoldYellow, tracker::Color::kReset);
    for (const auto& pair : allocations_) {
      const auto& info = pair.second;
      TRACKER_PRINT("\n%sLeak at %p (size: %zu bytes)%s\n",
                    tracker::Color::kBoldRed, pair.first, info.size,
                    tracker::Color::kReset);

      // 解析调用栈，将地址转换为符号名称
      char** symbols = backtrace_symbols(info.callstack.data(),
                                         static_cast<int>(info.callstack_size));
      if (symbols != nullptr) {
        TRACKER_PRINT("Callstack:\n");
        size_t frame_index = 0;  // 用户代码的帧索引
        for (size_t i = 0; i < info.callstack_size; ++i) {
          void* abs_addr = info.callstack[i];

          // 获取该地址所属的模块信息
          Dl_info dlinfo;
          if (dladdr(abs_addr, &dlinfo) != 0) {
            // 检查是否是检测工具内部的调用（通过模块名判断）
            bool is_detector_internal = false;
            if (dlinfo.dli_fname != nullptr) {
              // 检查模块名是否包含 "libnv_detector"
              if (strstr(dlinfo.dli_fname, "libnv_detector") != nullptr) {
                is_detector_internal = true;
              }
            }

            // 跳过检测工具内部的调用栈
            if (is_detector_internal) {
              continue;
            }

            // 计算相对地址（相对于模块基址）
            void* rel_addr = reinterpret_cast<void*>(
                reinterpret_cast<char*>(abs_addr) -
                reinterpret_cast<char*>(dlinfo.dli_fbase));

            // 第一帧（泄漏发生的位置）使用高亮颜色
            const char* color =
                (frame_index == 0) ? tracker::Color::kBoldCyan : "";
            const char* reset =
                (frame_index == 0) ? tracker::Color::kReset : "";

            TRACKER_PRINT("  %s[%zu] Absolute: %p, Relative: %p%s\n", color,
                          frame_index, abs_addr, rel_addr, reset);
            TRACKER_PRINT("      Module: %s\n", dlinfo.dli_fname);

            // 使用addr2line工具解析源代码位置
            constexpr size_t kCmdBufferSize = 256;
            std::array<char, kCmdBufferSize> cmd{};
            snprintf(cmd.data(), cmd.size(), "addr2line -e \"%s\" -f -C -p %p",
                     dlinfo.dli_fname, rel_addr);

            FILE* pipe = popen(cmd.data(), "r");
            if (pipe != nullptr) {
              constexpr size_t kLineBufferSize = 256;
              std::array<char, kLineBufferSize> line{};
              // NOLINTNEXTLINE(clang-analyzer-unix.BlockInCriticalSection)
              if (fgets(line.data(), line.size(), pipe) != nullptr) {
                // 第一帧的源代码位置也高亮显示
                TRACKER_PRINT("      %sSource: %s%s", color, line.data(),
                              reset);
              }
              pclose(pipe);
            }

            frame_index++;  // 只有用户代码才增加索引
          } else {
            // 如果无法获取模块信息，直接打印符号（假设是用户代码）
            const char* color =
                (frame_index == 0) ? tracker::Color::kBoldCyan : "";
            const char* reset =
                (frame_index == 0) ? tracker::Color::kReset : "";
            TRACKER_PRINT("  %s[%zu] %s%s\n", color, frame_index, symbols[i],
                          reset);
            frame_index++;
          }
        }
        free(symbols);  // 释放backtrace_symbols分配的内存
      }
    }
  }

  TRACKER_PRINT("\n===========================\n");
}

/**
 * @brief 检查是否有内存泄漏
 * @return bool 如果有泄漏返回true，否则返回false
 *
 * 线程安全，使用互斥锁保护数据结构
 */
auto MemoryTracker::HasLeaks() -> bool {
  std::lock_guard<std::mutex> lock(mutex_);
  return !allocations_.empty();
}

/**
 * @brief 获取总分配内存大小
 * @return size_t 总分配内存大小（字节数）
 *
 * 线程安全，使用互斥锁保护数据结构
 */
auto MemoryTracker::GetTotalAllocated() const -> size_t {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_allocated_;
}

/**
 * @brief 获取活跃的内存分配数量
 * @return size_t 当前未释放的内存分配数量
 *
 * 线程安全，使用互斥锁保护数据结构
 */
auto MemoryTracker::GetActiveAllocations() const -> size_t {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_allocations_;
}

/**
 * @brief 获取MemoryTracker单例实例的辅助函数
 * @return MemoryTracker& 单例实例的引用
 *
 * 提供一个简短的函数名，方便调用
 */
auto Instance() -> MemoryTracker& { return MemoryTracker::GetInstance(); }

}  // namespace tracker

/**
 * @brief 钩子函数：替换malloc
 * @param size 请求分配的内存大小
 * @return void* 分配的内存地址
 *
 * 拦截malloc调用，记录分配信息后调用原始malloc
 */
static auto HookedMalloc(size_t size) -> void* {
  TRACKER_DEBUG("HookedMalloc: %zu\n", size);

  void* ptr = malloc(size);  // 调用原始malloc

  // 使用单例记录分配
  tracker::Instance().RecordAllocation(ptr, size);
  return ptr;
}

/**
 * @brief 钩子函数：替换free
 * @param ptr 要释放的内存地址
 *
 * 拦截free调用，记录释放信息后调用原始free
 */
static auto HookedFree(void* ptr) -> void {
  TRACKER_DEBUG("HookedFree: %p\n", ptr);

  // 使用单例记录释放
  tracker::Instance().RecordDeallocation(ptr);

  free(ptr);  // 调用原始free
}

/**
 * @brief 钩子函数：替换calloc
 * @param nmemb 元素数量
 * @param size 每个元素的大小
 * @return void* 分配的内存地址
 *
 * 拦截calloc调用，记录分配信息后调用原始calloc
 */
static auto HookedCalloc(size_t nmemb, size_t size) -> void* {
  TRACKER_DEBUG("HookedCalloc: %zu, %zu\n", nmemb, size);

  void* ptr = calloc(nmemb, size);  // 调用原始calloc

  // 使用单例记录分配
  tracker::Instance().RecordAllocation(ptr, nmemb * size);
  return ptr;
}

/**
 * @brief 钩子函数：替换realloc
 * @param old_ptr 原内存地址
 * @param new_size 新的内存大小
 * @return void* 重新分配后的内存地址
 *
 * 拦截realloc调用，记录释放和分配信息后调用原始realloc
 */
static auto HookedRealloc(void* old_ptr, size_t new_size) -> void* {
  TRACKER_DEBUG("HookedRealloc: %p, %zu\n", old_ptr, new_size);

  // 使用单例记录释放
  tracker::Instance().RecordDeallocation(old_ptr);

  void* new_ptr = realloc(old_ptr, new_size);  // 调用原始realloc

  // 使用单例记录分配
  tracker::Instance().RecordAllocation(new_ptr, new_size);
  return new_ptr;
}

/**
 * @brief 钩子函数：替换operator new
 * @param size 请求分配的内存大小
 * @return void* 分配的内存地址
 *
 * 拦截C++的new操作符，记录分配信息
 */
static auto HookedOperatorNew(size_t size) -> void* {
  TRACKER_DEBUG("HookedOperatorNew: %zu\n", size);

  void* ptr = malloc(size);  // 使用malloc实现

  // 使用单例记录分配
  tracker::Instance().RecordAllocation(ptr, size);
  return ptr;
}

/**
 * @brief 钩子函数：替换operator delete
 * @param ptr 要释放的内存地址
 *
 * 拦截C++的delete操作符，记录释放信息
 */
static auto HookedOperatorDelete(void* ptr) noexcept -> void {
  TRACKER_DEBUG("HookedOperatorDelete: %p\n", ptr);

  if (ptr == nullptr) {
    return;  // 忽略空指针
  }

  // 使用单例记录释放
  tracker::Instance().RecordDeallocation(ptr);

  free(ptr);  // 使用free实现
}

/**
 * @brief 钩子函数：替换operator new[]
 * @param size 请求分配的内存大小
 * @return void* 分配的内存地址
 *
 * 拦截C++的new[]操作符，记录分配信息
 */
static auto HookedOperatorNewArray(size_t size) -> void* {
  TRACKER_DEBUG("HookedOperatorNewArray: %zu\n", size);

  void* ptr = malloc(size);  // 使用malloc实现

  // 使用单例记录分配
  tracker::Instance().RecordAllocation(ptr, size);
  return ptr;
}

/**
 * @brief 钩子函数：替换operator delete[]
 * @param ptr 要释放的内存地址
 *
 * 拦截C++的delete[]操作符，记录释放信息
 */
static auto HookedOperatorDeleteArray(void* ptr) noexcept -> void {
  TRACKER_DEBUG("HookedOperatorDeleteArray: %p\n", ptr);

  if (ptr == nullptr) {
    return;  // 忽略空指针
  }

  // 使用单例记录释放
  tracker::Instance().RecordDeallocation(ptr);

  free(ptr);  // 使用free实现
}

/**
 * @class MemoryHook
 * @brief 内存Hook类
 *
 * 负责替换特定库中的内存分配和释放函数
 * 使用PLTHook机制实现函数替换
 */
class MemoryHook {
 public:
  /**
   * @brief 构造函数
   * @param lib_path 要hook的库路径，空字符串表示主程序
   */
  explicit MemoryHook(std::string lib_path) : lib_path_(std::move(lib_path)) {}
  ~MemoryHook() = default;

  /**
   * @brief 开始hook操作
   *
   * 替换目标库中的内存分配和释放函数
   */
  auto Start() -> void;

 private:
  std::string lib_path_;           // 库路径
  std::unique_ptr<PltHook> hook_;  // PLT hook实例
};

/**
 * @brief 开始hook操作
 *
 * 替换目标库中的内存分配和释放函数
 * 包括malloc/free/calloc/realloc和C++的new/delete操作符
 */
auto MemoryHook::Start() -> void {
  // 创建PltHook实例，指定目标库
  hook_ = PltHook::Create(lib_path_.c_str());

  try {
    std::vector<std::string> hooked_functions;
    std::vector<std::string> skipped_functions;

    // 替换malloc函数
    if (hook_->ReplaceFunction("malloc", reinterpret_cast<void*>(&HookedMalloc),
                               nullptr) == PltHook::ErrorCode::kSuccess) {
      hooked_functions.push_back("malloc");
    } else {
      TRACKER_ERROR("Failed to hook malloc: %s",
                    PltHook::GetLastError().c_str());
    }

    // 替换free函数
    if (hook_->ReplaceFunction("free", reinterpret_cast<void*>(&HookedFree),
                               nullptr) == PltHook::ErrorCode::kSuccess) {
      hooked_functions.push_back("free");
    } else {
      TRACKER_ERROR("Failed to hook free: %s", PltHook::GetLastError().c_str());
    }

    // 替换calloc函数（可能不存在于PLT表中）
    if (hook_->ReplaceFunction("calloc", reinterpret_cast<void*>(&HookedCalloc),
                               nullptr) == PltHook::ErrorCode::kSuccess) {
      hooked_functions.push_back("calloc");
    } else {
      skipped_functions.push_back("calloc");
    }

    // 替换realloc函数（可能不存在于PLT表中）
    if (hook_->ReplaceFunction("realloc",
                               reinterpret_cast<void*>(&HookedRealloc),
                               nullptr) == PltHook::ErrorCode::kSuccess) {
      hooked_functions.push_back("realloc");
    } else {
      skipped_functions.push_back("realloc");
    }

    // 替换operator new函数（C++特有）
    if (hook_->ReplaceFunction("_Znwm",  // operator new的符号名
                               reinterpret_cast<void*>(&HookedOperatorNew),
                               nullptr) == PltHook::ErrorCode::kSuccess) {
      hooked_functions.push_back("operator new");
    } else {
      skipped_functions.push_back("operator new");
    }

    // 替换operator delete函数（C++特有）
    if (hook_->ReplaceFunction("_ZdlPv",  // operator delete的符号名
                               reinterpret_cast<void*>(&HookedOperatorDelete),
                               nullptr) == PltHook::ErrorCode::kSuccess) {
      hooked_functions.push_back("operator delete");
    } else {
      skipped_functions.push_back("operator delete");
    }

    // 替换operator new[]函数（C++特有）
    if (hook_->ReplaceFunction("_Znam",  // operator new[]的符号名
                               reinterpret_cast<void*>(&HookedOperatorNewArray),
                               nullptr) == PltHook::ErrorCode::kSuccess) {
      hooked_functions.push_back("operator new[]");
    } else {
      skipped_functions.push_back("operator new[]");
    }

    // 替换operator delete[]函数（C++特有）
    if (hook_->ReplaceFunction(
            "_ZdaPv",  // operator delete[]的符号名
            reinterpret_cast<void*>(&HookedOperatorDeleteArray),
            nullptr) == PltHook::ErrorCode::kSuccess) {
      hooked_functions.push_back("operator delete[]");
    } else {
      skipped_functions.push_back("operator delete[]");
    }

    // 输出成功 hook 的函数列表
    TRACKER_PRINT("%sSuccessfully hooked functions:%s ", tracker::Color::kGreen,
                  tracker::Color::kReset);
    for (size_t i = 0; i < hooked_functions.size(); ++i) {
      TRACKER_PRINT("%s", hooked_functions[i].c_str());
      if (i < hooked_functions.size() - 1) {
        TRACKER_PRINT(", ");
      }
    }
    TRACKER_PRINT("\n");

    // 输出跳过的函数列表
    if (!skipped_functions.empty()) {
      TRACKER_PRINT("%sSkipped functions (not in PLT):%s ",
                    tracker::Color::kYellow, tracker::Color::kReset);
      for (size_t i = 0; i < skipped_functions.size(); ++i) {
        TRACKER_PRINT("%s", skipped_functions[i].c_str());
        if (i < skipped_functions.size() - 1) {
          TRACKER_PRINT(", ");
        }
      }
      TRACKER_PRINT("\n");
    }

  } catch (const std::exception& e) {
    TRACKER_ERROR("Error starting memory tracking: %s", e.what());
  }
}

/**
 * @class MemoryDetectImpl
 * @brief 内存检测实现类
 *
 * 管理多个库的内存hook，实现MemoryDetect接口
 */
class MemoryDetectImpl {
 public:
  MemoryDetectImpl();
  ~MemoryDetectImpl() = default;

  /**
   * @brief 注册指定库进行内存检测
   * @param lib_name 库名称或路径
   *
   * 将指定的库添加到hook列表中
   */
  auto Register(const std::string& lib_name) -> void;

  /**
   * @brief 注册主程序进行内存检测
   *
   * 将主程序添加到hook列表中
   */
  auto RegisterMain() -> void;

  /**
   * @brief 开始内存检测
   *
   * 启动所有注册的库的hook
   */
  auto Start() -> void;

  /**
   * @brief 检测并报告内存泄漏
   *
   * 打印当前内存状态和潜在的泄漏信息
   */
  auto Detect() -> void;

 private:
  // 存储所有需要hook的库
  std::vector<std::unique_ptr<MemoryHook>> hooks_;
};

/**
 * @brief MemoryDetectImpl构造函数
 *
 * 初始化内存检测实现类
 */
MemoryDetectImpl::MemoryDetectImpl() = default;

/**
 * @brief 注册指定库进行内存检测
 * @param lib_name 库名称或路径
 *
 * 将指定的库添加到hook列表中
 */
auto MemoryDetectImpl::Register(const std::string& lib_name) -> void {
  hooks_.emplace_back(std::make_unique<MemoryHook>(lib_name));
}

/**
 * @brief 注册主程序进行内存检测
 *
 * 将主程序添加到hook列表中，使用空字符串表示主程序
 */
auto MemoryDetectImpl::RegisterMain() -> void {
  hooks_.emplace_back(std::make_unique<MemoryHook>(std::string()));
}

/**
 * @brief 开始内存检测
 *
 * 启动所有注册的库的hook
 */
auto MemoryDetectImpl::Start() -> void {
  for (auto& hook : hooks_) {
    hook->Start();  // 启动每个库的hook
  }
}

/**
 * @brief 检测并报告内存泄漏
 *
 * 打印当前内存状态和潜在的泄漏信息
 */
auto MemoryDetectImpl::Detect() -> void {
  tracker::Instance().PrintStatus();  // 打印内存状态
}

// MemoryDetect类的实现，提供公共接口

/**
 * @brief MemoryDetect构造函数
 *
 * 创建实现类实例
 */
MemoryDetect::MemoryDetect() : impl_(std::make_unique<MemoryDetectImpl>()) {}

/**
 * @brief 注册指定库进行内存检测
 * @param lib_name 库名称或路径
 */
auto MemoryDetect::Register(const std::string& lib_name) -> void {
  impl_->Register(lib_name);
}

/**
 * @brief 注册主程序进行内存检测
 */
auto MemoryDetect::RegisterMain() -> void { impl_->RegisterMain(); }

/**
 * @brief 开始内存检测
 */
auto MemoryDetect::Start() -> void { impl_->Start(); }

/**
 * @brief 检测并报告内存泄漏
 */
auto MemoryDetect::Detect() -> void { impl_->Detect(); }

/**
 * @brief MemoryDetect析构函数
 *
 * 释放实现类实例
 */
MemoryDetect::~MemoryDetect() {
  impl_.reset();  // 释放实现类
}
