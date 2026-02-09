#pragma once
#include <cstdarg>
#include <string>
#include "detector.h"
namespace tracker {
namespace Color {
constexpr const char* kReset = "\033[0m";
constexpr const char* kRed = "\033[31m";
constexpr const char* kGreen = "\033[32m";
constexpr const char* kYellow = "\033[33m";
constexpr const char* kBlue = "\033[34m";
constexpr const char* kMagenta = "\033[35m";
constexpr const char* kCyan = "\033[36m";
constexpr const char* kBoldRed = "\033[1;31m";
constexpr const char* kBoldGreen = "\033[1;32m";
constexpr const char* kBoldYellow = "\033[1;33m";
constexpr const char* kBoldCyan = "\033[1;36m";
}  
class OutputControl {
 public:
  static auto Instance() -> OutputControl& {
    static OutputControl instance;
    return instance;
  }
  auto Configure(OutputOption option, const std::string& filename = "") -> void;
  auto Print(const char* format, ...) const -> void;
  auto PrintToFile(const char* format, ...) const -> void;
  auto PrintToConsole(const char* format, ...) const -> void;
  [[nodiscard]] auto GetOutputFile() const -> FILE* { return output_file_; }
  auto PrintColored(const char* color_start, const char* color_end,
                    const char* format, ...) const -> void;
  OutputControl(const OutputControl&) = delete;
  auto operator=(const OutputControl&) -> OutputControl& = delete;
 private:
  OutputControl() = default;
  ~OutputControl();
  auto OpenOutputFile() -> void;
  auto CloseOutputFile() -> void;
  OutputOption output_option_ = OutputOption::kOutputOptionConsoleFile;
  std::string output_file_name_;
  FILE* output_file_ = nullptr;
};
}  
#define TRACKER_PRINT(...) tracker::OutputControl::Instance().Print(__VA_ARGS__)
#define TRACKER_PRINT_FILE(...) \
  tracker::OutputControl::Instance().PrintToFile(__VA_ARGS__)
#define TRACKER_PRINT_CONSOLE(...) \
  tracker::OutputControl::Instance().PrintToConsole(__VA_ARGS__)
#define TRACKER_PRINT_IF(condition, ...) \
  do {                                   \
    if (condition) {                     \
      TRACKER_PRINT(__VA_ARGS__);        \
    }                                    \
  } while (0)
#define TRACKER_ERROR(...) TRACKER_PRINT("ERROR: " __VA_ARGS__)
#define TRACKER_WARNING(...) TRACKER_PRINT("WARNING: " __VA_ARGS__)
#ifdef DEBUG
#define TRACKER_DEBUG(...) TRACKER_PRINT("DEBUG: " __VA_ARGS__)
#else
#define TRACKER_DEBUG(...) ((void)0)
#endif
