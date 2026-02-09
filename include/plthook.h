#pragma once
#include <memory>
#include <string>
class __attribute__((visibility("default"))) PltHook {
 public:
  enum class ErrorCode {
    kSuccess = 0,
    kFileNotFound = -1,
    kInvalidArgument = -2,
    kFunctionNotFound = -3,
    kInternalError = -4,
    kEofReached = -5,
  };
  static auto Create(const char* filename) -> std::unique_ptr<PltHook>;
  auto EnumerateSymbols(unsigned int& pos, const char*& name_out,
                        void**& addr_out) const -> ErrorCode;
  auto ReplaceFunction(const char* funcname, void* newfunc,
                       void** oldfunc = nullptr) -> ErrorCode;
  static auto GetLastError() -> const std::string&;
  ~PltHook();
  PltHook(const PltHook&) = delete;
  auto operator=(const PltHook&) -> PltHook& = delete;
 private:
  struct Impl;
  std::unique_ptr<Impl> pimpl_;
  explicit PltHook(struct link_map* lmap);
};
