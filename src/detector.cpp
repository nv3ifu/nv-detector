#include "detector.h"

#include <ctime>
#include <string>

#include "lock_detect.h"
#include "memory_detect.h"
#include "output_control.h"
auto GetFilePath(std::string work_dir) -> std::string {
  std::string output_file_name =
      work_dir + "/detector_" + std::to_string(time(nullptr)) + ".log";
  return output_file_name;
}
extern "C" {
static DetectorOption detector_option = kDetectorOptionMemoryLock;
__attribute__((visibility("default"))) auto DetectorInit(
    const char* work_dir, DetectorOption detect_option,
    OutputOption output_option) -> void {
  detector_option = detect_option;
  std::string output_file_name = GetFilePath(work_dir);
  tracker::OutputControl::Instance().Configure(output_option, output_file_name);
}
__attribute__((visibility("default"))) auto DetectorStart(void) -> void {
  if ((detector_option & kDetectorOptionMemory) != 0) {
    MemoryDetect::GetInstance().Start();
  }
  if ((detector_option & kDetectorOptionLock) != 0) {
    LockDetect::GetInstance().Start();
  }
}
__attribute__((visibility("default"))) auto DetectorDetect(void) -> void {
  if ((detector_option & kDetectorOptionMemory) != 0) {
    MemoryDetect::GetInstance().Detect();
  }
  if ((detector_option & kDetectorOptionLock) != 0) {
    LockDetect::GetInstance().Detect();
  }
}
__attribute__((visibility("default"))) auto DetectorRegister(
    const char* lib_name) -> void {
  if (lib_name == nullptr) {
    return;
  }
  if ((detector_option & kDetectorOptionMemory) != 0) {
    MemoryDetect::GetInstance().Register(lib_name);
  }
  if ((detector_option & kDetectorOptionLock) != 0) {
    LockDetect::GetInstance().Register(lib_name);
  }
}
__attribute__((visibility("default"))) auto DetectorRegisterMain(void) -> void {
  if ((detector_option & kDetectorOptionMemory) != 0) {
    MemoryDetect::GetInstance().Register("");
  }
  if ((detector_option & kDetectorOptionLock) != 0) {
    LockDetect::GetInstance().Register("");
  }
}
}
