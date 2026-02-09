#pragma once
#include <memory>
#include <string>
class MemoryDetectImpl;
class MemoryDetect {
 public:
  static auto GetInstance() -> MemoryDetect& {
    static MemoryDetect instance;
    return instance;
  }
  void Register(const std::string& lib_name);
  void RegisterMain();
  void Start();
  void Detect();
  ~MemoryDetect();
 private:
  MemoryDetect();
  std::unique_ptr<MemoryDetectImpl> impl_;
};
