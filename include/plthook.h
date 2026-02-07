#pragma once

#include <memory>
#include <string>

class __attribute__((visibility("default"))) PltHook {
 public:
  // 错误代码
  enum class ErrorCode {
    kSuccess = 0,
    kFileNotFound = -1,
    kInvalidArgument = -2,
    kFunctionNotFound = -3,
    kInternalError = -4,
    kEofReached = -5,
  };

  /**
   * @brief 创建指定库的PLT钩子
   * @param filename 库的名称（如果为nullptr则表示主程序）
   * @return 指向PltHook实例的唯一指针
   * @throws std::runtime_error 创建失败时抛出异常
   *
   * 该函数创建一个PltHook实例，用于操作指定库的PLT表。
   * 如果filename为nullptr，则操作主程序的PLT表。
   */
  static auto Create(const char* filename) -> std::unique_ptr<PltHook>;

  /**
   * @brief 枚举PLT表中的符号
   * @param pos 位置指针（输入/输出参数）
   * @param name_out 用于存储函数名的引用
   * @param addr_out 用于存储函数地址的引用
   * @return 成功返回kSuccess，没有更多条目时返回kEofReached
   *
   * 该函数用于遍历PLT表中的所有符号，每次调用返回一个符号的信息。
   * 首次调用时pos应设为0，后续调用使用上次调用后的pos值。
   * 当没有更多符号时，返回kEofReached。
   */
  auto EnumerateSymbols(unsigned int& pos, const char*& name_out,
                        void**& addr_out) const -> ErrorCode;

  /**
   * @brief 替换PLT表中的函数
   * @param funcname 要替换的函数名
   * @param newfunc 新的函数指针
   * @param oldfunc 可选参数，用于存储原始函数指针
   * @return 成功返回kSuccess，失败返回错误代码
   *
   * 该函数用于替换PLT表中指定名称的函数。
   * 替换后，对原函数的所有调用都会被重定向到newfunc。
   * 如果提供了oldfunc参数，原始函数的地址会被存储在其中，
   * 这样可以在新函数中调用原始函数，实现函数调用的拦截和监控。
   */
  auto ReplaceFunction(const char* funcname, void* newfunc,
                       void** oldfunc = nullptr) -> ErrorCode;

  /**
   * @brief 获取最近的错误信息
   * @return 错误信息字符串
   *
   * 当操作失败时，可以通过该函数获取详细的错误信息。
   * 返回的是静态存储的字符串，不需要调用者释放。
   */
  static auto GetLastError() -> const std::string&;

  /**
   * @brief 析构函数
   *
   * 释放所有资源，包括内部分配的内存和打开的文件句柄。
   */
  ~PltHook();
  PltHook(const PltHook&) = delete;
  auto operator=(const PltHook&) -> PltHook& = delete;

 private:
  // 实现细节隐藏在头文件之外
  struct Impl;
  std::unique_ptr<Impl> pimpl_;

  // 私有构造函数 - 使用Create()代替
  explicit PltHook(struct link_map* lmap);
};
