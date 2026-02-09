#include "memory_detect.h"

#include <dlfcn.h>
#include <execinfo.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "output_control.h"
#include "plthook.h"
#define TRACKER_DEBUG(...) ((void)0)
namespace tracker {
constexpr size_t kCallStackNum = 16;
struct AllocationInfo {
  size_t size;
  std::array<void*, kCallStackNum> callstack;
  size_t callstack_size;
};
class MemoryTracker {
 public:
  static auto GetInstance() -> MemoryTracker& {
    static MemoryTracker instance;
    return instance;
  }
  auto RecordAllocation(void* ptr, size_t size) -> void;
  auto RecordDeallocation(void* ptr) -> void;
  auto UpdateAllocationSize(void* ptr, size_t new_size) -> void;
  auto PrintStatus() const -> void;
  auto HasLeaks() -> bool;
  auto GetTotalAllocated() const -> size_t;
  auto GetActiveAllocations() const -> size_t;
  MemoryTracker(const MemoryTracker&) = delete;
  auto operator=(const MemoryTracker&) -> MemoryTracker& = delete;

 private:
  MemoryTracker() = default;
  mutable std::mutex mutex_;
  std::unordered_map<void*, AllocationInfo> allocations_;
  size_t total_allocated_ = 0;
  size_t total_freed_ = 0;
  size_t active_allocations_ = 0;
};
auto MemoryTracker::RecordAllocation(void* ptr, size_t size) -> void {
  if (ptr == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  AllocationInfo info;
  info.size = size;
  info.callstack_size =
      static_cast<size_t>(backtrace(info.callstack.data(), kCallStackNum));
  TRACKER_DEBUG("RecordAllocation: %p, size: %zu\n", ptr, size);
  allocations_[ptr] = info;
  total_allocated_ += size;
  active_allocations_++;
}
auto MemoryTracker::RecordDeallocation(void* ptr) -> void {
  if (ptr == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = allocations_.find(ptr);
  if (it != allocations_.end()) {
    total_freed_ += it->second.size;
    active_allocations_--;
    allocations_.erase(it);
  }
}
auto MemoryTracker::UpdateAllocationSize(void* ptr, size_t new_size) -> void {
  if (ptr == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = allocations_.find(ptr);
  if (it != allocations_.end()) {
    total_allocated_ = total_allocated_ - it->second.size + new_size;
    it->second.size = new_size;
    it->second.callstack_size = static_cast<size_t>(
        backtrace(it->second.callstack.data(), kCallStackNum));
  }
}
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto MemoryTracker::PrintStatus() const -> void {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& output = tracker::OutputControl::Instance();
  TRACKER_PRINT("\n\n=== Memory Tracker Status ===\n");
  TRACKER_PRINT("Total allocated: %zu bytes\n", total_allocated_);
  TRACKER_PRINT("Total freed: %zu bytes\n", total_freed_);
  TRACKER_PRINT("Active allocations: %zu\n", active_allocations_);
  TRACKER_PRINT("Potential leaks: ");
  output.PrintColored(
      allocations_.empty() ? tracker::Color::kGreen : tracker::Color::kBoldRed,
      tracker::Color::kReset, "%zu", allocations_.size());
  TRACKER_PRINT("\n");
  if (!allocations_.empty()) {
    TRACKER_PRINT("\n");
    output.PrintColored(tracker::Color::kBoldYellow, tracker::Color::kReset,
                        "Detailed leak information:");
    TRACKER_PRINT("\n");
    for (const auto& pair : allocations_) {
      const auto& info = pair.second;
      TRACKER_PRINT("\n");
      output.PrintColored(tracker::Color::kBoldRed, tracker::Color::kReset,
                          "Leak at %p (size: %zu bytes)", pair.first,
                          info.size);
      TRACKER_PRINT("\n");
      char** symbols = backtrace_symbols(info.callstack.data(),
                                         static_cast<int>(info.callstack_size));
      if (symbols != nullptr) {
        TRACKER_PRINT("Callstack:\n");
        size_t frame_index = 0;
        for (size_t i = 0; i < info.callstack_size; ++i) {
          void* abs_addr = info.callstack[i];
          Dl_info dlinfo;
          if (dladdr(abs_addr, &dlinfo) != 0) {
            bool is_detector_internal = false;
            if (dlinfo.dli_fname != nullptr) {
              if (strstr(dlinfo.dli_fname, "libnv_detector") != nullptr) {
                is_detector_internal = true;
              }
            }
            if (is_detector_internal) {
              continue;
            }
            void* rel_addr = reinterpret_cast<void*>(
                reinterpret_cast<char*>(abs_addr) -
                reinterpret_cast<char*>(dlinfo.dli_fbase));
            TRACKER_PRINT("  ");
            if (frame_index == 0) {
              output.PrintColored(tracker::Color::kBoldCyan,
                                  tracker::Color::kReset,
                                  "[%zu] Absolute: %p, Relative: %p",
                                  frame_index, abs_addr, rel_addr);
            } else {
              TRACKER_PRINT("[%zu] Absolute: %p, Relative: %p", frame_index,
                            abs_addr, rel_addr);
            }
            TRACKER_PRINT("\n");
            TRACKER_PRINT("      Module: %s\n", dlinfo.dli_fname);
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
                TRACKER_PRINT("      ");
                if (frame_index == 0) {
                  output.PrintColored(tracker::Color::kBoldCyan,
                                      tracker::Color::kReset, "Source: %s",
                                      line.data());
                } else {
                  TRACKER_PRINT("Source: %s", line.data());
                }
              }
              pclose(pipe);
            }
            frame_index++;
          } else {
            TRACKER_PRINT("  ");
            if (frame_index == 0) {
              output.PrintColored(tracker::Color::kBoldCyan,
                                  tracker::Color::kReset, "[%zu] %s",
                                  frame_index, symbols[i]);
            } else {
              TRACKER_PRINT("[%zu] %s", frame_index, symbols[i]);
            }
            TRACKER_PRINT("\n");
            frame_index++;
          }
        }
        free(symbols);
      }
    }
  }
  TRACKER_PRINT("\n===========================\n");
}
auto MemoryTracker::HasLeaks() -> bool {
  std::lock_guard<std::mutex> lock(mutex_);
  return !allocations_.empty();
}
auto MemoryTracker::GetTotalAllocated() const -> size_t {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_allocated_;
}
auto MemoryTracker::GetActiveAllocations() const -> size_t {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_allocations_;
}
auto Instance() -> MemoryTracker& { return MemoryTracker::GetInstance(); }
}  // namespace tracker
static auto HookedMalloc(size_t size) -> void* {
  TRACKER_DEBUG("HookedMalloc: %zu\n", size);
  void* ptr = malloc(size);
  tracker::Instance().RecordAllocation(ptr, size);
  return ptr;
}
static auto HookedFree(void* ptr) -> void {
  TRACKER_DEBUG("HookedFree: %p\n", ptr);
  tracker::Instance().RecordDeallocation(ptr);
  free(ptr);
}
static auto HookedCalloc(size_t nmemb, size_t size) -> void* {
  TRACKER_DEBUG("HookedCalloc: %zu, %zu\n", nmemb, size);
  void* ptr = calloc(nmemb, size);
  tracker::Instance().RecordAllocation(ptr, nmemb * size);
  return ptr;
}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuse-after-free"
static auto HookedRealloc(void* old_ptr, size_t new_size) -> void* {
  TRACKER_DEBUG("HookedRealloc: %p, %zu\n", old_ptr, new_size);
  auto old_addr = reinterpret_cast<std::uintptr_t>(old_ptr);
  void* new_ptr = realloc(old_ptr, new_size);
  if (new_ptr == nullptr) {
    return nullptr;
  }
  void* original_ptr = reinterpret_cast<void*>(old_addr);
  if (new_ptr == original_ptr) {
    tracker::Instance().UpdateAllocationSize(new_ptr, new_size);
  } else {
    tracker::Instance().RecordDeallocation(original_ptr);
    tracker::Instance().RecordAllocation(new_ptr, new_size);
  }
  return new_ptr;
}
#pragma GCC diagnostic pop
static auto HookedOperatorNew(size_t size) -> void* {
  TRACKER_DEBUG("HookedOperatorNew: %zu\n", size);
  void* ptr = malloc(size);
  tracker::Instance().RecordAllocation(ptr, size);
  return ptr;
}
static auto HookedOperatorDelete(void* ptr) noexcept -> void {
  TRACKER_DEBUG("HookedOperatorDelete: %p\n", ptr);
  if (ptr == nullptr) {
    return;
  }
  tracker::Instance().RecordDeallocation(ptr);
  free(ptr);
}
static auto HookedOperatorNewArray(size_t size) -> void* {
  TRACKER_DEBUG("HookedOperatorNewArray: %zu\n", size);
  void* ptr = malloc(size);
  tracker::Instance().RecordAllocation(ptr, size);
  return ptr;
}
static auto HookedOperatorDeleteArray(void* ptr) noexcept -> void {
  TRACKER_DEBUG("HookedOperatorDeleteArray: %p\n", ptr);
  if (ptr == nullptr) {
    return;
  }
  tracker::Instance().RecordDeallocation(ptr);
  free(ptr);
}
class MemoryHook {
 public:
  explicit MemoryHook(std::string lib_path) : lib_path_(std::move(lib_path)) {}
  ~MemoryHook() = default;
  auto Start() -> void;

 private:
  std::string lib_path_;
  std::unique_ptr<PltHook> hook_;
};
auto MemoryHook::Start() -> void {
  hook_ = PltHook::Create(lib_path_.c_str());
  try {
    std::vector<std::string> hooked_functions;
    std::vector<std::string> skipped_functions;
    if (hook_->ReplaceFunction("malloc", reinterpret_cast<void*>(&HookedMalloc),
                               nullptr) == PltHook::ErrorCode::kSuccess) {
      hooked_functions.emplace_back("malloc");
    } else {
      TRACKER_ERROR("Failed to hook malloc: %s",
                    PltHook::GetLastError().c_str());
    }
    if (hook_->ReplaceFunction("free", reinterpret_cast<void*>(&HookedFree),
                               nullptr) == PltHook::ErrorCode::kSuccess) {
      hooked_functions.emplace_back("free");
    } else {
      TRACKER_ERROR("Failed to hook free: %s", PltHook::GetLastError().c_str());
    }
    auto try_hook = [&](const char* symbol, void* hook_func,
                        const char* display_name) {
      if (hook_->ReplaceFunction(symbol, hook_func, nullptr) ==
          PltHook::ErrorCode::kSuccess) {
        hooked_functions.emplace_back(display_name);
      } else {
        skipped_functions.emplace_back(display_name);
      }
    };
    try_hook("calloc", reinterpret_cast<void*>(&HookedCalloc), "calloc");
    try_hook("realloc", reinterpret_cast<void*>(&HookedRealloc), "realloc");
    try_hook("_Znwm", reinterpret_cast<void*>(&HookedOperatorNew),
             "operator new");
    try_hook("_ZdlPv", reinterpret_cast<void*>(&HookedOperatorDelete),
             "operator delete");
    try_hook("_Znam", reinterpret_cast<void*>(&HookedOperatorNewArray),
             "operator new[]");
    try_hook("_ZdaPv", reinterpret_cast<void*>(&HookedOperatorDeleteArray),
             "operator delete[]");
    auto& output = tracker::OutputControl::Instance();
    output.PrintColored(tracker::Color::kGreen, tracker::Color::kReset,
                        "Successfully hooked functions: ");
    for (size_t i = 0; i < hooked_functions.size(); ++i) {
      TRACKER_PRINT("%s", hooked_functions[i].c_str());
      if (i < hooked_functions.size() - 1) {
        TRACKER_PRINT(", ");
      }
    }
    TRACKER_PRINT("\n");
    if (!skipped_functions.empty()) {
      output.PrintColored(tracker::Color::kYellow, tracker::Color::kReset,
                          "Skipped functions (not in PLT): ");
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
class MemoryDetectImpl {
 public:
  MemoryDetectImpl();
  ~MemoryDetectImpl() = default;
  auto Register(const std::string& lib_name) -> void;
  auto RegisterMain() -> void;
  auto Start() -> void;
  auto Detect() -> void;

 private:
  std::vector<std::unique_ptr<MemoryHook>> hooks_;
};
MemoryDetectImpl::MemoryDetectImpl() = default;
auto MemoryDetectImpl::Register(const std::string& lib_name) -> void {
  hooks_.emplace_back(std::make_unique<MemoryHook>(lib_name));
}
auto MemoryDetectImpl::RegisterMain() -> void {
  hooks_.emplace_back(std::make_unique<MemoryHook>(std::string()));
}
auto MemoryDetectImpl::Start() -> void {
  for (auto& hook : hooks_) {
    hook->Start();
  }
}
auto MemoryDetectImpl::Detect() -> void { tracker::Instance().PrintStatus(); }
MemoryDetect::MemoryDetect() : impl_(std::make_unique<MemoryDetectImpl>()) {}
auto MemoryDetect::Register(const std::string& lib_name) -> void {
  impl_->Register(lib_name);
}
auto MemoryDetect::RegisterMain() -> void { impl_->RegisterMain(); }
auto MemoryDetect::Start() -> void { impl_->Start(); }
auto MemoryDetect::Detect() -> void { impl_->Detect(); }
MemoryDetect::~MemoryDetect() { impl_.reset(); }
