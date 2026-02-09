#include "lock_detect.h"

#include <dlfcn.h>
#include <execinfo.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <mutex>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "output_control.h"
#include "plthook.h"
namespace tracker {
struct LockInfo {
  void* lock_addr = nullptr;
  pthread_t owner_thread = 0;
  std::vector<void*> callstack;
  std::unordered_set<void*> waiting_for;
  bool acquired = false;
};
struct ThreadInfo {
  std::vector<void*> held_locks;
  std::vector<void*> waiting_locks;
};
class LockTracker {
 public:
  static auto GetInstance() -> LockTracker& {
    static LockTracker instance;
    return instance;
  }
  LockTracker(const LockTracker&) = delete;
  auto operator=(const LockTracker&) -> LockTracker& = delete;
  auto RecordLockAcquire(pthread_mutex_t* mutex) -> void {
    if (mutex == nullptr) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    void* lock_addr = static_cast<void*>(mutex);
    pthread_t thread_id = pthread_self();
    auto it = active_locks_.find(lock_addr);
    if (it != active_locks_.end()) {
      if (it->second.acquired) {
        auto& waiting_thread_info = thread_info_[thread_id];
        waiting_thread_info.waiting_locks.push_back(lock_addr);
        for (void* held_lock : waiting_thread_info.held_locks) {
          auto& held_lock_info = active_locks_[held_lock];
          held_lock_info.waiting_for.insert(lock_addr);
        }
        DetectDeadlock(lock_addr, thread_id);
      }
    } else {
      LockInfo& info = active_locks_[lock_addr];
      info.lock_addr = lock_addr;
      info.acquired = false;
      GetCallStack(info.callstack);
    }
  }
  auto RecordLockAcquired(pthread_mutex_t* mutex) -> void {
    if (mutex == nullptr) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    void* lock_addr = static_cast<void*>(mutex);
    pthread_t thread_id = pthread_self();
    auto it = active_locks_.find(lock_addr);
    if (it != active_locks_.end()) {
      it->second.owner_thread = thread_id;
      it->second.acquired = true;
      auto& thread_locks = thread_info_[thread_id];
      thread_locks.held_locks.push_back(lock_addr);
      auto& waiting_locks = thread_locks.waiting_locks;
      std::erase(waiting_locks, lock_addr);
    }
  }
  auto RecordLockRelease(pthread_mutex_t* mutex) -> void {
    if (mutex == nullptr) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    void* lock_addr = static_cast<void*>(mutex);
    pthread_t thread_id = pthread_self();
    active_locks_.erase(lock_addr);
    auto it = thread_info_.find(thread_id);
    if (it != thread_info_.end()) {
      auto& held_locks = it->second.held_locks;
      std::erase(held_locks, lock_addr);
      if (held_locks.empty() && it->second.waiting_locks.empty()) {
        thread_info_.erase(it);
      }
    }
  }
  auto PrintStatus() const -> void;

 private:
  LockTracker() = default;
  auto GetCallStack(std::vector<void*>& callstack) -> void {
    constexpr size_t kMaxStackDepth = 16;
    std::array<void*, kMaxStackDepth> stack{};
    int size = backtrace(stack.data(), kMaxStackDepth);
    callstack.assign(stack.begin(), stack.begin() + size);
  }
  auto DetectDeadlock(void* lock_addr, pthread_t thread_id) -> bool;
  auto DetectDeadlockDFS(void* current_lock, pthread_t current_thread,
                         std::unordered_set<pthread_t>& visited_threads,
                         std::vector<std::pair<void*, pthread_t>>& lock_chain)
      -> bool;
  auto PrintLockInfo(const LockInfo& info) const -> void;
  auto PrintCallStack(const std::vector<void*>& callstack) const -> void;
  mutable std::mutex mutex_;
  std::unordered_map<void*, LockInfo> active_locks_;
  std::unordered_map<pthread_t, ThreadInfo> thread_info_;
};
static auto Instance() -> LockTracker& { return LockTracker::GetInstance(); }
auto LockTracker::DetectDeadlock(void* lock_addr, pthread_t thread_id) -> bool {
  std::unordered_set<pthread_t> visited_threads;
  std::vector<std::pair<void*, pthread_t>> lock_chain;
  bool found_deadlock =
      DetectDeadlockDFS(lock_addr, thread_id, visited_threads, lock_chain);
  if (found_deadlock) {
    TRACKER_PRINT("\n=== Potential Deadlock Detected! ===\n");
    TRACKER_PRINT("Lock chain:\n");
    for (const auto& pair : lock_chain) {
      const auto& info = active_locks_[pair.first];
      PrintLockInfo(info);
      TRACKER_PRINT("\n");
    }
  }
  return found_deadlock;
}
auto LockTracker::DetectDeadlockDFS(
    void* current_lock, pthread_t current_thread,
    std::unordered_set<pthread_t>& visited_threads,
    std::vector<std::pair<void*, pthread_t>>& lock_chain) -> bool {
  if (visited_threads.contains(current_thread)) {
    lock_chain.emplace_back(current_lock, current_thread);
    return true;
  }
  visited_threads.insert(current_thread);
  lock_chain.emplace_back(current_lock, current_thread);
  const auto& info = active_locks_[current_lock];
  for (void* waited_lock : info.waiting_for) {
    auto waited_lock_it = active_locks_.find(waited_lock);
    if (waited_lock_it == active_locks_.end()) {
      continue;
    }
    pthread_t owner_thread = waited_lock_it->second.owner_thread;
    if (DetectDeadlockDFS(waited_lock, owner_thread, visited_threads,
                          lock_chain)) {
      return true;
    }
  }
  visited_threads.erase(current_thread);
  lock_chain.pop_back();
  return false;
}
auto LockTracker::PrintLockInfo(const LockInfo& info) const -> void {
  TRACKER_PRINT("Lock %p (Mutex) held by thread %lu\n", info.lock_addr,
                info.owner_thread);
  TRACKER_PRINT("Acquired at:\n");
  char** symbols = backtrace_symbols(info.callstack.data(),
                                     static_cast<int>(info.callstack.size()));
  if (symbols != nullptr) {
    for (size_t i = 0; i < info.callstack.size(); ++i) {
      TRACKER_PRINT("  [%zu] %s\n", i, symbols[i]);
    }
    free(symbols);
  }
  if (!info.waiting_for.empty()) {
    TRACKER_PRINT("Waiting for locks:");
    for (void* waited_lock : info.waiting_for) {
      auto it = active_locks_.find(waited_lock);
      if (it != active_locks_.end()) {
        TRACKER_PRINT(" %p (held by thread %lu)", waited_lock,
                      it->second.owner_thread);
      } else {
        TRACKER_PRINT(" %p (unknown)", waited_lock);
      }
    }
    TRACKER_PRINT("\n");
  }
}
auto LockTracker::PrintStatus() const -> void {
  std::lock_guard<std::mutex> lock(mutex_);
  TRACKER_PRINT("\n=== Lock Detector Status ===\n");
  TRACKER_PRINT("Active locks: %zu\n", active_locks_.size());
  TRACKER_PRINT("Active threads: %zu\n", thread_info_.size());
  if (!active_locks_.empty()) {
    TRACKER_PRINT("\nDetailed lock information:\n");
    for (const auto& pair : active_locks_) {
      TRACKER_PRINT("\n");
      PrintLockInfo(pair.second);
    }
  }
  if (!thread_info_.empty()) {
    TRACKER_PRINT("\nThread Information:\n");
    for (const auto& pair : thread_info_) {
      TRACKER_PRINT("\nThread %lu:\n", pair.first);
      TRACKER_PRINT("  Held locks:");
      for (void* held_lock : pair.second.held_locks) {
        TRACKER_PRINT(" %p", held_lock);
      }
      TRACKER_PRINT("\n  Waiting for locks:");
      for (void* waited_lock : pair.second.waiting_locks) {
        auto it = active_locks_.find(waited_lock);
        if (it != active_locks_.end()) {
          TRACKER_PRINT(" %p (held by thread %lu)", waited_lock,
                        it->second.owner_thread);
        } else {
          TRACKER_PRINT(" %p", waited_lock);
        }
      }
      TRACKER_PRINT("\n");
    }
  }
  TRACKER_PRINT("\n===========================\n");
}
auto LockTracker::PrintCallStack(const std::vector<void*>& callstack) const
    -> void {
  char** symbols =
      backtrace_symbols(callstack.data(), static_cast<int>(callstack.size()));
  if (symbols != nullptr) {
    for (size_t i = 0; i < callstack.size(); ++i) {
      TRACKER_PRINT("  [%zu] %s\n", i, symbols[i]);
    }
    free(symbols);
  }
}
}  // namespace tracker
using PthreadMutexFunc = int (*)(pthread_mutex_t*);
static PthreadMutexFunc g_orig_mutex_lock = nullptr;
static PthreadMutexFunc g_orig_mutex_unlock = nullptr;
static PthreadMutexFunc g_orig_mutex_trylock = nullptr;
static auto HookedPthreadMutexLock(pthread_mutex_t* mutex) -> int {
  tracker::Instance().RecordLockAcquire(mutex);
  int result = g_orig_mutex_lock(mutex);
  if (result == 0) {
    tracker::Instance().RecordLockAcquired(mutex);
  }
  return result;
}
static auto HookedPthreadMutexUnlock(pthread_mutex_t* mutex) -> int {
  tracker::Instance().RecordLockRelease(mutex);
  return g_orig_mutex_unlock(mutex);
}
static auto HookedPthreadMutexTrylock(pthread_mutex_t* mutex) -> int {
  int result = g_orig_mutex_trylock(mutex);
  if (result == 0) {
    tracker::Instance().RecordLockAcquired(mutex);
  }
  return result;
}
class LockHook {
 public:
  explicit LockHook(std::string lib_path) : lib_path_(std::move(lib_path)) {}
  ~LockHook() = default;
  auto Start() -> void;

 private:
  std::string lib_path_;
  std::unique_ptr<PltHook> hook_;
};
auto LockHook::Start() -> void {
  hook_ = PltHook::Create(lib_path_.c_str());
  try {
    void* orig_lock = nullptr;
    if (hook_->ReplaceFunction("pthread_mutex_lock",
                               reinterpret_cast<void*>(&HookedPthreadMutexLock),
                               &orig_lock) != PltHook::ErrorCode::kSuccess) {
      TRACKER_ERROR("Failed to hook pthread_mutex_lock");
    } else {
      g_orig_mutex_lock = reinterpret_cast<PthreadMutexFunc>(orig_lock);
    }
    void* orig_unlock = nullptr;
    if (hook_->ReplaceFunction(
            "pthread_mutex_unlock",
            reinterpret_cast<void*>(&HookedPthreadMutexUnlock),
            &orig_unlock) != PltHook::ErrorCode::kSuccess) {
      TRACKER_ERROR("Failed to hook pthread_mutex_unlock");
    } else {
      g_orig_mutex_unlock = reinterpret_cast<PthreadMutexFunc>(orig_unlock);
    }
    void* orig_trylock = nullptr;
    if (hook_->ReplaceFunction(
            "pthread_mutex_trylock",
            reinterpret_cast<void*>(&HookedPthreadMutexTrylock),
            &orig_trylock) != PltHook::ErrorCode::kSuccess) {
      TRACKER_WARNING("Note: pthread_mutex_trylock not found in PLT");
    } else {
      g_orig_mutex_trylock = reinterpret_cast<PthreadMutexFunc>(orig_trylock);
    }
  } catch (const std::exception& e) {
    TRACKER_ERROR("Error starting lock tracking: %s", e.what());
  }
}
class LockDetectImpl {
 public:
  LockDetectImpl() = default;
  ~LockDetectImpl() = default;
  auto Register(const std::string& lib_name) -> void;
  auto RegisterMain() -> void;
  auto Start() -> void;
  auto Detect() -> void;

 private:
  std::vector<std::unique_ptr<LockHook>> hooks_;
};
auto LockDetectImpl::Register(const std::string& lib_name) -> void {
  hooks_.emplace_back(std::make_unique<LockHook>(lib_name));
}
auto LockDetectImpl::RegisterMain() -> void {
  hooks_.emplace_back(std::make_unique<LockHook>(std::string()));
}
auto LockDetectImpl::Start() -> void {
  for (auto& hook : hooks_) {
    hook->Start();
  }
}
auto LockDetectImpl::Detect() -> void { tracker::Instance().PrintStatus(); }
LockDetect::LockDetect() : impl_(std::make_unique<LockDetectImpl>()) {}
LockDetect::~LockDetect() = default;
auto LockDetect::Register(const std::string& lib_name) -> void {
  impl_->Register(lib_name);
}
auto LockDetect::RegisterMain() -> void { impl_->RegisterMain(); }
auto LockDetect::Start() -> void { impl_->Start(); }
auto LockDetect::Detect() -> void { impl_->Detect(); }
