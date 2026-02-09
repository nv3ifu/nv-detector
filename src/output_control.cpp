#include "output_control.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <array>
#include <cstring>
#include <ctime>
namespace tracker {
auto OutputControl::Configure(OutputOption option, const std::string& filename)
    -> void {
  CloseOutputFile();
  output_option_ = option;
  output_file_name_ = filename;
  if (option != OutputOption::kOutputOptionConsole) {
    OpenOutputFile();
  }
}
auto OutputControl::Print(const char* format, ...) const -> void {
  va_list args1;
  va_list args2;
  va_start(args1, format);
  if (output_option_ == OutputOption::kOutputOptionConsoleFile ||
      output_option_ == OutputOption::kOutputOptionConsole) {
    va_copy(args2, args1);
    vprintf(format, args2);
    va_end(args2);
  }
  if ((output_option_ == OutputOption::kOutputOptionConsoleFile ||
       output_option_ == OutputOption::kOutputOptionFile) &&
      output_file_ != nullptr) {
    vfprintf(output_file_, format, args1);
    fflush(output_file_);
  }
  va_end(args1);
}
auto OutputControl::PrintToFile(const char* format, ...) const -> void {
  if (output_file_ == nullptr ||
      output_option_ == OutputOption::kOutputOptionConsole) {
    return;
  }
  va_list args;
  va_start(args, format);
  vfprintf(output_file_, format, args);
  fflush(output_file_);
  va_end(args);
}
auto OutputControl::PrintToConsole(const char* format, ...) const -> void {
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
}
auto OutputControl::PrintColored(const char* color_start, const char* color_end,
                                 const char* format, ...) const -> void {
  va_list args1, args2;
  va_start(args1, format);
  if (output_option_ == OutputOption::kOutputOptionConsoleFile ||
      output_option_ == OutputOption::kOutputOptionConsole) {
    va_copy(args2, args1);
    printf("%s", color_start);
    vprintf(format, args2);
    printf("%s", color_end);
    va_end(args2);
  }
  if ((output_option_ == OutputOption::kOutputOptionConsoleFile ||
       output_option_ == OutputOption::kOutputOptionFile) &&
      output_file_ != nullptr) {
    vfprintf(output_file_, format, args1);
    fflush(output_file_);
  }
  va_end(args1);
}
auto OutputControl::OpenOutputFile() -> void {
  std::string filename = output_file_name_;
  if (filename.empty()) {
    time_t now = time(nullptr);
    constexpr size_t kTimestampBufferSize = 32;
    std::array<char, kTimestampBufferSize> timestamp{};
    strftime(timestamp.data(), timestamp.size(), "%Y%m%d_%H%M%S_",
             localtime(&now));
    filename = std::string(timestamp.data()) + "detector.log";
  }
  size_t last_slash = filename.find_last_of('/');
  if (last_slash != std::string::npos) {
    std::string dir_path = filename.substr(0, last_slash);
    std::string cmd = "mkdir -p \"" + dir_path + "\" 2>/dev/null";
    system(cmd.c_str());
  }
  output_file_ = fopen(filename.c_str(), "w");
  if (output_file_ == nullptr) {
    printf("Failed to open output file: %s\n", filename.c_str());
  }
}
auto OutputControl::CloseOutputFile() -> void {
  if (output_file_ != nullptr) {
    fclose(output_file_);
    output_file_ = nullptr;
  }
}
OutputControl::~OutputControl() { CloseOutputFile(); }
}  
